#pragma once

#include <atomic>
#include <cstdio>
#include <mutex>

namespace er::util {
inline std::atomic<FILE *> nativeLogFile{nullptr};
inline std::mutex nativeLogMutex;

[[nodiscard]] inline bool nativeLogEnabled() { return nativeLogFile.load(std::memory_order_relaxed) != nullptr; }

template<typename... T>
void nativeLog(const char *format, T... values) {
    if (!nativeLogEnabled())
        return;
    std::lock_guard lock(nativeLogMutex);
    if (auto *file = nativeLogFile.load(std::memory_order_relaxed))
        std::fprintf(file, format, values...);
}
inline void flushNativeLog() {
    if (!nativeLogEnabled())
        return;
    std::lock_guard lock(nativeLogMutex);
    if (auto *file = nativeLogFile.load(std::memory_order_relaxed))
        std::fflush(file);
}
inline void closeNativeLog() {
    std::lock_guard lock(nativeLogMutex);
    if (auto *file = nativeLogFile.exchange(nullptr, std::memory_order_relaxed))
        std::fclose(file);
}
} // namespace er::util
