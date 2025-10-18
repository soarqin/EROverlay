/*
 * Copyright (C) 2024,2025, Soar Qin<soarchin@gmail.com>

 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include "paraminternal.hpp"
#include "wstring.hpp"
#include "pointers.hpp"

#include <windows.h>

#include <unordered_map>
#include <string>

namespace er::params {

#pragma pack(push, 8)

struct ParamData {
    uintptr_t vtable[2];
    WstringImpl name;
    uintptr_t unk1[9];
    struct {
        uintptr_t unk0[16];
        ParamTable *data;
    } *path;
};

struct CSRegulationManager {
    uintptr_t vtable[3];
    ParamData **start;
    ParamData **end;
    uintptr_t unk0;
    uintptr_t unk1;
    uintptr_t unk2;
    int loaded;
};

#pragma pack(pop)

struct ParamType {
    const wchar_t *name;
    const ParamTable *param;
};

static std::unordered_map<std::wstring, ParamType> paramTypes;

bool paramLoadTable() {
    paramTypes.clear();
    gPointers.init();
    if (!gPointers.csRegulationManager) {
        return false;
    }
    const CSRegulationManager *regMan;
    while (1) {
        regMan = (const CSRegulationManager*)*(uintptr_t*)gPointers.csRegulationManager;
        if (regMan != nullptr && regMan->start != nullptr && regMan->end != nullptr && regMan->loaded) break;
        Sleep(100);
    }
    // fwprintf(stderr, L"%p %p %zd\n", regMan->start, regMan->end, regMan->end - regMan->start);
    for (ParamData **current = regMan->start; current < regMan->end; current++) {
        const ParamData *param = *current;
        const auto *name = wstringImplStr(&param->name);
        paramTypes.emplace(name, ParamType { name, param->path->data });
    }
    return true;
}

void paramUnload() {
    paramTypes.clear();
}

const ParamTable *paramFindTable(const wchar_t *name) {
    if (paramTypes.empty()) {
        if (!paramLoadTable()) return nullptr;
    }
    auto it = paramTypes.find(name);
    if (it == paramTypes.end()) {
        return nullptr;
    }
    return it->second.param;
}

}
