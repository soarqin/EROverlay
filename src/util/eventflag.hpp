#pragma once

#include <cstdint>

#include "memory.hpp"

namespace er::util {

// CSEventFlagManImp storage, verified against the native reader and writer.
// Type 1 indexes the packed buffer; type 2 owns a direct category buffer.
[[nodiscard]] inline uintptr_t resolveEventFlag(uintptr_t manager, uint32_t flagId, uint8_t *bits) {
    if (bits == nullptr)
        return 0;
    *bits = 0;
    if (manager == 0 || flagId == 0)
        return 0;
    auto divisor = MemoryHandle(manager + 0x1C).as<uint32_t &>();
    if (divisor == 0)
        return 0;
    auto category = flagId / divisor;
    auto remainder = flagId % divisor;
    auto sentinel = MemoryHandle(manager + 0x38).as<uintptr_t &>();
    if (sentinel == 0)
        return 0;
    auto candidate = sentinel;
    auto node = MemoryHandle(sentinel + 0x08).as<uintptr_t &>();
    unsigned depth = 0;
    while (node != 0 && MemoryHandle(node + 0x19).as<uint8_t &>() == 0) {
        if (++depth > 64)
            return 0;
        if (MemoryHandle(node + 0x20).as<uint32_t &>() < category) {
            node = MemoryHandle(node + 0x10).as<uintptr_t &>();
        } else {
            candidate = node;
            node = MemoryHandle(node).as<uintptr_t &>();
        }
    }
    if (node == 0 || candidate == sentinel || MemoryHandle(candidate + 0x20).as<uint32_t &>() != category)
        return 0;
    uintptr_t buffer = 0;
    switch (MemoryHandle(candidate + 0x28).as<uint32_t &>()) {
        case 1: {
            auto base = MemoryHandle(manager + 0x28).as<uintptr_t &>();
            if (base == 0)
                return 0;
            auto stride = MemoryHandle(manager + 0x20).as<uint32_t &>();
            auto index = MemoryHandle(candidate + 0x30).as<uint32_t &>();
            if (stride == 0 || stride < (divisor + uint64_t{7}) / 8)
                return 0;
            // The native storage index multiplication is 32-bit.
            buffer = base + static_cast<uint32_t>(stride * index);
            break;
        }
        case 2:
            buffer = MemoryHandle(candidate + 0x30).as<uintptr_t &>();
            break;
        default:
            return 0;
    }
    if (buffer == 0)
        return 0;
    *bits = static_cast<uint8_t>(1u << (7u - (remainder & 7u)));
    return buffer + (remainder >> 3u);
}

} // namespace er::util
