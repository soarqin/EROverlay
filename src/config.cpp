#include "config.hpp"

#include "ini.h"
#include <windows.h>
#include <cwchar>

namespace er {

Config gConfig;

void Config::load(const std::wstring &filename) {
    auto *f = _wfopen(filename.c_str(), L"r");
    if (f == nullptr) {
        fwprintf(stderr, L"Unable to open %ls\n", filename.c_str());
        return;
    }
    ini_parse_file(f, [](void *user, const char *section, const char *name, const char *value) {
        auto &entries = *static_cast<std::map<std::string, std::string> *>(user);
        entries[section + std::string(".") + name] = value;
        return 1;
    }, &entries_);
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

}
