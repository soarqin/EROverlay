#include "config.hpp"

#include "util/string.hpp"

#include <ini.h>

#include <filesystem>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>

namespace er {

std::vector<int> mapStringToVKey(const std::string &name) {
    static const std::map<std::string, int> sVKeyMap = {
        {"LWIN", VK_LWIN | 0x8000},
        {"WIN", VK_LWIN | 0x8000},
        {"RWIN", VK_RWIN | 0x8000},
        {"ALT", VK_MENU | 0x8000},
        {"LALT", VK_LMENU | 0x8000},
        {"RALT", VK_RMENU | 0x8000},
        {"CTRL", VK_CONTROL | 0x8000},
        {"LCTRL", VK_LCONTROL | 0x8000},
        {"RCTRL", VK_RCONTROL | 0x8000},
        {"CTRL", VK_SHIFT | 0x8000},
        {"LSHIFT", VK_LSHIFT | 0x8000},
        {"RSHIFT", VK_RSHIFT | 0x8000},
        {"LBUTTON", VK_LBUTTON},
        {"RBUTTON", VK_RBUTTON},
        {"CANCEL", VK_CANCEL},
        {"MBUTTON", VK_MBUTTON},
        {"XBUTTON1", VK_XBUTTON1},
        {"XBUTTON2", VK_XBUTTON2},
        {"BACK", VK_BACK},
        {"BACKSPACE", VK_BACK},
        {"TAB", VK_TAB},
        {"CLEAR", VK_CLEAR},
        {"RETURN", VK_RETURN},
        {"ENTER", VK_RETURN},
        {"PAUSE", VK_PAUSE},
        {"CAPITAL", VK_CAPITAL},
        {"CAPSLOCK", VK_CAPITAL},
        {"KANA", VK_KANA},
        {"HANGUL", VK_HANGUL},
        {"JUNJA", VK_JUNJA},
        {"FINAL", VK_FINAL},
        {"HANJA", VK_HANJA},
        {"KANJI", VK_KANJI},
        {"ESCAPE", VK_ESCAPE},
        {"ESC", VK_ESCAPE},
        {"CONVERT", VK_CONVERT},
        {"NONCONVERT", VK_NONCONVERT},
        {"ACCEPT", VK_ACCEPT},
        {"MODECHANGE", VK_MODECHANGE},
        {"SPACE", VK_SPACE},
        {"PRIOR", VK_PRIOR},
        {"NEXT", VK_NEXT},
        {"END", VK_END},
        {"HOME", VK_HOME},
        {"LEFT", VK_LEFT},
        {"UP", VK_UP},
        {"RIGHT", VK_RIGHT},
        {"DOWN", VK_DOWN},
        {"SELECT", VK_SELECT},
        {"PRINT", VK_PRINT},
        {"EXECUTE", VK_EXECUTE},
        {"SNAPSHOT", VK_SNAPSHOT},
        {"INSERT", VK_INSERT},
        {"DELETE", VK_DELETE},
        {"HELP", VK_HELP},
        {"0", 0x30},
        {"1", 0x31},
        {"2", 0x32},
        {"3", 0x33},
        {"4", 0x34},
        {"5", 0x35},
        {"6", 0x36},
        {"7", 0x37},
        {"8", 0x38},
        {"9", 0x39},
        {"A", 0x41},
        {"B", 0x42},
        {"C", 0x43},
        {"D", 0x44},
        {"E", 0x45},
        {"F", 0x46},
        {"G", 0x47},
        {"H", 0x48},
        {"I", 0x49},
        {"J", 0x4A},
        {"K", 0x4B},
        {"L", 0x4C},
        {"M", 0x4D},
        {"N", 0x4E},
        {"O", 0x4F},
        {"P", 0x50},
        {"Q", 0x51},
        {"R", 0x52},
        {"S", 0x53},
        {"T", 0x54},
        {"U", 0x55},
        {"V", 0x56},
        {"W", 0x57},
        {"X", 0x58},
        {"Y", 0x59},
        {"Z", 0x5A},
        {"APPS", VK_APPS},
        {"SLEEP", VK_SLEEP},
        {"NUMPAD0", VK_NUMPAD0},
        {"NUMPAD1", VK_NUMPAD1},
        {"NUMPAD2", VK_NUMPAD2},
        {"NUMPAD3", VK_NUMPAD3},
        {"NUMPAD4", VK_NUMPAD4},
        {"NUMPAD5", VK_NUMPAD5},
        {"NUMPAD6", VK_NUMPAD6},
        {"NUMPAD7", VK_NUMPAD7},
        {"NUMPAD8", VK_NUMPAD8},
        {"NUMPAD9", VK_NUMPAD9},
        {"NUM0", VK_NUMPAD0},
        {"NUM1", VK_NUMPAD1},
        {"NUM2", VK_NUMPAD2},
        {"NUM3", VK_NUMPAD3},
        {"NUM4", VK_NUMPAD4},
        {"NUM5", VK_NUMPAD5},
        {"NUM6", VK_NUMPAD6},
        {"NUM7", VK_NUMPAD7},
        {"NUM8", VK_NUMPAD8},
        {"NUM9", VK_NUMPAD9},
        {"MULTIPLY", VK_MULTIPLY},
        {"ADD", VK_ADD},
        {"SUBTRACT", VK_SUBTRACT},
        {"MINUS", VK_SUBTRACT},
        {"DECIMAL", VK_DECIMAL},
        {"DIVIDE", VK_DIVIDE},
        {"F1", VK_F1},
        {"F2", VK_F2},
        {"F3", VK_F3},
        {"F4", VK_F4},
        {"F5", VK_F5},
        {"F6", VK_F6},
        {"F7", VK_F7},
        {"F8", VK_F8},
        {"F9", VK_F9},
        {"F10", VK_F10},
        {"F11", VK_F11},
        {"F12", VK_F12},
        {"F13", VK_F13},
        {"F14", VK_F14},
        {"F15", VK_F15},
        {"F16", VK_F16},
        {"F17", VK_F17},
        {"F18", VK_F18},
        {"F19", VK_F19},
        {"F20", VK_F20},
        {"F21", VK_F21},
        {"F22", VK_F22},
        {"F23", VK_F23},
        {"F24", VK_F24},
        {"NUMLOCK", VK_NUMLOCK},
        {"SCROLL", VK_SCROLL},
        {"BROWSER_BACK", VK_BROWSER_BACK},
        {"BROWSER_FORWARD", VK_BROWSER_FORWARD},
        {"BROWSER_REFRESH", VK_BROWSER_REFRESH},
        {"BROWSER_STOP", VK_BROWSER_STOP},
        {"BROWSER_SEARCH", VK_BROWSER_SEARCH},
        {"BROWSER_FAVORITES", VK_BROWSER_FAVORITES},
        {"BROWSER_HOME", VK_BROWSER_HOME},
        {"VOLUME_MUTE", VK_VOLUME_MUTE},
        {"VOLUME_DOWN", VK_VOLUME_DOWN},
        {"VOLUME_UP", VK_VOLUME_UP},
        {"MEDIA_NEXT_TRACK", VK_MEDIA_NEXT_TRACK},
        {"MEDIA_PREV_TRACK", VK_MEDIA_PREV_TRACK},
        {"MEDIA_STOP", VK_MEDIA_STOP},
        {"MEDIA_PLAY_PAUSE", VK_MEDIA_PLAY_PAUSE},
        {"LAUNCH_MAIL", VK_LAUNCH_MAIL},
        {"LAUNCH_MEDIA_SELECT", VK_LAUNCH_MEDIA_SELECT},
        {"LAUNCH_APP1", VK_LAUNCH_APP1},
        {"LAUNCH_APP2", VK_LAUNCH_APP2},
        {"=", VK_OEM_PLUS},
        {",", VK_OEM_COMMA},
        {"-", VK_OEM_MINUS},
        {".", VK_OEM_PERIOD},
        {";", VK_OEM_1},
        {"/", VK_OEM_2},
        {"~", VK_OEM_3},
        {"`", VK_OEM_3},
        {"[", VK_OEM_4},
        {"\\", VK_OEM_5},
        {"]", VK_OEM_6},
        {"'", VK_OEM_7},
        {"PROCESSKEY", VK_PROCESSKEY},
        {"ATTN", VK_ATTN},
        {"CRSEL", VK_CRSEL},
        {"EXSEL", VK_EXSEL},
        {"EREOF", VK_EREOF},
        {"PLAY", VK_PLAY},
        {"ZOOM", VK_ZOOM},
        {"PA1", VK_PA1},
        {"OEM_CLEAR", VK_OEM_CLEAR},
    };

    std::string str = name;
    for (auto &c: str) { c = char(std::toupper(c)); }
    auto sl = util::splitString(str, '+');
    std::vector<int> res;
    if (sl.empty()) {
        return res;
    }
    for (const auto &s: sl) {
        auto ite2 = sVKeyMap.find(s);
        if (ite2 == sVKeyMap.end()) {
            return res;
        }
        res.emplace_back(ite2->second);
    }
    return res;
}

Config gConfig;

void Config::loadDir(const std::wstring &dir) {
    std::error_code ec;
    for (const auto &entry: std::filesystem::directory_iterator(dir, ec)) {
        fflush(stderr);
        if (entry.is_regular_file() && entry.path().extension() == L".ini") {
            load(entry.path(), entry.path().stem().string());
        }
    }
}

void Config::load(const std::wstring &filename, const std::string &modname) {
    auto *f = _wfopen(filename.c_str(), L"r");
    if (f == nullptr) {
        fwprintf(stderr, L"Unable to open %ls\n", filename.c_str());
        return;
    }
    struct ConfigData {
        std::map<std::string, std::string> &entries;
        const std::string &modname;
    } configData = {entries_, modname};
    ini_parse_file(f, [](void *user, const char *section, const char *name, const char *value) {
        auto &configData = *static_cast<ConfigData *>(user);
        auto &entries = configData.entries;
        auto modulename = configData.modname;
        if (!modulename.empty()) modulename += '.';
        if (section != nullptr && section[0] != '\0') {
            modulename = modulename + section + '.';
        }
        entries[modulename + name] = value;
        return 1;
    }, &configData);
    fclose(f);
}

const std::string &Config::operator[](const std::string &key) const {
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        static const std::string empty;
        return empty;
    }
    return it->second;
}

const std::string &Config::get(const std::string &key, const std::string &defaultValue) const {
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return defaultValue;
    }
    return it->second;
}

std::wstring Config::getw(const std::string &key, const std::wstring &defaultValue) const {
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return defaultValue;
    }
    std::wstring ws;
    ws.resize(MultiByteToWideChar(CP_UTF8, 0, it->second.c_str(), -1, nullptr, 0));
    MultiByteToWideChar(CP_UTF8, 0, it->second.c_str(), -1, ws.data(), (int)ws.size());
    while (!ws.empty() && ws.back() == 0) ws.pop_back();
    return ws;
}

bool Config::enabled(const std::string &key) const {
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return false;
    }
    const auto &s = it->second;
    return s == "on" || s == "true" || s == "1";
}

std::vector<int> Config::getVirtualKey(const std::string &key, const std::vector<int> &defaultValue) const {
    const auto &s = get(key, "");
    if (s.empty()) return defaultValue;
    auto vkey = mapStringToVKey(s);
    if (vkey.empty()) return defaultValue;
    /* Check for all vkey values, if all with 0x8000, remove last one */
    bool allWith0x8000 = true;
    for (auto &vk: vkey) {
        if ((vk & 0x8000) == 0) {
            allWith0x8000 = false;
            break;
        }
    }
    if (allWith0x8000) {
        vkey.back() &= 0x7FFF;
    }
    return vkey;
}

}
