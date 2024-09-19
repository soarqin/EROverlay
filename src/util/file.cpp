#include "file.hpp"

#include <fstream>

namespace er::util {

void *getFileContent(const std::wstring &path, size_t &size, void *(*alloc)(size_t)) {
    std::ifstream ifs(path.c_str(), std::ios::binary);
    if (!ifs) {
        size = 0;
        return nullptr;
    }
    ifs.seekg(0, std::ios::end);
    size = static_cast<size_t>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);
    auto *buf = alloc ? alloc(size) : new char[size];
    ifs.read(reinterpret_cast<char *>(buf), static_cast<std::streamsize>(size));
    return buf;
}

}
