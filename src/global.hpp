#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>

namespace er {
extern HMODULE gModule;
extern wchar_t gModulePath[MAX_PATH];
extern std::atomic_bool gRunning;
extern bool gShowMenu;
extern uint64_t gGameVersion;
extern bool gIsDLC01Installed;
}
