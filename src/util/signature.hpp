//
// Created by soar on 2024/10/17.
//

#pragma once

#include "memory.hpp"
#include <Pattern16.h>

namespace er::util {

class Signature {
public:
    /**
     * \brief Constructs the signature with an IDA pattern
     * \param pattern The IDA pattern string
     */
    explicit Signature(const char *pattern) : pattern_(pattern) {
    }

    /**
     * \brief Scans for the pattern in a memory region
     * \param region The region to search in, default is the main module
     * \return MemoryHandle
     */
    MemoryHandle scan(MemoryRegion region = Module(nullptr)) {
        return Pattern16::scan(region.base().as<void *>(), region.size(), pattern_);
    }
private:
    const char *pattern_;
};

}