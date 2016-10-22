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


    // Memory map the file and check it
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


enum States {
    START, BRACKET, HEADER, MOVE_NUMBER, NEW_MOVE, WHITE_MOVE, BLACK_MOVE, COMMENT, RESULT
};

enum TokenType {
    T_NONE, T_LF, T_SPACE, T_DOT, T_SLASH, T_DIGIT, T_MOVE, T_OPEN_BRACKET, T_CLOSE_BRACKET,
    T_OPEN_COMMENT, T_CLOSE_COMMENT
};

static TokenType TokenMap[256];

void init_tokens() {

    TokenMap['\n'] = TokenMap['\r'] = T_LF;
    TokenMap[' '] = TokenMap['\t'] = T_SPACE;
    TokenMap['.'] = T_DOT;
    TokenMap['/'] = T_SLASH;
    TokenMap['['] = T_OPEN_BRACKET;
    TokenMap[']'] = T_CLOSE_BRACKET;
    TokenMap['{'] = T_OPEN_COMMENT;
    TokenMap['}'] = T_CLOSE_COMMENT;

    TokenMap['0'] = TokenMap['1'] = TokenMap['2'] = TokenMap['3'] =
    TokenMap['4'] = TokenMap['5'] = TokenMap['6'] = TokenMap['7'] =
    TokenMap['8'] = TokenMap['9'] = T_DIGIT;

    TokenMap['a'] = TokenMap['b'] = TokenMap['c'] = TokenMap['d'] =
    TokenMap['e'] = TokenMap['f'] = TokenMap['g'] = TokenMap['h'] =
    TokenMap['N'] = TokenMap['B'] = TokenMap['R'] = TokenMap['Q'] =
    TokenMap['K'] = TokenMap['x'] = TokenMap['+'] = TokenMap['#'] =
    TokenMap['='] = TokenMap['O'] = TokenMap['-'] = T_MOVE;
}

void parse_pgn(uint8_t* data, uint64_t size) {

    int state = START, prevState = START;
    char buf[10] = {};
    char* san = buf;
    int lineNumber = 1;
    uint8_t* end = data + size;

    for (  ; data < end; ++data)
    {
        TokenType tk = TokenMap[*data];

        if (tk == T_LF)
            lineNumber++;

        switch (state)
        {
        case START:
            if (tk == T_OPEN_BRACKET)
                state = BRACKET;
            else if (tk != T_SPACE && tk != T_LF)
                std::cerr << "Wrong start at line: " << lineNumber << ", '" << std::string((const char*)(data), 10) << "' " << tk << std::endl, exit(0);
            break;

        case BRACKET:
            if (tk == T_CLOSE_BRACKET)
                state = HEADER;
            break;

        case COMMENT:
              if (tk == T_CLOSE_COMMENT)
                  state = prevState;
              break;

        case HEADER:
            if (tk == T_OPEN_BRACKET)
                state = BRACKET;
            else if (tk == T_DIGIT)
                state = MOVE_NUMBER;
            else if (tk == T_OPEN_COMMENT)
            {
                prevState = state;
                state = COMMENT;
            }
            else if (tk != T_LF && tk != T_SPACE)
                std::cerr << "Wrong header at line: " << lineNumber << ", '" << std::string((const char*)(data), 10) << "' " << tk << std::endl, exit(0);
            break;

        case MOVE_NUMBER:
            if (tk == T_DOT)
            {
                state = WHITE_MOVE;
                san = buf;
            }
            else if (tk != T_DIGIT)
                std::cerr << "Wrong move number at line: " << lineNumber << ", '" << std::string((const char*)(data), 10) << "' " << tk << std::endl, exit(0);
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
//                std::cerr << "WM: " << std::string((const char*)buf)<< std::endl;
                san = buf;  // Here white move is parsed!
            }
            else if (tk == T_DIGIT)
                state = RESULT;
            else if (tk == T_OPEN_COMMENT)
            {
                prevState = state;
                state = COMMENT;
            }
            else
                std::cerr << "Wrong white move at line: " << lineNumber << ", '" << std::string((const char*)(data), 10) << "' " << tk << std::endl, exit(0);
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
//                std::cerr << "BM: " << std::string((const char*)buf) << std::endl;
                san = buf;  // Here black move is parsed!
            }
            else if (tk == T_DIGIT)
                state = RESULT;
            else if (tk == T_OPEN_COMMENT)
            {
                prevState = state;
                state = COMMENT;
            }
            else
                std::cerr << "Wrong black move at line: " << lineNumber << ", '" << std::string((const char*)(data), 10) << "' " << tk << std::endl, exit(0);
            break;

        case NEW_MOVE:
            if (tk == T_SPACE && san == buf)
                continue;
            else if (tk == T_DIGIT)
            {
                // Look ahead here! Can be next move or game result
                while (true)
                {
                    tk = TokenMap[*++data];
                    if (tk == T_DIGIT || tk == T_SPACE)
                        continue;

                    else if (tk == T_DOT)
                        state = WHITE_MOVE;
                    else
                        state = RESULT;
                    break;
                }
            }
            else if (tk == T_OPEN_COMMENT)
            {
                prevState = state;
                state = COMMENT;
            }
            else
                std::cerr << "Wrong move end at line: " << lineNumber << ", '" << std::string((const char*)(data), 10) << "' " << tk << std::endl, exit(0);
            break;

        case RESULT:
            if (tk == T_LF)
                state = START;
            break;

        default:
            break;
        }
    }

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

    parse_pgn(data, size);


    unmap(baseAddress, size);
    return 0;
}
