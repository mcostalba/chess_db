#include <algorithm>
#include <cstdint>
#include <cstdio>
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

struct __attribute__ ((packed)) KeyEntry {
    uint64_t key;
    uint32_t moveOffset;
};

typedef std::vector<KeyEntry> Keys;
typedef std::vector<Move> Moves;

inline bool operator<(const KeyEntry& f, const KeyEntry& s) {
    return f.key < s.key;
}

struct Stats {
    int64_t games;
    int64_t moves;
};

enum State {
    HEADER, BRACKET, COMMENT, NEW_MOVE, WHITE_MOVE, BLACK_MOVE, RESULT
};

enum Token {
    T_NONE, T_LF, T_SPACE, T_DOT, T_RESULT, T_DIGIT, T_MOVE, T_OPEN_BRACKET,
    T_CLOSE_BRACKET, T_OPEN_COMMENT, T_CLOSE_COMMENT
};

Token CharToToken[256];
Position RootPos;

void map(const char* fname, void** baseAddress, uint64_t* mapping) {

#ifndef _WIN32
    struct stat statbuf;
    int fd = ::open(fname, O_RDONLY);
    fstat(fd, &statbuf);
    *mapping = statbuf.st_size;
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

    std::string what = std::string(data, 10);
    std::cerr << desc << ": " << what << "' " << std::endl;
    exit(0);
}

void sort_by_frequency(Keys& kTable, const Moves& mTable, size_t start, size_t end) {

    struct Score {
        KeyEntry kt;
        Move move;
        int score;
    };

    std::vector<Score> scores;
    std::map<Move, int> moves;
    size_t size = end - start;
    scores.reserve(size);

    for (size_t i = 0; i < size; ++i)
    {
        Move move = mTable[kTable[i].moveOffset +  1]; // Next move
        scores.push_back(Score{kTable[i], move, 0});
        if (move != MOVE_NONE)
            moves[move]++;
    }

    for (size_t i = 0; i < size; ++i)
        scores[i].score = moves[scores[i].move];

    std::sort(scores.begin(), scores.end(),
              [](const Score& a, const Score& b) -> bool
              {
                  return    a.score > b.score
                        || (a.score == b.score && a.move > b.move);
              });

    for (size_t i = 0; i < size; ++i)
        kTable[i] = scores[i].kt;
}

void parse_game(const char* moves, const char* end, Keys& kTable, Moves& mTable) {

    StateInfo states[1024], *st = states;
    Position pos = RootPos;
    const char *cur = moves, *next = moves;

    while (cur < end)
    {
        while (*next++) {} // Go to next move

        bool givesCheck;
        Move move = pos.san_to_move(cur, &givesCheck, next == end);
        if (!move)
            error("Illegal move", cur);

        pos.do_move(move, *st++, givesCheck);
        mTable.push_back(move);
        kTable.push_back({pos.key(), uint32_t(mTable.size() * sizeof(Move))});
        cur = next;
    }

    mTable.push_back(MOVE_NONE); // Game separator
}

void parse_pgn(void* baseAddress, uint64_t size, Stats& stats,
               Keys& kTable, Moves& mTable) {

    int state = HEADER, prevState = HEADER;
    char moves[1024 * 8] = {};
    char* curMove = moves;
    char* end = curMove;
    size_t moveCnt = 0, gameCnt = 0;
    char* data = (char*)baseAddress;
    char* eof = data + size;

    for (  ; data < eof; ++data)
    {
        Token tk = CharToToken[*(uint8_t*)data];

        switch (state)
        {
        case HEADER:
            if (tk == T_OPEN_BRACKET)
                state = BRACKET;

            else if (tk == T_DIGIT)
                state = NEW_MOVE;

            else if (tk == T_OPEN_COMMENT)
            {
                prevState = state;
                state = COMMENT;
            }
            else if (tk != T_LF && tk != T_SPACE)
                error("Wrong header", data);
            break;

        case BRACKET:
            if (tk == T_CLOSE_BRACKET)
                state = HEADER;
            break;

        case COMMENT:
            if (tk == T_CLOSE_COMMENT)
                state = prevState;
            break;

        case NEW_MOVE:
            if (tk == T_DIGIT)
                continue;

            else if (tk == T_DOT)
                state = WHITE_MOVE;

            else if (tk == T_SPACE && end == curMove)
                continue;

            else if (tk == T_RESULT || *data == '-')
                state = RESULT;

            else if (tk == T_OPEN_COMMENT)
            {
                prevState = state;
                state = COMMENT;
            }
            else
                error("Wrong new move", data);
            break;

        case WHITE_MOVE:
            if (tk == T_MOVE || (tk == T_DIGIT && end != curMove))
                *end++ = *data;

            else if (tk == T_SPACE && end == curMove)
                continue;

            else if (tk == T_SPACE && end != curMove)
            {
                state = BLACK_MOVE;
                *end++ = 0; // Zero-terminating string
                curMove = end;
                moveCnt++;
            }

            else if (tk == T_OPEN_COMMENT)
            {
                prevState = state;
                state = COMMENT;
            }
            else
                error("Wrong white move", data);
            break;

        case BLACK_MOVE:
            if (tk == T_MOVE || (tk == T_DIGIT && end != curMove))
                *end++ = *data;

            else if (tk == T_SPACE && end == curMove)
                continue;

            else if (tk == T_SPACE && end != curMove)
            {
                state = NEW_MOVE;
                *end++ = 0; // Zero-terminating string
                curMove = end;
                moveCnt++;
            }
            else if (tk == T_DIGIT)
                state = RESULT;

            else if (tk == T_OPEN_COMMENT)
            {
                prevState = state;
                state = COMMENT;
            }
            else
                error("Wrong black move", data);
            break;

        case RESULT:
            if (tk == T_LF)
            {
                parse_game(moves, end, kTable, mTable);
                gameCnt++;
                state = HEADER;
                end = curMove = moves;
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

    CharToToken['\n'] = CharToToken['\r'] = T_LF;
    CharToToken[' '] = CharToToken['\t'] = T_SPACE;
    CharToToken['.'] = T_DOT;
    CharToToken['/'] = CharToToken['*'] = T_RESULT;
    CharToToken['['] = T_OPEN_BRACKET;
    CharToToken[']'] = T_CLOSE_BRACKET;
    CharToToken['{'] = T_OPEN_COMMENT;
    CharToToken['}'] = T_CLOSE_COMMENT;

    CharToToken['0'] = CharToToken['1'] = CharToToken['2'] = CharToToken['3'] =
    CharToToken['4'] = CharToToken['5'] = CharToToken['6'] = CharToToken['7'] =
    CharToToken['8'] = CharToToken['9'] = T_DIGIT;

    CharToToken['a'] = CharToToken['b'] = CharToToken['c'] = CharToToken['d'] =
    CharToToken['e'] = CharToToken['f'] = CharToToken['g'] = CharToToken['h'] =
    CharToToken['N'] = CharToToken['B'] = CharToToken['R'] = CharToToken['Q'] =
    CharToToken['K'] = CharToToken['x'] = CharToToken['+'] = CharToToken['#'] =
    CharToToken['='] = CharToToken['O'] = CharToToken['-'] = T_MOVE;
}

void process_pgn(const char* fname) {

    Keys kTable;
    Moves mTable;
    Stats stats;
    uint64_t size;
    void* baseAddress;

    map(fname, &baseAddress, &size);

    // Reserve enough capacity according to file size. This is a very crude
    // estimation, mainly we assume key index to be of same size of the pgn
    // file, and moves to be half size.
    kTable.reserve(size / sizeof(KeyEntry));
    mTable.reserve(size / 2 / sizeof(Move));

    std::cerr << "\nProcessing...";

    TimePoint elapsed = now();

    parse_pgn(baseAddress, size, stats, kTable, mTable);

    elapsed = now() - elapsed + 1; // Ensure positivity to avoid a 'divide by zero'

    std::cerr << "done\nSorting...";

    std::sort(kTable.begin(), kTable.end());

    size_t uniqueKeys = 1, last = 0;
    for (size_t idx = 1; idx < kTable.size(); ++idx)
        if (kTable[idx].key != kTable[idx - 1].key)
        {
            if (idx - last > 2)
               sort_by_frequency(kTable, mTable, last, idx);

            last = idx;
            uniqueKeys++;
        }

    std::cerr << "done\nWriting to files...";

    //First entry is reserved for the header
    const KeyEntry header({kTable.size() * sizeof(KeyEntry), 0});

    std::string posFile = std::string(fname) + ".idx";

    auto pFile = fopen(posFile.c_str(), "wb");
    fwrite(&header, sizeof(KeyEntry), 1, pFile);
    fwrite(kTable.data(), sizeof(KeyEntry), kTable.size(), pFile);
    fwrite(mTable.data(), sizeof(Move), mTable.size(), pFile);

    std::cerr << "done\n"
              << "\nGames: " << stats.games
              << "\nMoves: " << stats.moves
              << "\nUnique positions: " << 100 * uniqueKeys / stats.moves << "%"
              << "\nGames/second: " << 1000 * stats.games / elapsed
              << "\nMoves/second: " << 1000 * stats.moves / elapsed
              << "\nMBytes/second: " << float(size) / elapsed / 1000
              << "\nSize of index file (MB): " << ftell(pFile)
              << "\nIndex file: " << posFile
              << "\nProcessing time (ms): " << elapsed << "\n" << std::endl;

    fclose(pFile);
    unmap(baseAddress, size);
}

}
