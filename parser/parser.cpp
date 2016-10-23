#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

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

// FEN string of the initial position, normal chess
const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// A list to keep track of the position states along the setup moves
StateInfo States[1024];
StateInfo* CurSt = States;

// At the start of every game a new position is copied form here
Position RootPos;

// Memory map the file and check it
uint8_t* map(const char* fname, void** baseAddress, uint64_t* mapping) {

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
    return (uint8_t*)*baseAddress;
}


void unmap(void* baseAddress, uint64_t mapping) {

#ifndef _WIN32
    munmap(baseAddress, mapping);
#else
    UnmapViewOfFile(baseAddress);
    CloseHandle((HANDLE)mapping);
#endif
}

struct Stats {
    int64_t games;
    int64_t moves;
    int64_t lines;
};

enum States {
    HEADER, BRACKET, COMMENT, NEW_MOVE, WHITE_MOVE, BLACK_MOVE, RESULT
};

enum Tokens {
    T_NONE, T_LF, T_SPACE, T_DOT, T_RESULT, T_DIGIT, T_MOVE, T_OPEN_BRACKET,
    T_CLOSE_BRACKET, T_OPEN_COMMENT, T_CLOSE_COMMENT
};


Tokens CharToToken[256];

void error(const std::string& desc, int64_t lineNumber, const char* data) {

    std::string what = std::string(data, 10);
    std::cerr << desc << " at line: " << lineNumber << ", '" << what << "' " << std::endl;
    exit(0);
}

bool parse_move(Position& pos, const char* san) {

    Move m = pos.do_san_move(san, CurSt++);
//    std::cerr << san << " key: " << pos.key() << " fen: " << pos.fen() << std::endl;
    return m != MOVE_NONE;
}

void parse_pgn(char* data, uint64_t size, Stats& stats) {

    Position pos = RootPos;
    int state = HEADER, prevState = HEADER;
    char buf[10] = {};
    char* san = buf;
    int64_t lineCnt = 1;
    int64_t moveCnt = 0, gameCnt = 0;
    char* end = data + size;

    for (  ; data < end; ++data)
    {
        Tokens tk = CharToToken[*(uint8_t*)data];

        if (tk == T_LF)
            lineCnt++;

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
                error("Wrong header", lineCnt, data);
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

            else if (tk == T_SPACE && san == buf)
                continue;

            else if (tk == T_RESULT || *data == '-')
                state = RESULT;

            else if (tk == T_OPEN_COMMENT)
            {
                prevState = state;
                state = COMMENT;
            }
            else
                error("Wrong new move", lineCnt, data);
            break;

        case WHITE_MOVE:
            if (tk == T_MOVE || (tk == T_DIGIT && san != buf))
                *san++ = *data;

            else if (tk == T_SPACE && san == buf)
                continue;

            else if (tk == T_SPACE && san != buf)
            {
                state = BLACK_MOVE;
                *san = 0;   // Zero-terminating string
                if (!parse_move(pos, buf))
                    error("Illegal white move", lineCnt, data);
                moveCnt++;
                san = buf;
            }

            else if (tk == T_OPEN_COMMENT)
            {
                prevState = state;
                state = COMMENT;
            }
            else
                error("Wrong white move", lineCnt, data);
            break;

        case BLACK_MOVE:
            if (tk == T_MOVE || (tk == T_DIGIT && san != buf))
                *san++ = *data;

            else if (tk == T_SPACE && san == buf)
                continue;

            else if (tk == T_SPACE && san != buf)
            {
                state = NEW_MOVE;
                *san = 0;   // Zero-terminating string
                if (!parse_move(pos, buf))
                    error("Illegal black move", lineCnt, data);
                moveCnt++;
                san = buf;
            }
            else if (tk == T_DIGIT)
                state = RESULT;

            else if (tk == T_OPEN_COMMENT)
            {
                prevState = state;
                state = COMMENT;
            }
            else
                error("Wrong black move", lineCnt, data);
            break;

        case RESULT:
            if (tk == T_LF)
            {
                gameCnt++;
                state = HEADER;
                pos = RootPos;
                CurSt = States + 1;
            }
            break;

        default:
            break;
        }
    }

    stats.games = gameCnt;
    stats.moves = moveCnt;
    stats.lines = lineCnt;
}

} // namespace

namespace Parser {


void init() {

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

    RootPos.set(StartFEN, false, CurSt++);
}


void process_pgn(const char* fname) {

    void* baseAddress;
    uint64_t size;
    char* data = (char*)map(fname, &baseAddress, &size);

    std::cerr << "Mapped " << std::string(fname)
              << "\nSize: " << size << " bytes" << std::endl;

    TimePoint elapsed = now();

    Stats stats;
    parse_pgn(data, size, stats);

    elapsed = now() - elapsed + 1; // Ensure positivity to avoid a 'divide by zero'

    std::cerr << "\nElpased time: " << elapsed << "ms"
              << "\nGames: " << stats.games
              << "\nMoves: " << stats.moves
              << "\nLines: " << stats.lines
              << "\nGames/second: " << 1000 * stats.games / elapsed
              << "\nMoves/second: " << 1000 * stats.moves / elapsed << std::endl;

    unmap(baseAddress, size);
}

}
