#include "global.hpp"

namespace er {

HMODULE gModule{};
wchar_t gModulePath[MAX_PATH]{};
std::atomic_bool gRunning = true;
std::atomic_bool gKillSwitch = false;
bool showMenu = false;

}
