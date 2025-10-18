/*
 * Copyright (C) 2024,2025, Soar Qin<soarchin@gmail.com>

 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include "pointers.hpp"

#include "util/signature.hpp"

namespace er::params {

Pointers gPointers;

static uintptr_t scanIndirectPointer(const char *pattern, size_t offset) {
    util::Signature sig(pattern);
    if (auto res = sig.scan()) {
        auto addr = res.add(res.add(offset).as<std::uint32_t &>() + offset + 4);
        return addr.as<uintptr_t>();
    }
    return 0;
}

#define initIndirectPointer(name, pattern, offset) do { \
    if (name == 0) { \
        name = scanIndirectPointer(pattern, offset); \
        if (name == 0) break; \
    } \
} while (0)

void Pointers::init() {
    if (initialized_) return;
    initIndirectPointer(csRegulationManager, "48 8B 0D ?? ?? ?? ?? 48 85 C9 74 0B 4C 8B C0 48 8B D7", 3);
    initialized_ = true;
}

}
