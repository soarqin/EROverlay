#include "global.hpp"

namespace er {

HMODULE gModule{};
wchar_t gModulePath[MAX_PATH]{};
std::atomic_bool gRunning = true;
bool showMenu = false;

}
