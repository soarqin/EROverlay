#include "file.hpp"

#include <fstream>

namespace er::util {

void *getFileContent(const std::wstring &path, size_t &size, void *(*alloc)(size_t)) {
    size = 0;
    std::ifstream ifs(path.c_str(), std::ios::binary | std::ios::ate);
    if (!ifs) {
        return nullptr;
    }
    // Empty, unreadable or unsized files have no content to hand out.
    auto length = ifs.tellg();
    if (length <= 0) {
        return nullptr;
    }
    ifs.seekg(0, std::ios::beg);
    auto *buf = alloc ? alloc(static_cast<size_t>(length)) : new char[static_cast<size_t>(length)];
    if (!buf) {
        return nullptr;
    }
    ifs.read(reinterpret_cast<char *>(buf), static_cast<std::streamsize>(length));
    size = static_cast<size_t>(ifs.gcount());
    return buf;
}

}
