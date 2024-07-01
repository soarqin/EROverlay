#include "string.hpp"

namespace er::util {

std::vector<std::string> splitString(const std::string &str, char c) {
    typename std::string::size_type pos = 0;
    std::vector<std::string> result;
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

}
