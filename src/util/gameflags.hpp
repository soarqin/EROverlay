#pragma once

#include <cstdint>
#include <unordered_map>

namespace er::util {

// Resolves each event-flag category's storage once per manager and refresh
// window, so a flag read costs one memory copy instead of a tree walk. Flag
// values themselves are always read live. Not thread-safe.
class EventFlagCache {
public:
    [[nodiscard]] bool read(uintptr_t manager, uint32_t id, bool &value);
    void reset();

private:
    uintptr_t manager_ = 0;
    uint32_t divisor_ = 0;
    uint64_t expiry_ = 0;
    std::unordered_map<uint32_t, uintptr_t> storage_; // 0: category without storage.
};

// These readers inspect published CPU data only. Failure is distinct from a
// readable flag whose value is false, so unavailable progress cannot select
// the unexplored map variant.
[[nodiscard]] bool readGameEventFlag(uintptr_t manager, uint32_t id, bool &value);
[[nodiscard]] bool readMapPieceMasks(uintptr_t table, uintptr_t manager, bool reveal, uint32_t (&masks)[3], EventFlagCache *cache = nullptr);

} // namespace er::util
