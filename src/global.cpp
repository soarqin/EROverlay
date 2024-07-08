#include "global.hpp"

namespace er {

HMODULE gModule{};
wchar_t gModulePath[MAX_PATH]{};
std::atomic_bool gRunning = true;
bool gShowMenu = false;
/* default to v1.12 */
uint64_t gGameVersion = 0x0002000200000000ULL;

}
