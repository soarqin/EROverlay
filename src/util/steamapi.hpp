#pragma once

#include <string>

namespace er::steamapi {

bool init();
const wchar_t *getGameLanguage();
bool isDLCInstalled(unsigned int dlc);

}
