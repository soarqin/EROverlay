#pragma once

#include <windows.h>
#include <atomic>

namespace er {
extern HMODULE gModule;
extern wchar_t gModulePath[MAX_PATH];
extern std::atomic_bool gRunning;
extern bool showMenu;
}
