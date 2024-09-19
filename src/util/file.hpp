#pragma once

#include <vector>
#include <string>
#include <cstddef>

namespace er::util {

void *getFileContent(const std::wstring &path, size_t &size, void*(*alloc)(size_t) = nullptr);

}
