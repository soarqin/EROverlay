#include "config.hpp"

#include "global.hpp"
#include "util/string.hpp"

#include <imgui.h>
#include <ini.h>

#include <filesystem>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>

namespace er {

int mapStringToImGuiKey(const std::string &name) {
    static const std::map<std::string, int> sVKeyMap = {
        {"WIN", ImGuiMod_Super},
        {"LWIN", ImGuiKey_LeftSuper},
        {"RWIN", ImGuiKey_RightSuper},
        {"ALT", ImGuiMod_Alt},
        {"LALT", ImGuiKey_LeftAlt},
        {"RALT", ImGuiKey_RightAlt},
        {"CTRL", ImGuiMod_Ctrl},
        {"LCTRL", ImGuiKey_LeftCtrl},
        {"RCTRL", ImGuiKey_RightCtrl},
        {"SHIFT", ImGuiMod_Shift},
        {"LSHIFT", ImGuiKey_LeftShift},
        {"RSHIFT", ImGuiKey_RightShift},
        // {"LBUTTON", VK_LBUTTON}, // Not available in ImGuiKey_
        // {"RBUTTON", VK_RBUTTON}, // Not available in ImGuiKey_
        // {"CANCEL", VK_CANCEL}, // Not available in ImGuiKey_
        // {"MBUTTON", VK_MBUTTON}, // Not available in ImGuiKey_
        // {"XBUTTON1", VK_XBUTTON1}, // Not available in ImGuiKey_
        // {"XBUTTON2", VK_XBUTTON2}, // Not available in ImGuiKey_
        {"BACK", ImGuiKey_Backspace},
        {"BACKSPACE", ImGuiKey_Backspace},
        {"TAB", ImGuiKey_Tab},
        // {"CLEAR", VK_CLEAR},
        {"RETURN", ImGuiKey_Enter},
        {"ENTER", ImGuiKey_Enter},
        {"PAUSE", ImGuiKey_Pause},
        {"CAPITAL", ImGuiKey_CapsLock},
        {"CAPSLOCK", ImGuiKey_CapsLock},
        // {"KANA", VK_KANA}, // Not available in ImGuiKey_
        // {"HANGUL", VK_HANGUL}, // Not available in ImGuiKey_
        // {"JUNJA", VK_JUNJA}, // Not available in ImGuiKey_
        // {"FINAL", VK_FINAL}, // Not available in ImGuiKey_
        // {"HANJA", VK_HANJA}, // Not available in ImGuiKey_
        // {"KANJI", VK_KANJI}, // Not available in ImGuiKey_
        {"ESCAPE", ImGuiKey_Escape},
        {"ESC", ImGuiKey_Escape},
        // {"CONVERT", VK_CONVERT}, // Not available in ImGuiKey_
        // {"NONCONVERT", VK_NONCONVERT}, // Not available in ImGuiKey_
        // {"ACCEPT", VK_ACCEPT}, // Not available in ImGuiKey_
        // {"MODECHANGE", VK_MODECHANGE}, // Not available in ImGuiKey_
        {"SPACE", ImGuiKey_Space},
        {"PAGEUP", ImGuiKey_PageUp},
        {"PAGEDOWN", ImGuiKey_PageDown},
        {"END", ImGuiKey_End},
        {"HOME", ImGuiKey_Home},
        {"LEFT", ImGuiKey_LeftArrow},
        {"UP", ImGuiKey_UpArrow},
        {"RIGHT", ImGuiKey_RightArrow},
        {"DOWN", ImGuiKey_DownArrow},
        // {"SELECT", VK_SELECT}, // Not available in ImGuiKey_
        {"PRINT", ImGuiKey_PrintScreen},
        // {"EXECUTE", VK_EXECUTE}, // Not available in ImGuiKey_
        {"SNAPSHOT", ImGuiKey_PrintScreen},
        {"INSERT", ImGuiKey_Insert},
        {"DELETE", ImGuiKey_Delete},
        // {"HELP", VK_HELP}, // Not available in ImGuiKey_
        {"0", ImGuiKey_0},
        {"1", ImGuiKey_1},
        {"2", ImGuiKey_2},
        {"3", ImGuiKey_3},
        {"4", ImGuiKey_4},
        {"5", ImGuiKey_5},
        {"6", ImGuiKey_6},
        {"7", ImGuiKey_7},
        {"8", ImGuiKey_8},
        {"9", ImGuiKey_9},
        {"A", ImGuiKey_A},
        {"B", ImGuiKey_B},
        {"C", ImGuiKey_C},
        {"D", ImGuiKey_D},
        {"E", ImGuiKey_E},
        {"F", ImGuiKey_F},
        {"G", ImGuiKey_G},
        {"H", ImGuiKey_H},
        {"I", ImGuiKey_I},
        {"J", ImGuiKey_J},
        {"K", ImGuiKey_K},
        {"L", ImGuiKey_L},
        {"M", ImGuiKey_M},
        {"N", ImGuiKey_N},
        {"O", ImGuiKey_O},
        {"P", ImGuiKey_P},
        {"Q", ImGuiKey_Q},
        {"R", ImGuiKey_R},
        {"S", ImGuiKey_S},
        {"T", ImGuiKey_T},
        {"U", ImGuiKey_U},
        {"V", ImGuiKey_V},
        {"W", ImGuiKey_W},
        {"X", ImGuiKey_X},
        {"Y", ImGuiKey_Y},
        {"Z", ImGuiKey_Z},
        {"APPS", ImGuiKey_Menu},
        // {"SLEEP", VK_SLEEP}, // Not available in ImGuiKey_
        {"NUMPAD0", ImGuiKey_Keypad0},
        {"NUMPAD1", ImGuiKey_Keypad1},
        {"NUMPAD2", ImGuiKey_Keypad2},
        {"NUMPAD3", ImGuiKey_Keypad3},
        {"NUMPAD4", ImGuiKey_Keypad4},
        {"NUMPAD5", ImGuiKey_Keypad5},
        {"NUMPAD6", ImGuiKey_Keypad6},
        {"NUMPAD7", ImGuiKey_Keypad7},
        {"NUMPAD8", ImGuiKey_Keypad8},
        {"NUMPAD9", ImGuiKey_Keypad9},
        {"NUM0", ImGuiKey_Keypad0},
        {"NUM1", ImGuiKey_Keypad1},
        {"NUM2", ImGuiKey_Keypad2},
        {"NUM3", ImGuiKey_Keypad3},
        {"NUM4", ImGuiKey_Keypad4},
        {"NUM5", ImGuiKey_Keypad5},
        {"NUM6", ImGuiKey_Keypad6},
        {"NUM7", ImGuiKey_Keypad7},
        {"NUM8", ImGuiKey_Keypad8},
        {"NUM9", ImGuiKey_Keypad9},
        {"MULTIPLY", ImGuiKey_KeypadMultiply},
        {"ADD", ImGuiKey_KeypadAdd},
        {"SUBTRACT", ImGuiKey_KeypadSubtract},
        {"MINUS", ImGuiKey_KeypadSubtract},
        {"DECIMAL", ImGuiKey_KeypadDecimal},
        {"DIVIDE", ImGuiKey_KeypadDivide},
        {"F1", ImGuiKey_F1},
        {"F2", ImGuiKey_F2},
        {"F3", ImGuiKey_F3},
        {"F4", ImGuiKey_F4},
        {"F5", ImGuiKey_F5},
        {"F6", ImGuiKey_F6},
        {"F7", ImGuiKey_F7},
        {"F8", ImGuiKey_F8},
        {"F9", ImGuiKey_F9},
        {"F10", ImGuiKey_F10},
        {"F11", ImGuiKey_F11},
        {"F12", ImGuiKey_F12},
        {"F13", ImGuiKey_F13},
        {"F14", ImGuiKey_F14},
        {"F15", ImGuiKey_F15},
        {"F16", ImGuiKey_F16},
        {"F17", ImGuiKey_F17},
        {"F18", ImGuiKey_F18},
        {"F19", ImGuiKey_F19},
        {"F20", ImGuiKey_F20},
        {"F21", ImGuiKey_F21},
        {"F22", ImGuiKey_F22},
        {"F23", ImGuiKey_F23},
        {"F24", ImGuiKey_F24},
        {"NUMLOCK", ImGuiKey_NumLock},
        {"SCROLL", ImGuiKey_ScrollLock},
        {"BROWSER_BACK", ImGuiKey_AppBack},
        {"BROWSER_FORWARD", ImGuiKey_AppForward},
        // {"BROWSER_REFRESH", VK_BROWSER_REFRESH}, // Not available in ImGuiKey_
        // {"BROWSER_STOP", VK_BROWSER_STOP}, // Not available in ImGuiKey_
        // {"BROWSER_SEARCH", VK_BROWSER_SEARCH}, // Not available in ImGuiKey_
        // {"BROWSER_FAVORITES", VK_BROWSER_FAVORITES}, // Not available in ImGuiKey_
        // {"BROWSER_HOME", VK_BROWSER_HOME}, // Not available in ImGuiKey_
        // {"VOLUME_MUTE", VK_VOLUME_MUTE}, // Not available in ImGuiKey_
        // {"VOLUME_DOWN", VK_VOLUME_DOWN}, // Not available in ImGuiKey_
        // {"VOLUME_UP", VK_VOLUME_UP}, // Not available in ImGuiKey_
        // {"MEDIA_NEXT_TRACK", VK_MEDIA_NEXT_TRACK}, // Not available in ImGuiKey_
        // {"MEDIA_PREV_TRACK", VK_MEDIA_PREV_TRACK}, // Not available in ImGuiKey_
        // {"MEDIA_STOP", VK_MEDIA_STOP}, // Not available in ImGuiKey_
        // {"MEDIA_PLAY_PAUSE", VK_MEDIA_PLAY_PAUSE}, // Not available in ImGuiKey_
        // {"LAUNCH_MAIL", VK_LAUNCH_MAIL}, // Not available in ImGuiKey_
        // {"LAUNCH_MEDIA_SELECT", VK_LAUNCH_MEDIA_SELECT}, // Not available in ImGuiKey_
        // {"LAUNCH_APP1", VK_LAUNCH_APP1}, // Not available in ImGuiKey_
        // {"LAUNCH_APP2", VK_LAUNCH_APP2}, // Not available in ImGuiKey_
        {"=", ImGuiKey_Equal},
        {",", ImGuiKey_Comma},
        {"-", ImGuiKey_Minus},
        {".", ImGuiKey_Period},
        {";", ImGuiKey_Semicolon},
        {"/", ImGuiKey_Slash},
        {"~", ImGuiKey_GraveAccent},
        {"`", ImGuiKey_GraveAccent},
        {"[", ImGuiKey_LeftBracket},
        {"\\", ImGuiKey_Backslash},
        {"]", ImGuiKey_RightBracket},
        {"'", ImGuiKey_Apostrophe},
        // {"PROCESSKEY", VK_PROCESSKEY}, // Not available in ImGuiKey_
        // {"ATTN", VK_ATTN}, // Not available in ImGuiKey_
        // {"CRSEL", VK_CRSEL}, // Not available in ImGuiKey_
        // {"EXSEL", VK_EXSEL}, // Not available in ImGuiKey_
        // {"EREOF", VK_EREOF}, // Not available in ImGuiKey_
        // {"PLAY", VK_PLAY}, // Not available in ImGuiKey_
        // {"ZOOM", VK_ZOOM}, // Not available in ImGuiKey_
        // {"PA1", VK_PA1}, // Not available in ImGuiKey_
        // {"OEM_CLEAR", VK_OEM_CLEAR}, // Not available in ImGuiKey_
    };

    std::string str = name;
    for (auto &c: str) { c = char(std::toupper(c)); }
    auto sl = util::splitString(str, '+');
    if (sl.empty()) {
        return 0;
    }
    // For key combinations, we'll return the first key
    int result = 0; 
    for (const auto &s: sl) {
        auto ite = sVKeyMap.find(s);
        if (ite != sVKeyMap.end()) {
            if (ite->second & 0x0FFF) {
                // Is normal key, we only allow one normal key in the combination
                if (!(result & 0x0FFF)) {
                    result = result | ite->second;
                }
            } else {
                // Is modifier key, we can have multiple modifier keys in the combination
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
        fflush(stderr);
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

int Config::getImGuiKey(const std::string &key, int defaultValue) const {
    const auto &s = get(key, "");
    if (s.empty()) return defaultValue;
    return mapStringToImGuiKey(s);
}

}
