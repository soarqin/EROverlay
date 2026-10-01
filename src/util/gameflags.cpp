#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <limits>

#include "gameflags.hpp"
#include "params/param.hpp"

namespace er::util {
namespace {
template<typename T>
bool read(uintptr_t address, T &value) {
    SIZE_T copied = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address), &value, sizeof(value), &copied) && copied == sizeof(value);
}
} // namespace

bool readGameEventFlag(uintptr_t manager, uint32_t id, bool &value) {
    value = false;
    if (!id)
        return true;
    uint32_t divisor = 0;
    uintptr_t sentinel = 0, node = 0;
    if (!manager || !read(manager + 0x1C, divisor) || !divisor || !read(manager + 0x38, sentinel) || !sentinel || !read(sentinel + 8, node))
        return false;
    uint32_t category = id / divisor, remainder = id % divisor;
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
    uintptr_t address = 0;
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
    if (!address || (remainder >> 3) > std::numeric_limits<uintptr_t>::max() - address)
        return false;
    uint8_t bits = 0;
    if (!read(address + (remainder >> 3), bits))
        return false;
    value = (bits & (1u << (7 - (remainder & 7)))) != 0;
    return true;
}

bool readMapPieceMasks(uintptr_t table, uintptr_t manager, bool reveal, uint32_t (&masks)[3]) {
    std::memset(masks, 0, sizeof(masks));
    if (reveal) {
        for (auto &mask: masks)
            mask = UINT32_MAX;
        return true;
    }
    uint16_t count = 0;
    uint8_t flags[2]{};
    // The installed sample uses PARAM's 24-byte row entries. Reject other
    // layouts rather than interpreting unrelated bytes as event flag IDs.
    if (!table || !manager || !read(table + 0xA, count) || !count || count > 8192 || !read(table + 0x2D, flags) ||
        !((flags[0] & 0x7F) == 4 || ((flags[0] & 0x7F) == 5 && (flags[0] & 0x80))) || !(flags[1] & 2))
        return false;
    uint32_t selected[3]{};
    for (uint16_t i = 0; i < count; ++i) {
        params::ParamEntryOffset entry;
        if (!read(table + 0x40 + size_t(i) * sizeof(entry), entry))
            return false;
        // RVA 0x8892C0 queries exactly 32 IDs per map: 0..31, 100..131,
        // and 1000..1031. Rows such as DLC 1070 are separate reveal areas.
        uint64_t map = entry.paramId / 100, bit = entry.paramId % 100;
        if (bit >= 32 || (map != 0 && map != 1 && map != 10))
            continue;
        int32_t event = 0;
        if (entry.offset <= 0 || entry.offset > 0x10000000 || uintptr_t(entry.offset) > std::numeric_limits<uintptr_t>::max() - table - 4 || !read(table + entry.offset + 4, event))
            return false;
        bool unlocked = false;
        if (!readGameEventFlag(manager, event == -1 ? 0u : static_cast<uint32_t>(event), unlocked))
            return false;
        if (unlocked)
            selected[map == 10 ? 2 : map] |= 1u << bit;
    }
    std::memcpy(masks, selected, sizeof(masks));
    return true;
}

} // namespace er::util
