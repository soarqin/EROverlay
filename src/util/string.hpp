#pragma once

#include <algorithm>
#include <vector>
#include <string>
#include <sstream>

namespace er::util {

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
            auto val = std::stof(item);
            elems.push_back(std::clamp(val / 100.f, -0.999999f, 0.999999f));
            continue;
        }
        elems.push_back(std::stof(item));
    }
    return elems;
}

}
