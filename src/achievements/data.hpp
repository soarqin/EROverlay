#pragma once

#include <mutex>
#include <vector>
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
    void update();

    [[nodiscard]] inline const std::vector<Achievement> &achievements() const { return achievements_; }
    [[nodiscard]] inline const std::vector<uint32_t> &locked() const { return locked_; }
    [[nodiscard]] inline std::mutex &mutex() { return mutex_; }

private:
    void *stats_;

    std::mutex mutex_;
    std::vector<Achievement> achievements_;
    std::vector<uint32_t> locked_;
    std::vector<uint32_t> lockedPrev_;
};

extern Data gData;

}
