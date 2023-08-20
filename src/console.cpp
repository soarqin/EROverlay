#include "console.hpp"

#include <chrono>
#include <sstream>
#include <iomanip>

namespace er {

Console::Console(const char *ConsoleName) {
    initializeConsole(ConsoleName);
}

Console::~Console() noexcept {
    free();
}

void Console::initializeConsole(const char *ConsoleName) {
    AllocConsole();
    g_Handle = GetStdHandle(STD_OUTPUT_HANDLE);
    g_hWnd = GetConsoleWindow();
    freopen_s(&stream_in, "CONIN$", "r", stdin);
    freopen_s(&stream_out, "CONOUT$", "w", stdout);
    freopen_s(&stream_error, "CONOUT$", "w", stderr);
    SetConsoleTitleA(ConsoleName);
    ShowWindow(g_hWnd, SW_SHOW);
    printdbg("[+] Console::Initialized\n", Console::Colors::DEFAULT);
}

void Console::printdbg(const char *Text, Colors Color, ...) {
    SetConsoleTextAttribute(g_Handle, Color);
    va_list arg;
    va_start(arg, Color);
    vfprintf(stream_out, Text, arg);
    va_end(arg);
    SetConsoleTextAttribute(g_Handle, Colors::DEFAULT);
}

void Console::scandbg(const char *Text, ...) {
    va_list arg;
    va_start(arg, Text);
    vfscanf(stream_in, Text, arg);
    va_end(arg);
}

//  LOG EVENT FUNCTION
//  ONLY USE FOR ON|OFF DEBUG PRINTS
//<EXAMPLE>
//  TEXT: "[+] MENU:: ESP ; "
//  FLAG: bESP
//</EXAMPLE>
//  OUTPUT: [+] MENU:: ESP ; [<FLAG RESULT>]
void Console::logEvent(std::string TEXT, bool FLAG) {
    std::string output;
    int color;
    if (FLAG) {
        output = " [ON]\n";
        color = Colors::green;
    } else {
        output = " [OFF]\n";
        color = Colors::red;
    }
    std::string append = TEXT + output;
    gConsole->printdbg(append.c_str(), static_cast<Colors>(color));
}

std::string Console::getTimeString() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm{};
    localtime_s(&now_tm, &now_c);
    std::stringstream ss{};
    ss << std::put_time(&now_tm, "%Y-%m-%d");
    return ss.str();
}

void Console::free() {
    ShowWindow(g_hWnd, SW_HIDE);
    fclose(stream_in);
    fclose(stream_out);
    fclose(stream_error);
    FreeConsole();
}
}
