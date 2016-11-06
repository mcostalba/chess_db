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
 * A Polyglot book is a series of "entries" of 16 bytes

key    uint64
move   uint16
weight uint16
learn  uint32

All integers are stored highest byte first (regardless of size)

The entries are ordered according to key. Lowest key first.
*/

struct PolyEntry {
    PKey     key;
    PMove    move;
    uint16_t weight;
    uint32_t learn;
};

// Acvoid alignment issues with sizeof(PolyEntry)
const size_t SizeOfPolyEntry = sizeof(PKey) + sizeof(PMove) + sizeof(uint16_t) + sizeof(uint32_t);

typedef std::vector<PolyEntry> Keys;

inline bool operator<(const PolyEntry& f, const PolyEntry& s) {
    return f.key  < s.key;
}

struct Stats {
    int64_t games;
    int64_t moves;
};

enum State {
    BOM, HEADER, TAG, FEN_TAG, BRACE_COMMENT, NUMERIC_ANNOTATION_GLYPH, VARIATION,
    MOVE_NUMBER, WHITE_MOVE, BLACK_MOVE, RESULT
};

enum Token {
    T_NONE, T_SPACES, T_DOT, T_QUOTES, T_DOLLAR, T_RESULT, T_DIGIT, T_MOVE,
    T_LEFT_BRACKET, T_RIGHT_BRACKET, T_LEFT_BRACE, T_RIGHT_BRACE,
    T_LEFT_PARENTHESIS, T_RIGHT_PARENTHESIS
};

Token ToToken[256];
Position RootPos;

void map(const char* fname, void** baseAddress, uint64_t* mapping, size_t* size) {

#ifndef _WIN32
    struct stat statbuf;
    int fd = ::open(fname, O_RDONLY);
    fstat(fd, &statbuf);
    *mapping = *size = statbuf.st_size;
    *baseAddress = mmap(nullptr, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);

    if (*baseAddress == MAP_FAILED) {
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

    if (!mmap) {
        std::cerr << "CreateFileMapping() failed" << std::endl;
        exit(1);
    }

    *size = ((size_t)size_high << 32) | (size_t)size_low;
    *mapping = (uint64_t)mmap;
    *baseAddress = MapViewOfFile(mmap, FILE_MAP_READ, 0, 0, 0);

    if (!*baseAddress) {
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

void error(const std::string& desc, const char* data) {

    std::string what = std::string(data, 50);
    std::cerr << desc << ": '" << what << "' " << std::endl;
    exit(0);
}

/// Convert a number of type T into a sequence of bytes and write to file in
/// big-endian format.

template<typename T> uint8_t* write(const T& n, uint8_t* data) {

  for (int i =  8 * (sizeof(T) - 1); i >= 0; i -= 8, ++data)
      *data = uint8_t(n >> i);

  return data;
}

template<> uint8_t* write(const PolyEntry& e, uint8_t* data) {

  data = write(e.key, data);
  data = write(e.move, data);
  data = write(e.weight, data);
  return write(e.learn, data);
}

size_t write_poly_file(const Keys& kTable, const std::string& fname) {

    uint8_t data[SizeOfPolyEntry];
    std::ofstream ofs;
    ofs.open(fname, std::ofstream::out | std::ofstream::binary);

    const PolyEntry* prev = &kTable[0];

    for (const PolyEntry& e : kTable)
        if (e.key != prev->key || e.move != prev->move)
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

void parse_game(const char* moves, const char* end, Keys& kTable,
                const char* fen, const char* fenEnd) {

    StateInfo states[1024], *st = states;
    Position pos = RootPos;
    const char *cur = moves, *next = moves;

    if (fenEnd != fen)
        pos.set(fen, false, st++);

    while (true)
    {
        while (*next++) {} // Go to next move

        Move move = pos.san_to_move(cur);
        if (!move)
        {
            std::string sep = pos.side_to_move() == WHITE ? "" : "..";
            std::cerr << "\nWrong move notation: " << sep << cur << "\n" << pos << std::endl;
            break;
        }

        kTable.push_back({pos.key(), to_polyglot(move), 1, 0});
        if (next == end)
            break;

        pos.do_move(move, *st++, pos.gives_check(move));
        cur = next;
    }
}

void parse_pgn(void* baseAddress, uint64_t size, Stats& stats, Keys& kTable) {

    int state = BOM;
    int stateStack[16], *stateSp = stateStack;
    char fen[256], *fenEnd = fen;
    char moves[1024 * 8], *curMove = moves;
    char* end = curMove;
    size_t moveCnt = 0, gameCnt = 0;
    char* data = (char*)baseAddress;
    char* eof = data + size;

    for (  ; data < eof; ++data)
    {
        Token tk = ToToken[*(uint8_t*)data];

        switch (state)
        {
        case BOM: // Some editors place UTF8 byte order mark at the beginning
            if (tk == T_LEFT_BRACKET)
            {
                state = HEADER;
                *stateSp++ = state; // Force TAG
                state = TAG;
            }
            else if (tk == T_DIGIT)
                state = MOVE_NUMBER;
            break;

        case HEADER:
            if (tk == T_LEFT_BRACKET)
            {
                *stateSp++ = state;
                state =   *(  data + 1) == 'F'
                       && *(++data + 1) == 'E'
                       && *(++data + 1) == 'N'
                       && *(++data + 1) == ' '
                       && *(++data + 1) == '"'
                       &&   ++data ? FEN_TAG : TAG;
            }
            else if (tk == T_DIGIT)
                state = MOVE_NUMBER;

            else if (tk == T_LEFT_BRACE)
            {
                *stateSp++ = state;
                state = BRACE_COMMENT;
            }
            else if (tk == T_RESULT)
                state = RESULT;

            else if (tk != T_SPACES)
                error("Wrong header", data);
            break;

        case TAG:
            if (tk == T_RIGHT_BRACKET)
                state = *(--stateSp);

            else if (tk == T_LEFT_BRACKET) // Nested bracket in a tag
                *stateSp++ = state;
            break;

        case FEN_TAG:
            if (tk == T_QUOTES) // Starting quotes have been consumed in HEADER
            {
                *fenEnd++ = 0; // Zero-terminating string
                state = TAG;
            }
            else
                *fenEnd++ = *data;
            break;

        case BRACE_COMMENT:
            if (tk == T_RIGHT_BRACE)
                state = *(--stateSp);

            else if (tk == T_LEFT_BRACE) // Nested comment
                *stateSp++ = state;
            break;

        case VARIATION:
            if (tk == T_RIGHT_PARENTHESIS)
                state = *(--stateSp);

            else if (tk == T_LEFT_PARENTHESIS) // Nested variation
                *stateSp++ = state;

            else if (tk == T_LEFT_BRACE) // Nested comment
            {
                *stateSp++ = state;
                state = BRACE_COMMENT;
            }
            break;

        case NUMERIC_ANNOTATION_GLYPH:
            if (tk == T_DIGIT)
                continue;
            state = *(--stateSp);
            break;

        case MOVE_NUMBER:
            if (tk == T_DIGIT)
                continue;

            else if (tk == T_DOT)
                state = WHITE_MOVE;

            else if (tk == T_SPACES && end == curMove)
                continue;

            else if (tk == T_RESULT)
                state = RESULT;

            else if (tk == T_LEFT_BRACE || tk == T_LEFT_PARENTHESIS || tk == T_DOLLAR)
            {
                *stateSp++ = state;
                state =  tk == T_LEFT_BRACE       ? BRACE_COMMENT
                       : tk == T_LEFT_PARENTHESIS ? VARIATION : NUMERIC_ANNOTATION_GLYPH;
            }
            else if (tk == T_MOVE) // This can happen when dot is missing, like: 1 e4 e5 2 Nf3 Nc6
            {
                state = WHITE_MOVE;
                *end++ = *data;
            }
            else if (tk == T_LEFT_BRACKET) // Missing result, start next game
            {
                if (end - moves) // Force RESULT
                    parse_game(moves, end, kTable, fen, fenEnd);
                gameCnt++;
                state = HEADER;
                end = curMove = moves;
                fenEnd = fen;

                *stateSp++ = state; // Force TAG
                state = TAG;
            }
            else
                error("Wrong move number", data);
            break;

        case WHITE_MOVE:
            if (tk == T_MOVE || (tk == T_DIGIT && end != curMove))
                *end++ = *data;

            else if (tk == T_SPACES && end == curMove)
                continue;

            else if (tk == T_SPACES && end != curMove)
            {
                state = BLACK_MOVE;
                *end++ = 0; // Zero-terminating string
                curMove = end;
                moveCnt++;
            }
            else if (*data == '-' && end != curMove) // Castling: '-' is also a result
                *end++ = *data;

            else if (tk == T_RESULT)
                state = RESULT;

            else if (tk == T_DIGIT && end == curMove) // Can be a result
            {
                // Check for castle with zero instead of big O
                if (data[0] == '0' && data[1] == '-' && data[2] == '0')
                    *end++ = *data;
            }
            else if (tk == T_LEFT_BRACE || tk == T_LEFT_PARENTHESIS || tk == T_DOLLAR)
            {
                *stateSp++ = state;
                state =  tk == T_LEFT_BRACE       ? BRACE_COMMENT
                       : tk == T_LEFT_PARENTHESIS ? VARIATION : NUMERIC_ANNOTATION_GLYPH;
            }
            else if (tk == T_DOT && end == curMove) // Game starts with black to
                state = BLACK_MOVE;                 // move as with FEN

            else
                error("Wrong white move", data);
            break;

        case BLACK_MOVE:
            if (tk == T_MOVE || (tk == T_DIGIT && end != curMove))
                *end++ = *data;

            else if (tk == T_SPACES && end == curMove)
                continue;

            else if (tk == T_SPACES && end != curMove)
            {
                state = MOVE_NUMBER;
                *end++ = 0; // Zero-terminating string
                curMove = end;
                moveCnt++;
            }
            else if (*data == '-' && end != curMove) // Castling: '-' is also a result
                *end++ = *data;

            else if (tk == T_RESULT)
                state = RESULT;

            else if ((tk == T_DIGIT || tk == T_DOT) && end == curMove) // Like 4... exd5 5. Bg2
            {
                // Check for castle with zero instead of big O
                if (data[0] == '0' && data[1] == '-' && data[2] == '0')
                    *end++ = *data;
            }
            else if (tk == T_LEFT_BRACE || tk == T_LEFT_PARENTHESIS || tk == T_DOLLAR)
            {
                *stateSp++ = state;
                state =  tk == T_LEFT_BRACE       ? BRACE_COMMENT
                       : tk == T_LEFT_PARENTHESIS ? VARIATION : NUMERIC_ANNOTATION_GLYPH;
            }
            else if (tk == T_LEFT_BRACKET) // Missing result, start next game
            {
                if (end - moves) // Force RESULT
                    parse_game(moves, end, kTable, fen, fenEnd);
                gameCnt++;
                state = HEADER;
                end = curMove = moves;
                fenEnd = fen;

                *stateSp++ = state; // Force TAG
                state = TAG;
            }
            else
                error("Wrong black move", data);
            break;

        case RESULT:
            if (tk == T_SPACES)
            {
                if (end - moves)
                    parse_game(moves, end, kTable, fen, fenEnd);
                gameCnt++;
                state = HEADER;
                end = curMove = moves;
                fenEnd = fen;
            }
            break;

        default:
            break;
        }
    }

    stats.games = gameCnt;
    stats.moves = moveCnt;
}

} // namespace

namespace Parser {


void init() {

    static StateInfo st;
    const char* startFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    RootPos.set(startFEN, false, &st);

    ToToken['\n'] = ToToken['\r'] = ToToken[' '] = ToToken['\t'] = T_SPACES;
    ToToken['!'] = ToToken['?'] = T_SPACES;
    ToToken['/'] = ToToken['-'] = ToToken['*'] = T_RESULT;
    ToToken['.'] = T_DOT;
    ToToken['"'] = T_QUOTES;
    ToToken['$'] = T_DOLLAR;
    ToToken['['] = T_LEFT_BRACKET;
    ToToken[']'] = T_RIGHT_BRACKET;
    ToToken['{'] = T_LEFT_BRACE;
    ToToken['}'] = T_RIGHT_BRACE;
    ToToken['('] = T_LEFT_PARENTHESIS;
    ToToken[')'] = T_RIGHT_PARENTHESIS;
    ToToken['0'] = ToToken['1'] = ToToken['2'] = ToToken['3'] =
    ToToken['4'] = ToToken['5'] = ToToken['6'] = ToToken['7'] =
    ToToken['8'] = ToToken['9'] = T_DIGIT;
    ToToken['a'] = ToToken['b'] = ToToken['c'] = ToToken['d'] =
    ToToken['e'] = ToToken['f'] = ToToken['g'] = ToToken['h'] =
    ToToken['N'] = ToToken['B'] = ToToken['R'] = ToToken['Q'] =
    ToToken['K'] = ToToken['x'] = ToToken['+'] = ToToken['#'] =
    ToToken['='] = ToToken['O'] = T_MOVE;
}

void process_pgn(const char* fname) {

    Keys kTable;
    Stats stats;
    uint64_t mapping, size;
    void* baseAddress;

    map(fname, &baseAddress, &mapping, &size);

    // Reserve enough capacity according to file size. This is a very crude
    // estimation, mainly we assume key index to be of 2 times the size of
    // the pgn file, and moves to be half size.
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
    size_t bookSize = write_poly_file(kTable, bookName);

    std::cerr << "done\n"
              << "\nGames: " << stats.games
              << "\nMoves: " << stats.moves
              << "\nUnique positions: " << 100 * uniqueKeys / stats.moves << "%"
              << "\nGames/second: " << 1000 * stats.games / elapsed
              << "\nMoves/second: " << 1000 * stats.moves / elapsed
              << "\nMBytes/second: " << float(size) / elapsed / 1000
              << "\nSize of index file (MB): " << bookSize
              << "\nBook file: " << bookName
              << "\nProcessing time (ms): " << elapsed << "\n" << std::endl;

    unmap(baseAddress, mapping);
}

}
