#pragma once

#include <cstdio>
#include <mutex>

namespace er::util {
inline FILE *nativeLogFile = nullptr;
inline std::mutex nativeLogMutex;

template<typename... T>
void nativeLog(const char *format, T... values) {
    std::lock_guard lock(nativeLogMutex);
    if (!nativeLogFile)
        return;
    std::fprintf(nativeLogFile, format, values...);
    std::fflush(nativeLogFile);
}
} // namespace er::util
