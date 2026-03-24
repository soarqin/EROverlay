#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace er::achievements {

constexpr int kUpdateTickMask = 0x1F; // update every 32 ticks (~60Hz / 32 ~ 2Hz)

struct CollectionFlag {
    uint32_t flagId = 0;
    uintptr_t offset = 0;
    uint8_t bits = 0;
};

struct Achievement {
    const char *name;
    const char *displayName;
    const char *description;

    std::vector<CollectionFlag> collectionFlags;
};

struct Locked {
    uint32_t index = 0;
    uint32_t flags = 0;
    uint32_t flagCount = 0;
};

struct UnlockNotification {
    uint32_t index = 0;
    std::chrono::time_point<std::chrono::steady_clock> deadline;
    float alpha = 0.0f;
};

class Data {
public:
    void load();
    void update();
    void unlock(const char *name);
    void updateUnlockingStatus();

    [[nodiscard]] const auto &achievements() const { return achievements_; }
    [[nodiscard]] const auto &locked() const { return locked_; }
    [[nodiscard]] const auto &unlocking() const { return unlocking_; }
    [[nodiscard]] std::mutex &mutex() { return mutex_; }

private:
    std::mutex mutex_;
    std::vector<Achievement> achievements_;
    std::unordered_map<std::string, uint32_t> indexMap_;
    std::vector<Locked> locked_;
    std::vector<Locked> lockedPrev_;
    std::vector<UnlockNotification> unlocking_;

    bool flagResolved_ = false;
};

extern Data gData;

}
