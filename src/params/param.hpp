/*
 * Copyright (C) 2024,2025, Soar Qin<soarchin@gmail.com>

 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#pragma once

#include <cstdint>

namespace er::params {

#pragma pack(push, 8)

struct ParamEntryOffset {
    uint64_t paramId;
    intptr_t offset;
    uint64_t unk0;
};

struct ParamTable {
    uintptr_t vtable;
    uint16_t unk0;
    uint16_t count;
    uint16_t padding0[2];
    uintptr_t unk1[6];
    ParamEntryOffset entries[1];
};

#pragma pack(pop)

#define paramTableFindId(t, id, tp) { \
    const ::er::params::ParamTable *paramTable = (const ::er::params::ParamTable*)t; \
    uint16_t count = paramTable->count; \
    for (uint16_t i = 0; i < count; i++) { \
        const ::er::params::ParamEntryOffset *entry = &paramTable->entries[i]; \
        if (entry->paramId == id) { \
            return (tp*)((uintptr_t)t + entry->offset); \
        } \
    } \
}

#define paramTableIterateBegin(t, tp, var) do { \
    const ::er::params::ParamTable *paramTable = (const ::er::params::ParamTable*)t; \
    uint16_t count = paramTable->count; \
    for (uint16_t i = 0; i < count; i++) { \
        const ::er::params::ParamEntryOffset *entry = &paramTable->entries[i]; \
        tp *var = (tp*)((uintptr_t)t + entry->offset); \
        if (!var) { \
            continue; \
        }

#define paramTableIterateEnd() } } while(0)

}
