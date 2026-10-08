#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <limits>

#include "gameflags.hpp"
#include "paramreader.hpp"

namespace er::util {
namespace {
template<typename T>
bool read(uintptr_t address, T &value) {
    SIZE_T copied = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address), &value, sizeof(value), &copied) && copied == sizeof(value);
}

// EventFlagCache re-resolves category storage after this interval.
constexpr uint64_t CATEGORY_REFRESH_MS = 1000;

// Finds the flag bytes for a category; address 0 is a readable category
// without storage, whose flags are false. Returns false when unreadable.
bool categoryStorage(uintptr_t manager, uint32_t category, uintptr_t &address) {
    address = 0;
    uintptr_t sentinel = 0, node = 0;
    if (!read(manager + 0x38, sentinel) || !sentinel || !read(sentinel + 8, node))
        return false;
    uintptr_t candidate = sentinel;
    bool terminal = false;
    for (unsigned depth = 0; depth < 64; ++depth) {
        uint8_t nil = 0;
        uint32_t key;
        if (!node || !read(node + 0x19, nil))
            return false;
        if (nil) {
            terminal = true;
            break;
        }
        if (!read(node + 0x20, key))
            return false;
        if (key >= category) {
            candidate = node;
            if (!read(node, node))
                return false;
        } else if (!read(node + 0x10, node))
            return false;
    }
    if (!terminal)
        return false;
    if (candidate == sentinel)
        return true;
    uint32_t key = 0, type = 0;
    if (!read(candidate + 0x20, key))
        return false;
    if (key != category)
        return true;
    if (!read(candidate + 0x28, type))
        return false;
    // RVA 0x5FA250: type 1 is an index into the packed allocation; type 2
    // is a direct pointer. Other types have no readable flag storage.
    if (type == 1) {
        uint32_t stride = 0, index = 0;
        uintptr_t flags = 0;
        if (!read(manager + 0x20, stride) || !stride || !read(candidate + 0x30, index) || !read(manager + 0x28, flags) || !flags)
            return false;
        uint64_t offset = uint64_t(stride) * index;
        if (offset > UINT32_MAX || offset > std::numeric_limits<uintptr_t>::max() - flags)
            return false;
        address = flags + static_cast<uintptr_t>(offset);
    } else if (type == 2) {
        if (!read(candidate + 0x30, address))
            return false;
    } else
        return true;
    return address != 0;
}

bool readStorageBit(uintptr_t address, uint32_t remainder, bool &value) {
    if ((remainder >> 3) > std::numeric_limits<uintptr_t>::max() - address)
        return false;
    uint8_t bits = 0;
    if (!read(address + (remainder >> 3), bits))
        return false;
    value = (bits & (1u << (7 - (remainder & 7)))) != 0;
    return true;
}
} // namespace

bool readGameEventFlag(uintptr_t manager, uint32_t id, bool &value) {
    value = false;
    if (!id)
        return true;
    uint32_t divisor = 0;
    uintptr_t address = 0;
    if (!manager || !read(manager + 0x1C, divisor) || !divisor || !categoryStorage(manager, id / divisor, address))
        return false;
    return !address || readStorageBit(address, id % divisor, value);
}

bool EventFlagCache::read(uintptr_t manager, uint32_t id, bool &value) {
    value = false;
    if (!id)
        return true;
    uint64_t now = GetTickCount64();
    if (manager != manager_ || now >= expiry_) {
        // Bound how long a rebuilt tree or a newly allocated category can be
        // missed; callers also reset() when the game context changes.
        reset();
        uint32_t divisor = 0;
        if (!manager || !util::read(manager + 0x1C, divisor) || !divisor)
            return false;
        manager_ = manager;
        divisor_ = divisor;
        expiry_ = now + CATEGORY_REFRESH_MS;
    }
    auto [entry, inserted] = storage_.try_emplace(id / divisor_, 0);
    if (inserted && !categoryStorage(manager, entry->first, entry->second)) {
        storage_.erase(entry);
        return false;
    }
    if (!entry->second || readStorageBit(entry->second, id % divisor_, value))
        return true;
    // Unreadable storage may belong to a rebuilt tree; resolve it again next time.
    storage_.erase(entry);
    return false;
}

void EventFlagCache::reset() {
    manager_ = 0;
    divisor_ = 0;
    expiry_ = 0;
    storage_.clear();
}

bool readMapPieceMasks(uintptr_t table, uintptr_t manager, bool reveal, uint32_t (&masks)[3], EventFlagCache *cache) {
    std::memset(masks, 0, sizeof(masks));
    if (reveal) {
        for (auto &mask: masks)
            mask = UINT32_MAX;
        return true;
    }
    ParamRows rows;
    if (!manager || !rows.open(table) || rows.rows().size() > 8192)
        return false;
    uint32_t selected[3]{};
    for (const auto &entry: rows.rows()) {
        // RVA 0x8892C0 queries exactly 32 IDs per map: 0..31, 100..131,
        // and 1000..1031. Rows such as DLC 1070 are separate reveal areas.
        uint64_t map = entry.id / 100, bit = entry.id % 100;
        if (bit >= 32 || (map != 0 && map != 1 && map != 10))
            continue;
        int32_t event = 0;
        if (!rows.field(entry, 4, event))
            return false;
        bool unlocked = false;
        uint32_t id = event == -1 ? 0u : static_cast<uint32_t>(event);
        if (!(cache ? cache->read(manager, id, unlocked) : readGameEventFlag(manager, id, unlocked)))
            return false;
        if (unlocked)
            selected[map == 10 ? 2 : map] |= 1u << bit;
    }
    std::memcpy(masks, selected, sizeof(masks));
    return true;
}

} // namespace er::util
