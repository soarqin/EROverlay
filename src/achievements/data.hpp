#pragma once

#include <chrono>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <tuple>
#include <string>
#include <cstdint>

namespace er::achievements {

struct Achievement {
    const char *name;
    const char *displayName;
    const char *description;
};

class Data {
public:
    void load();
    void unlock(const char *name);
    void updateUnlockingStatus();

    [[nodiscard]] inline const auto &achievements() const { return achievements_; }
    [[nodiscard]] inline const auto &locked() const { return locked_; }
    [[nodiscard]] inline const auto &unlocking() const { return unlocking_; }
    [[nodiscard]] inline std::mutex &mutex() { return mutex_; }

private:
    void *stats_;

    std::mutex mutex_;
    std::vector<Achievement> achievements_;
    std::unordered_map<std::string, uint32_t> indexMap_;
    std::vector<uint32_t> locked_;
    std::vector<uint32_t> lockedPrev_;
    std::vector<std::tuple<uint32_t, std::chrono::time_point<std::chrono::steady_clock>, float>> unlocking_;
};

extern Data gData;

}
