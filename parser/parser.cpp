#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "misc.h"
#include "position.h"

namespace {

struct __attribute__ ((packed)) KeyTableType {
    uint64_t posKey;
    uint32_t pgnKey;
};
KeyTableType* KeyTable;

inline bool operator<(const KeyTableType& f, const KeyTableType& s) {
    return f.posKey < s.posKey;
}

struct __attribute__ ((packed)) PgnTableType {
    uint32_t pgnKey;
    uint64_t ofs;
    uint32_t file_id;
};
PgnTableType* PgnTable;

inline bool operator<(const PgnTableType& f, const PgnTableType& s) {
    return f.pgnKey < s.pgnKey;
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
    HANDLE fd = CreateFile(fname.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
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

uint32_t parse_game(const char* moves, const char* end, KeyTableType** kt) {

    StateInfo states[1024], *st = states;
    Position pos = RootPos;
    KeyTableType* curKt = *kt;
    const char* cur = moves;

    while (cur < end)
    {
        if (!pos.do_san_move(cur, st++))
            error("Illegal move", cur);

        (*kt)->posKey = pos.key();
        ++(*kt);

        while (*cur++) {} // Go to next move
    }

    uint32_t pgnKey = uint32_t(pos.pgn_key());
    for ( ; curKt < *kt; ++curKt)
        curKt->pgnKey = pgnKey;

    return pgnKey;
}

void parse_pgn(void* baseAddress, uint64_t size, Stats& stats, bool dryRun) {

    KeyTableType* kt = KeyTable;
    PgnTableType* pt = PgnTable;

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
            {
                state = BRACKET;

                if (!pt->ofs)
                    pt->ofs = size_t(data - (char*)baseAddress);
            }
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
                if (!dryRun)
                {
                    pt->pgnKey = parse_game(moves, end, &kt);
                    pt++;
                }
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

    static StateInfo St;
    const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    RootPos.set(StartFEN, false, &St);

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

    uint64_t size;
    void* baseAddress;

    map(fname, &baseAddress, &size);

    KeyTable = new KeyTableType [1];
    PgnTable = new PgnTableType[1];

    std::cerr << "\nAnalizing...";

    Stats stats;
    parse_pgn(baseAddress, size, stats, true);  // Dry run to get file stats

    std::cerr << "done\nProcessing...";

    delete [] KeyTable;
    delete [] PgnTable;
    KeyTable = new KeyTableType [stats.moves * 5 / 4];
    PgnTable = new PgnTableType[stats.games * 5 / 4];

    TimePoint elapsed = now();

    parse_pgn(baseAddress, size, stats, false);

    elapsed = now() - elapsed + 1; // Ensure positivity to avoid a 'divide by zero'

    std::cerr << "done\nSorting...";

    std::sort(PgnTable, PgnTable + stats.games);
    std::sort(KeyTable, KeyTable + stats.moves);

    std::cerr << "done\nWriting to files...";

    std::string posFile = std::string(fname) + ".kidx";
    std::string gameFile = std::string(fname) + ".gidx";

    auto pFile = fopen(posFile.c_str(), "wb");
    fwrite(KeyTable, sizeof(KeyTableType), stats.moves, pFile);
    fclose(pFile);

    pFile = fopen(gameFile.c_str(), "wb");
    fwrite(PgnTable, sizeof(PgnTableType), stats.games, pFile);
    fclose(pFile);

    float ks = float(stats.moves * sizeof(KeyTableType)) / 1024 / 1024;
    float ps = float(stats.games * sizeof(PgnTableType)) / 1024 / 1024;

    std::cerr << "done\n"
              << "\nGames: " << stats.games
              << "\nMoves: " << stats.moves
              << "\nGames/second: " << 1000 * stats.games / elapsed
              << "\nMoves/second: " << 1000 * stats.moves / elapsed
              << "\nMBytes/second: " << float(size) / elapsed / 1000
              << "\nSize of positions index (MB): " << ks
              << "\nSize of games index (MB): " << ps
              << "\nPositions index: " << posFile
              << "\nGames index: " << gameFile
              << "\nProcessing time (ms): " << elapsed << "\n" << std::endl;

    delete [] KeyTable;
    delete [] PgnTable;
    unmap(baseAddress, size);
}

}
