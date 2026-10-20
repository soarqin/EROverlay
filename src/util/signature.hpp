//
// Created by soar on 2024/10/17.
//

#pragma once

#include <Pattern16.h>

#include "memory.hpp"

namespace er::util {

class Signature {
public:
    /**
     * \brief Constructs the signature with an IDA pattern
     * \param pattern The IDA pattern string
     */
    explicit Signature(const char *pattern) : pattern_(pattern) {}

    [[nodiscard]] std::size_t size() const { return Pattern16::Impl::processSignatureString(pattern_).first.size(); }

    /**
     * \brief Scans for the pattern in a memory region
     * \param region The region to search in, default is the main module
     * \return MemoryHandle
     */
    [[nodiscard]] MemoryHandle scan(MemoryRegion region = Module(nullptr)) const { return Pattern16::scan(region.base().as<void *>(), region.size(), pattern_); }

    /** Returns null if the region contains zero or multiple matches. */
    [[nodiscard]] MemoryHandle scanUnique(MemoryRegion region = Module(nullptr)) const {
        auto hit = scan(region);
        if (!hit)
            return nullptr;
        auto next = hit.add(1);
        // Include overlapping matches when checking for ambiguity.
        if (scan(MemoryRegion(next, region.end().as<std::uintptr_t>() - next.as<std::uintptr_t>())))
            return nullptr;
        return hit;
    }

    /** Checks the pattern at the beginning of the supplied region. */
    [[nodiscard]] bool matches(MemoryRegion region) const {
        auto length = size();
        if (!length || length > region.size())
            return false;
        auto hit = scan(MemoryRegion(region.base(), length));
        return hit && hit.as<std::uintptr_t>() == region.base().as<std::uintptr_t>();
    }

private:
    const char *pattern_;
};

} // namespace er::util
