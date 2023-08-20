#pragma once

#include <windows.h>
#include <memory>
#include <string>
#include <cstdio>

namespace er {

class Console {
public:
    enum Colors {
        dark_blue = 1,
        dark_green = 2,
        dark_teal = 3,
        dark_red = 4,
        dark_pink = 5,
        dark_yellow = 6,
        dark_white = 7,
        dark_gray = 8,
        blue = 9,
        green = 10,
        teal = 11,
        red = 12,
        pink = 13,
        yellow = 14,
        white = 15,
        DEFAULT = 15,
    };

    HANDLE g_Handle;
    HWND g_hWnd;
    FILE *stream_in;
    FILE *stream_out;
    FILE *stream_error;
    bool verbose = false;

    explicit Console(const char *ConsoleName);
    ~Console() noexcept;
    Console(Console const &) = delete;
    Console(Console &&) = delete;
    Console &operator=(Console const &) = delete;
    Console &operator=(Console &&) = delete;

    void initializeConsole(const char *ConsoleName);
    void printdbg(const char *Text, Colors Color, ...);
    void scandbg(const char *Text, ...);
    void logEvent(std::string TEXT, bool FLAG);
    std::string getTimeString();
    void free();
};
inline std::unique_ptr<Console> gConsole;
}