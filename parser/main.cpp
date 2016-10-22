#include <cstdint>
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


/// Memory map the file and check it
uint8_t* map(char* fname, void** baseAddress, uint64_t* mapping) {

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
    int games;
    int moves;
    int lines;
};

enum States {
    HEADER, BRACKET, COMMENT, NEW_MOVE, WHITE_MOVE, BLACK_MOVE, RESULT
};

enum Tokens {
    T_NONE, T_LF, T_SPACE, T_DOT, T_RESULT, T_DIGIT, T_MOVE, T_OPEN_BRACKET,
    T_CLOSE_BRACKET, T_OPEN_COMMENT, T_CLOSE_COMMENT
};

static Tokens CharToToken[256];

void init_tokens() {

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

void error(const std::string& desc, int lineNumber, uint8_t* data) {

    std::string what = std::string((const char*)(data), 10);
    std::cerr << desc << " at line: " << lineNumber << ", '" << what << "' " << std::endl;
    exit(0);
}

void parse_pgn(uint8_t* data, uint64_t size, Stats& stats) {

    int state = HEADER, prevState = HEADER;
    char buf[10] = {};
    char* san = buf;
    int lineCnt = 1;
    int moveCnt = 0, gameCnt = 0;
    uint8_t* end = data + size;

    for (  ; data < end; ++data)
    {
        Tokens tk = CharToToken[*data];

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
                *san = 0;   // Zero-terminating string, here white move is parsed!
                moveCnt++;
                san = buf;
            }

            else if (tk == T_OPEN_COMMENT)
            {
                prevState = state;
                state = COMMENT;
            }
            else
                error("Wrong white move end", lineCnt, data);
            break;

        case BLACK_MOVE:
            if (tk == T_MOVE || (tk == T_DIGIT && san != buf))
                *san++ = *data;

            else if (tk == T_SPACE && san == buf)
                continue;

            else if (tk == T_SPACE && san != buf)
            {
                state = NEW_MOVE;
                *san = 0;   // Zero-terminating string, here black move is parsed!
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
                error("Wrong black move end", lineCnt, data);
            break;

        case RESULT:
            if (tk == T_LF)
            {
                gameCnt++;
                state = HEADER;
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

int main(int argc, char* argv[]) {

    init_tokens();

    if (argc < 2)
    {
        std::cerr << "Missing pgn file name..." << std::endl;
        return 0;
    }

    void* baseAddress;
    uint64_t size;
    uint8_t* data = map(argv[1], &baseAddress, &size);

    std::cerr << "Mapped " << std::string(argv[1])
            << "\nSize: " << size << std::endl;

    Stats stats;
    parse_pgn(data, size, stats);

    std::cerr << "Parsed " << stats.games << " games, "
              << stats.moves << " moves, "
              << stats.lines << " lines" << std::endl;

    unmap(baseAddress, size);
    return 0;
}
