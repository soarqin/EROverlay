#pragma once

#include <vector>
#include <string>

namespace er::util {

template<typename T>
std::vector<T> splitString(const T &str, typename T::value_type c) {
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
int replaceAll(T &str, const T &from, const T &to) {
    int count = 0;
    for (size_t pos = 0; (pos = str.find(from, pos)) != T::npos; pos += to.size()) {
        str.replace(pos, from.size(), to);
        ++count;
    }
    return count;
}

}
