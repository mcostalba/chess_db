/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#else
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "misc.h"
#include "position.h"

namespace {

typedef uint64_t PKey;  // Polyglot key
typedef uint16_t PMove; // Polyglot move

/*
   A Polyglot book is a series of "entries" of 16 bytes:

    key    uint64
    move   uint16
    weight uint16
    learn  uint32

   All integers are stored highest byte first (regardless of size). The entries
   are ordered according to key. Lowest key first.
*/

struct PolyEntry {
    PKey     key;
    PMove    move;
    uint16_t weight;
    uint32_t learn;
};
typedef std::vector<PolyEntry> Keys;

// Avoid alignment issues with sizeof(PolyEntry)
const size_t SizeOfPolyEntry = sizeof(PKey) + sizeof(PMove) + sizeof(uint16_t) + sizeof(uint32_t);

inline bool operator<(const PolyEntry& f, const PolyEntry& s) {
    return f.key  < s.key;
}

struct Stats {
    int64_t games;
    int64_t moves;
    int64_t fixed;
};

enum Token {
    T_NONE, T_SPACES, T_RESULT, T_MINUS, T_DOT, T_QUOTES, T_DOLLAR,
    T_LEFT_BRACKET, T_RIGHT_BRACKET, T_LEFT_BRACE, T_RIGHT_BRACE,
    T_LEFT_PARENTHESIS, T_RIGHT_PARENTHESIS, T_ZERO, T_DIGIT, T_MOVE_HEAD, TOKEN_NB
};

enum State {
    HEADER, TAG, FEN_TAG, BRACE_COMMENT, VARIATION, NUMERIC_ANNOTATION_GLYPH,
    NEXT_MOVE, MOVE_NUMBER, NEXT_SAN, READ_SAN, RESULT, STATE_NB
};

enum Step : uint8_t {
    FAIL, CONTINUE, OPEN_TAG, OPEN_BRACE_COMMENT, READ_FEN, CLOSE_FEN_TAG,
    OPEN_VARIATION, START_NAG, POP_STATE, START_MOVE_NUMBER, START_NEXT_SAN,
    CASTLE_OR_RESULT, START_READ_SAN, READ_MOVE_CHAR, END_MOVE, START_RESULT,
    END_GAME, TAG_IN_BRACE, MISSING_RESULT
};

Token ToToken[256];
Step ToStep[STATE_NB][TOKEN_NB];
Position RootPos;

void map(const char* fname, void** baseAddress, uint64_t* mapping, size_t* size) {

#ifndef _WIN32
    struct stat statbuf;
    int fd = ::open(fname, O_RDONLY);
    fstat(fd, &statbuf);
    *mapping = *size = statbuf.st_size;
    *baseAddress = mmap(nullptr, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (*baseAddress == MAP_FAILED)
    {
        std::cerr << "Could not mmap() " << fname << std::endl;
        exit(1);
    }
#else
    HANDLE fd = CreateFile(fname, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD size_high;
    DWORD size_low = GetFileSize(fd, &size_high);
    HANDLE mmap = CreateFileMapping(fd, nullptr, PAGE_READONLY, size_high, size_low, nullptr);
    CloseHandle(fd);
    if (!mmap)
    {
        std::cerr << "CreateFileMapping() failed" << std::endl;
        exit(1);
    }
    *size = ((size_t)size_high << 32) | (size_t)size_low;
    *mapping = (uint64_t)mmap;
    *baseAddress = MapViewOfFile(mmap, FILE_MAP_READ, 0, 0, 0);
    if (!*baseAddress)
    {
        std::cerr << "MapViewOfFile() failed, name = " << fname
                  << ", error = " << GetLastError() << std::endl;
        exit(1);
    }
#endif
}

void unmap(void* baseAddress, uint64_t mapping) {

#ifndef _WIN32
    munmap(baseAddress, mapping);
#else
    UnmapViewOfFile(baseAddress);
    CloseHandle((HANDLE)mapping);
#endif
}

void error(Step* state, const char* data) {

    std::vector<std::string> stateDesc = {
        "HEADER", "TAG", "FEN_TAG", "BRACE_COMMENT", "VARIATION",
        "NUMERIC_ANNOTATION_GLYPH", "NEXT_MOVE", "MOVE_NUMBER",
        "NEXT_SAN", "READ_SAN", "RESULT"
    };

    for (int i = 0; i < STATE_NB; i++)
        if (ToStep[i] == state)
        {
            std::string what = std::string(data, 50);
            std::cerr << "Wrong " << stateDesc[i] << ": '"
                      << what << "' " << std::endl;
        }
    exit(0);
}

/// Convert a number of type T into a sequence of bytes in big-endian format

template<typename T> uint8_t* write(const T& n, uint8_t* data) {

    for (int i =  8 * (sizeof(T) - 1); i >= 0; i -= 8, ++data)
        *data = uint8_t(n >> i);

    return data;
}

template<> uint8_t* write(const PolyEntry& e, uint8_t* data) {

    data = write(e.key,    data);
    data = write(e.move,   data);
    data = write(e.weight, data);
    return write(e.learn,  data);
}

size_t write_poly_file(const Keys& kTable, const std::string& fname, bool full) {

    uint8_t data[SizeOfPolyEntry];
    std::ofstream ofs;
    ofs.open(fname, std::ofstream::out | std::ofstream::binary);

    const PolyEntry* prev = &kTable[0];

    for (const PolyEntry& e : kTable)
        if (e.key != prev->key || e.move != prev->move || full)
        {
            assert(e.weight);

            write(e, data);
            ofs.write((char*)data, SizeOfPolyEntry);
            prev = &e;
        }

    size_t size = ofs.tellp();
    ofs.close();
    return size;
}

void sort_by_frequency(Keys& kTable, size_t start, size_t end) {

    std::map<PMove, int> moves;

    for (size_t i = start; i < end; ++i)
        moves[kTable[i].move]++;

    for (size_t i = start; i < end; ++i)
        kTable[i].weight = moves[kTable[i].move];

    std::sort(kTable.begin() + start, kTable.begin() + end,
              [](const PolyEntry& a, const PolyEntry& b) -> bool
    {
        return    a.weight > b.weight
              || (a.weight == b.weight && a.move > b.move);
    });
}

inline PMove to_polyglot(Move m) {
    // A PolyGlot book move is encoded as follows:
    //
    // bit  0- 5: destination square (from 0 to 63)
    // bit  6-11: origin square (from 0 to 63)
    // bit 12-13: promotion piece (from KNIGHT == 1 to QUEEN == 4)
    //
    // Castling moves follow the "king captures rook" representation. If a book
    // move is a promotion, we have to convert it to our representation and in
    // all other cases, we can directly compare with a Move after having masked
    // out the special Move flags (bit 14-15) that are not supported by PolyGlot.
    if (m & PROMOTION)
        return PMove((m & 0xFFF) | ((promotion_type(m) - 1) << 12));

    return PMove(m & 0x3FFF);
}

template<bool DryRun = false>
const char* parse_game(const char* moves, const char* end, Keys& kTable,
                       const char* fen, const char* fenEnd, size_t& fixed,
                       const char* data) {

    StateInfo states[1024], *st = states;
    Position pos = RootPos;
    const char *cur = moves;

    if (fenEnd != fen)
        pos.set(fen, false, st++);

    // Use Polyglot 'learn' parameter to store game result in the upper 2 bits,
    // and game offset in the PGN file. Note that the offset is 8 bytes aligned
    // and points to "somewhere" in the game. It is up to the look up tool to
    // find game's boundaries. This allow us to index up to 8GB PGN files.
    // Result is stored in the upper 2 bits so that sorting by 'learn' allows
    // easy counting of result statistics.
    //
    // Result is coded from 0 to 3 as WHITE_WIN, BLACK_WIN, DRAW, RESULT_UNKNOWN
    int result = data ? (*(data-1) - '0') : 3;
    const uint32_t learn =  ((uint32_t(result) & 3) << 31)
                          | ((uintptr_t(data) >> 3) & 0x3FFFFFFF);
    while (cur < end)
    {
        Move move = pos.san_to_move(cur, end, fixed);
        if (move == MOVE_NONE)
        {
            if (!DryRun)
            {
                const char* sep = pos.side_to_move() == WHITE ? "" : "..";
                std::cerr << "\nWrong move notation: " << sep << cur
                          << "\n" << pos << std::endl;

            }
            return cur;
        }
        else if (move == MOVE_NULL)
            pos.do_null_move(*st++);
        else
        {
            if (!DryRun)
                kTable.push_back({pos.key(), to_polyglot(move), 1, learn});

            pos.do_move(move, *st++, pos.gives_check(move));
        }

        while (*cur++) {} // Go to next move
    }
    return end;
}

void parse_pgn(void* baseAddress, uint64_t size, Stats& stats, Keys& kTable) {

    Step* stateStack[16];
    Step**stateSp = stateStack;
    char fen[256], *fenEnd = fen;
    char moves[1024 * 8], *curMove = moves;
    char* end = curMove;
    size_t moveCnt = 0, gameCnt = 0, fixed = 0;
    char* data = (char*)baseAddress;
    char* eof = data + size;
    int stm = WHITE;
    Step* state = ToStep[HEADER];

    for (  ; data < eof; ++data)
    {
        Token tk = ToToken[*(uint8_t*)data];

        switch (state[tk])
        {
        case FAIL:
            error(state, data);
            break;

        case CONTINUE:
            break;

        case OPEN_TAG:
            *stateSp++ = state;
            state =   *(  data + 1) == 'F'
                   && *(++data + 1) == 'E'
                   && *(++data + 1) == 'N'
                   && *(++data + 1) == ' '
                   && *(++data + 1) == '"'
                   &&   ++data ? ToStep[FEN_TAG] : ToStep[TAG];
            break;

        case OPEN_BRACE_COMMENT:
            *stateSp++ = state;
            state = ToStep[BRACE_COMMENT];
            break;

        case READ_FEN:
            *fenEnd++ = *data;
            break;

        case CLOSE_FEN_TAG:
            *fenEnd++ = 0; // Zero-terminating string
            state = ToStep[TAG];
            if (strstr(fen, " b "))
                stm = BLACK;
            break;

        case OPEN_VARIATION:
            *stateSp++ = state;
            state = ToStep[VARIATION];
            break;

        case START_NAG:
            *stateSp++ = state;
            state = ToStep[NUMERIC_ANNOTATION_GLYPH];
            break;

        case POP_STATE:
            state = *(--stateSp);
            break;

        case START_MOVE_NUMBER:
            state = ToStep[MOVE_NUMBER];
            break;

        case START_NEXT_SAN:
            state = ToStep[NEXT_SAN];
            break;

        case CASTLE_OR_RESULT:
            if (data[2] != '0')
            {
                state = ToStep[RESULT];
                continue;
            }
            /* Fall through */

        case START_READ_SAN:
            *end++ = *data;
            state = ToStep[READ_SAN];
            break;

        case READ_MOVE_CHAR:
            *end++ = *data;
            break;

        case END_MOVE:
            *end++ = 0; // Zero-terminating string
            curMove = end;
            moveCnt++;
            state = ToStep[stm == WHITE ? NEXT_SAN : NEXT_MOVE];
            stm ^= 1;
            break;

        case START_RESULT:
            state = ToStep[RESULT];
            break;

        case END_GAME:
            if (*data != '\n') // Handle spaces in result, like 1/2 - 1/2
            {
                state = ToStep[RESULT];
                break;
            }
            parse_game(moves, end, kTable, fen, fenEnd, fixed, data);
            gameCnt++;
            end = curMove = moves;
            fenEnd = fen;
            state = ToStep[HEADER];
            stm = WHITE;
            break;

        case TAG_IN_BRACE:
             // Special case of missed brace close. Detect beginning of next game
             if (strncmp(data, "[Event ", 7))
                 break;

             /* Fall through */

        case MISSING_RESULT: // Missing result, next game already started
            parse_game(moves, end, kTable, fen, fenEnd, fixed, data);
            gameCnt++;
            end = curMove = moves;
            fenEnd = fen;
            state = ToStep[HEADER];
            stm = WHITE;

            *stateSp++ = state; // Fast forward into a TAG
            state = ToStep[TAG];
            break;

        default:
            assert(false);
            break;
        }
    }

    // Force accounting of last game if still pending. Many reason for this to
    // trigger: no newline at EOF, missing result, missing closing brace, etc.
    if (state != ToStep[HEADER] && end - moves)
    {
        parse_game(moves, end, kTable, fen, fenEnd, fixed, data);
        gameCnt++;
    }

    stats.games = gameCnt;
    stats.moves = moveCnt;
    stats.fixed = fixed;
}

} // namespace

const char* play_game(const Position& pos, Move move, const char* cur, const char* end) {

    size_t fixed;
    Keys k;
    StateInfo st;
    Position p = pos;
    p.do_move(move, st, pos.gives_check(move));
    while (*cur++) {} // Move to next move in game
    return cur < end ? parse_game<true>(cur, end, k, p.fen().c_str(),
                                        nullptr, fixed, nullptr) : cur;
}

namespace Parser {


void init() {

    static StateInfo st;
    const char* startFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    RootPos.set(startFEN, false, &st);

    ToToken['\n'] = ToToken['\r'] = ToToken[' '] = ToToken['\t'] = T_SPACES;
    ToToken['/'] = ToToken['*'] = T_RESULT;
    ToToken['-'] = T_MINUS;
    ToToken['.'] = T_DOT;
    ToToken['"'] = T_QUOTES;
    ToToken['$'] = T_DOLLAR;
    ToToken['['] = T_LEFT_BRACKET;
    ToToken[']'] = T_RIGHT_BRACKET;
    ToToken['{'] = T_LEFT_BRACE;
    ToToken['}'] = T_RIGHT_BRACE;
    ToToken['('] = T_LEFT_PARENTHESIS;
    ToToken[')'] = T_RIGHT_PARENTHESIS;
    ToToken['0'] = T_ZERO;
    ToToken['1'] = ToToken['2'] = ToToken['3'] =
    ToToken['4'] = ToToken['5'] = ToToken['6'] = ToToken['7'] =
    ToToken['8'] = ToToken['9'] = T_DIGIT;
    ToToken['a'] = ToToken['b'] = ToToken['c'] = ToToken['d'] =
    ToToken['e'] = ToToken['f'] = ToToken['g'] = ToToken['h'] =
    ToToken['N'] = ToToken['B'] = ToToken['R'] = ToToken['Q'] =
    ToToken['K'] = ToToken['O'] = ToToken['o'] = T_MOVE_HEAD;

    // Trailing move notations are ignored because SAN detector
    // does not need them and in some malformed PGN they appear
    // one blank apart from the corresponding move.
    ToToken['!'] = ToToken['?'] = ToToken['+'] = ToToken['#'] = T_SPACES;

    // STATE = HEADER
    //
    // Between tags, before game starts. Accept anything
    for (int i = 0; i < TOKEN_NB; i++)
        ToStep[HEADER][i] = CONTINUE;

    ToStep[HEADER][T_LEFT_BRACKET] = OPEN_TAG;
    ToStep[HEADER][T_LEFT_BRACE  ] = OPEN_BRACE_COMMENT;
    ToStep[HEADER][T_DIGIT       ] = START_MOVE_NUMBER;
    ToStep[HEADER][T_ZERO        ] = START_RESULT;
    ToStep[HEADER][T_RESULT      ] = START_RESULT;

    // STATE = TAG
    //
    // Between brackets in header section, generic tag
    for (int i = 0; i < TOKEN_NB; i++)
        ToStep[TAG][i] = CONTINUE;

    ToStep[TAG][T_RIGHT_BRACKET] = POP_STATE;

    // STATE = FEN_TAG
    //
    // Special tag to set a position from a FEN string
    for (int i = 0; i < TOKEN_NB; i++)
        ToStep[FEN_TAG][i] = READ_FEN;

    ToStep[FEN_TAG][T_QUOTES] = CLOSE_FEN_TAG;

    // STATE = BRACE_COMMENT
    //
    // Comment in braces, can appear almost everywhere. Note that brace comments
    // do not nest according to PGN standard.
    for (int i = 0; i < TOKEN_NB; i++)
        ToStep[BRACE_COMMENT][i] = CONTINUE;

    ToStep[BRACE_COMMENT][T_RIGHT_BRACE ] = POP_STATE;
    ToStep[BRACE_COMMENT][T_LEFT_BRACKET] = TAG_IN_BRACE; // Missed closing brace

    // STATE = VARIATION
    //
    // For the moment variations are ignored
    for (int i = 0; i < TOKEN_NB; i++)
        ToStep[VARIATION][i] = CONTINUE;

    ToStep[VARIATION][T_RIGHT_PARENTHESIS] = POP_STATE;
    ToStep[VARIATION][T_LEFT_PARENTHESIS ] = OPEN_VARIATION; // Nested
    ToStep[VARIATION][T_LEFT_BRACE       ] = OPEN_BRACE_COMMENT;

    // STATE = NUMERIC_ANNOTATION_GLYPH
    //
    // Just read a single number
    for (int i = 0; i < TOKEN_NB; i++)
        ToStep[NUMERIC_ANNOTATION_GLYPH][i] = POP_STATE;

    ToStep[NUMERIC_ANNOTATION_GLYPH][T_ZERO ] = CONTINUE;
    ToStep[NUMERIC_ANNOTATION_GLYPH][T_DIGIT] = CONTINUE;

    // STATE = NEXT_MOVE
    //
    // Check for the beginning of the next move number
    for (int i = 0; i < TOKEN_NB; i++)
        ToStep[NEXT_MOVE][i] = CONTINUE;

    ToStep[NEXT_MOVE][T_LEFT_PARENTHESIS] = OPEN_VARIATION;
    ToStep[NEXT_MOVE][T_LEFT_BRACE      ] = OPEN_BRACE_COMMENT;
    ToStep[NEXT_MOVE][T_LEFT_BRACKET    ] = MISSING_RESULT;
    ToStep[NEXT_MOVE][T_DOLLAR          ] = START_NAG;
    ToStep[NEXT_MOVE][T_RESULT          ] = START_RESULT;
    ToStep[NEXT_MOVE][T_ZERO            ] = START_RESULT;
    ToStep[NEXT_MOVE][T_DOT             ] = FAIL;
    ToStep[NEXT_MOVE][T_MOVE_HEAD       ] = FAIL;
    ToStep[NEXT_MOVE][T_MINUS           ] = FAIL;
    ToStep[NEXT_MOVE][T_DIGIT           ] = START_MOVE_NUMBER;

    // STATE = MOVE_NUMBER
    //
    // Continue until a dot is found, to tolerate missing dots,
    // stop at first space, then start NEXT_SAN that will handle
    // head trailing spaces. We can alias with a result like 1-0 or 1/2-1/2
    ToStep[MOVE_NUMBER][T_ZERO  ] = CONTINUE;
    ToStep[MOVE_NUMBER][T_DIGIT ] = CONTINUE;
    ToStep[MOVE_NUMBER][T_RESULT] = START_RESULT;
    ToStep[MOVE_NUMBER][T_MINUS ] = START_RESULT;
    ToStep[MOVE_NUMBER][T_SPACES] = START_NEXT_SAN;
    ToStep[MOVE_NUMBER][T_DOT   ] = START_NEXT_SAN;

    // STATE = NEXT_SAN
    //
    // Check for the beginning of the next move SAN
    for (int i = 0; i < TOKEN_NB; i++)
        ToStep[NEXT_SAN][i] = CONTINUE;

    ToStep[NEXT_SAN][T_LEFT_PARENTHESIS] = OPEN_VARIATION;
    ToStep[NEXT_SAN][T_LEFT_BRACE      ] = OPEN_BRACE_COMMENT;
    ToStep[NEXT_SAN][T_LEFT_BRACKET    ] = MISSING_RESULT;
    ToStep[NEXT_SAN][T_DOLLAR          ] = START_NAG;
    ToStep[NEXT_SAN][T_RESULT          ] = START_RESULT;
    ToStep[NEXT_SAN][T_ZERO            ] = CASTLE_OR_RESULT;  // 0-0 or 0-1
    ToStep[NEXT_SAN][T_DOT             ] = CONTINUE;          // Like 4... exd5
    ToStep[NEXT_SAN][T_DIGIT           ] = START_MOVE_NUMBER; // Same as above
    ToStep[NEXT_SAN][T_MOVE_HEAD       ] = START_READ_SAN;
    ToStep[NEXT_SAN][T_MINUS           ] = START_READ_SAN;    // Null move "--"

    // STATE = READ_SAN
    //
    // Just read a single move SAN until a space is reached
    for (int i = 0; i < TOKEN_NB; i++)
        ToStep[READ_SAN][i] = READ_MOVE_CHAR;

    ToStep[READ_SAN][T_SPACES    ] = END_MOVE;
    ToStep[READ_SAN][T_LEFT_BRACE] = OPEN_BRACE_COMMENT;

    // STATE = RESULT
    //
    // Ignore anything until a space is reached
    for (int i = 0; i < TOKEN_NB; i++)
        ToStep[RESULT][i] = CONTINUE;

    ToStep[RESULT][T_SPACES] = END_GAME;
}

void process_pgn(int argc, char* argv[]) {

    Keys kTable;
    Stats stats;
    uint64_t mapping, size;
    void* baseAddress;

    if (argc < 2)
    {
        std::cerr << "Missing PGN file name..." << std::endl;
        exit(0);
    }

    const char* fname = argv[1];
    bool full = (argc > 2) && !strcmp(argv[2], "full");

    map(fname, &baseAddress, &mapping, &size);

    // Reserve enough capacity according to file size. This is a very crude
    // estimation, mainly we assume key index to be of 2 times the size of
    // the pgn file.
    kTable.reserve(2 * size / sizeof(PolyEntry));

    std::cerr << "\nProcessing...";

    TimePoint elapsed = now();

    parse_pgn(baseAddress, size, stats, kTable);

    elapsed = now() - elapsed + 1; // Ensure positivity to avoid a 'divide by zero'

    std::cerr << "done\nSorting...";

    std::sort(kTable.begin(), kTable.end());

    size_t uniqueKeys = 1, last = 0;
    for (size_t idx = 1; idx < kTable.size(); ++idx)
        if (kTable[idx].key != kTable[idx - 1].key)
        {
            if (idx - last > 2)
                sort_by_frequency(kTable, last, idx);

            last = idx;
            uniqueKeys++;
        }

    std::cerr << "done\nWriting Polygot book...";

    std::string bookName = std::string(fname);
    size_t lastdot = bookName.find_last_of(".");
    if (lastdot != std::string::npos)
        bookName = bookName.substr(0, lastdot);
    bookName += ".bin";
    size_t bookSize = write_poly_file(kTable, bookName, full);

    std::cerr << "done\n"
              << "\nGames: " << stats.games
              << "\nMoves: " << stats.moves
              << "\nIncorrect moves: " << stats.fixed
              << "\nUnique positions: " << 100 * uniqueKeys / stats.moves << "%"
              << "\nGames/second: " << 1000 * stats.games / elapsed
              << "\nMoves/second: " << 1000 * stats.moves / elapsed
              << "\nMBytes/second: " << float(size) / elapsed / 1000
              << "\nSize of index file (bytes): " << bookSize
              << "\nBook file: " << bookName
              << "\nProcessing time (ms): " << elapsed << "\n" << std::endl;

    unmap(baseAddress, mapping);
}

}
