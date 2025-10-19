#pragma once

#include <algorithm>
#include <vector>
#include <string>
#include <sstream>
#include <cctype>

namespace er::util {

template<typename T>
inline void toLower(T &str) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
}

template<typename T>
inline void toUpper(T &str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
}

template<typename T>
inline void trimString(T &str) {
    auto pos = str.find_last_not_of(" \f\n\r\t\v");
    if (pos != T::npos) str.erase(pos + 1);
    auto pos2 = str.find_first_not_of(" \f\n\r\t\v");
    if (pos2 != T::npos) str.erase(0, pos2);
}

template<typename T>
inline std::vector<T> splitString(const T &str, typename T::value_type c) {
    typename std::string::size_type pos = 0;
    std::vector<T> result;
    while (true) {
        auto epos = str.find(c, pos);
        result.emplace_back(str.substr(pos, epos - pos));
        if (epos == std::string::npos) {
            break;
        }
        pos = epos + 1;
    }
    return result;
}

template<typename T>
inline int replaceAll(T &str, const T &from, const T &to) {
    int count = 0;
    for (size_t pos = 0; (pos = str.find(from, pos)) != T::npos; pos += to.size()) {
        str.replace(pos, from.size(), to);
        ++count;
    }
    return count;
}

inline std::vector<float> strSplitToFloatVec(const std::string &s) {
    std::vector<float> elems;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.empty()) {
            elems.push_back(0);
            continue;
        }
        if (item.back() == '%') {
            item.pop_back();
            elems.push_back(std::clamp(std::stof(item) / 100.f, -1.f, 1.f));
            continue;
        }
        elems.push_back(std::stof(item));
    }
    return elems;
}

inline std::vector<float> strSplitToFloatVec(const std::wstring &s) {
    return strSplitToFloatVec(std::string(s.begin(), s.end()));
}

inline float strToFloat(const std::string &s) {
    if (s.back() == '%') {
        return std::stof(s.substr(0, s.size() - 1)) / 100.f;
    }
    return std::stof(s);
}

}
