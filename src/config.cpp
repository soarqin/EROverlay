#include "config.hpp"

#include "global.hpp"
#include "input.hpp"
#include "util/ini.hpp"
#include "util/string.hpp"

#include <charconv>
#include <filesystem>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cctype>
#include <cwchar>

namespace er {

namespace {
std::string_view numberText(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1);
    return text;
}
} // namespace

bool parseConfigNumber(std::string_view text, long long &value) {
    text = numberText(text);
    return std::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc{};
}

bool parseConfigNumber(std::string_view text, double &value) {
    bool percent = !text.empty() && text.back() == '%';
    if (percent)
        text.remove_suffix(1);
    text = numberText(text);
    if (std::from_chars(text.data(), text.data() + text.size(), value).ec != std::errc{})
        return false;
    if (percent)
        value /= 100.0;
    return true;
}

int mapStringToVirtualKey(const std::string &name) {
    static const std::map<std::string, int> sVKeyMap = {
        {"WIN", input::KEY_MOD_SUPER},
        {"LWIN", VK_LWIN},
        {"RWIN", VK_RWIN},
        {"ALT", input::KEY_MOD_ALT},
        {"LALT", VK_LMENU},
        {"RALT", VK_RMENU},
        {"CTRL", input::KEY_MOD_CTRL},
        {"CONTROL", input::KEY_MOD_CTRL},
        {"LCTRL", VK_LCONTROL},
        {"LCONTROL", VK_LCONTROL},
        {"RCTRL", VK_RCONTROL},
        {"RCONTROL", VK_RCONTROL},
        {"SHIFT", input::KEY_MOD_SHIFT},
        {"LSHIFT", VK_LSHIFT},
        {"RSHIFT", VK_RSHIFT},
        {"LBUTTON", VK_LBUTTON},
        {"LMB", VK_LBUTTON},
        {"RBUTTON", VK_RBUTTON},
        {"RMB", VK_RBUTTON},
        {"CANCEL", VK_CANCEL},
        {"MBUTTON", VK_MBUTTON},
        {"MMB", VK_MBUTTON},
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
        {"PAGEUP", VK_PRIOR},
        {"PRIOR", VK_PRIOR},
        {"PAGEDOWN", VK_NEXT},
        {"NEXT", VK_NEXT},
        {"END", VK_END},
        {"HOME", VK_HOME},
        {"LEFT", VK_LEFT},
        {"UP", VK_UP},
        {"RIGHT", VK_RIGHT},
        {"DOWN", VK_DOWN},
        {"SELECT", VK_SELECT},
        {"PRINT", VK_SNAPSHOT},
        {"PRINTSCREEN", VK_SNAPSHOT},
        {"SNAPSHOT", VK_SNAPSHOT},
        {"EXECUTE", VK_EXECUTE},
        {"INSERT", VK_INSERT},
        {"INS", VK_INSERT},
        {"DELETE", VK_DELETE},
        {"DEL", VK_DELETE},
        {"HELP", VK_HELP},
        {"0", '0'},
        {"1", '1'},
        {"2", '2'},
        {"3", '3'},
        {"4", '4'},
        {"5", '5'},
        {"6", '6'},
        {"7", '7'},
        {"8", '8'},
        {"9", '9'},
        {"A", 'A'},
        {"B", 'B'},
        {"C", 'C'},
        {"D", 'D'},
        {"E", 'E'},
        {"F", 'F'},
        {"G", 'G'},
        {"H", 'H'},
        {"I", 'I'},
        {"J", 'J'},
        {"K", 'K'},
        {"L", 'L'},
        {"M", 'M'},
        {"N", 'N'},
        {"O", 'O'},
        {"P", 'P'},
        {"Q", 'Q'},
        {"R", 'R'},
        {"S", 'S'},
        {"T", 'T'},
        {"U", 'U'},
        {"V", 'V'},
        {"W", 'W'},
        {"X", 'X'},
        {"Y", 'Y'},
        {"Z", 'Z'},
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
        {"SEPARATOR", VK_SEPARATOR},
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
        {"LMENU", VK_LMENU},
        {"RMENU", VK_RMENU},
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
        {"+", VK_OEM_PLUS},
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
        {"OEM_1", VK_OEM_1},
        {"OEM_PLUS", VK_OEM_PLUS},
        {"OEM_COMMA", VK_OEM_COMMA},
        {"OEM_MINUS", VK_OEM_MINUS},
        {"OEM_PERIOD", VK_OEM_PERIOD},
        {"OEM_2", VK_OEM_2},
        {"OEM_3", VK_OEM_3},
        {"OEM_4", VK_OEM_4},
        {"OEM_5", VK_OEM_5},
        {"OEM_6", VK_OEM_6},
        {"OEM_7", VK_OEM_7},
        {"OEM_8", VK_OEM_8},
        {"PROCESSKEY", VK_PROCESSKEY},
        {"PACKET", VK_PACKET},
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
    for (auto &c : str) { c = char(std::toupper(static_cast<unsigned char>(c))); }
    auto sl = util::splitString(str, '+');
    if (sl.empty()) {
        return 0;
    }

    int result = 0;
    for (const auto &s : sl) {
        auto ite = sVKeyMap.find(s);
        if (ite != sVKeyMap.end()) {
            if ((ite->second & input::KEY_MOD_MASK) == 0) {
                // Normal virtual key: allow one non-modifier key in a chord.
                if ((result & input::KEY_CODE_MASK) == 0) {
                    result |= ite->second;
                }
            } else {
                // Generic modifier flags can be combined with one normal virtual key.
                result |= ite->second;
            }
        }
    }
    return result;
}

Config gConfig;

void Config::loadDir(const wchar_t *dir) {
    std::error_code ec;
    std::filesystem::path path = er::gModulePath;
    for (const auto &entry: std::filesystem::directory_iterator(path / dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == L".ini") {
            loadSingleFile(entry.path().wstring(), entry.path().stem().string());
        }
    }
}

void Config::loadFile(const wchar_t *filename) {
    std::filesystem::path path = er::gModulePath;
    loadSingleFile((path / filename).wstring());
}

void Config::loadSingleFile(const std::wstring &filename, const std::string &modname) {
    auto *f = _wfopen(filename.c_str(), L"r");
    if (f == nullptr) {
        fwprintf(stderr, L"Unable to open %ls\n", filename.c_str());
        return;
    }
    struct ConfigData {
        std::map<std::string, std::string> &entries;
        const std::string &modname;
    } configData = {entries_, modname};
    util::parseIniFile(f, [](void *user, const char *section, const char *name, const char *value) {
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

int Config::getVirtualKey(const std::string &key, int defaultValue) const {
    const auto &s = get(key, "");
    if (s.empty()) return defaultValue;
    return mapStringToVirtualKey(s);
}

}
