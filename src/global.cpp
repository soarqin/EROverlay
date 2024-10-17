#include "global.hpp"

namespace er {

HMODULE gModule{};
wchar_t gModulePath[MAX_PATH]{};
std::atomic_bool gRunning = true;
bool gShowMenu = false;
/* default to v1.15 */
uint64_t gGameVersion = 0x0002000500000000ULL;
bool gIsDLC01Installed;

}
