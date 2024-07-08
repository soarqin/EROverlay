#pragma once

#include <windows.h>
#include <atomic>
#include <cstdint>

namespace er {
extern HMODULE gModule;
extern wchar_t gModulePath[MAX_PATH];
extern std::atomic_bool gRunning;
extern bool gShowMenu;
extern uint64_t gGameVersion;
}
