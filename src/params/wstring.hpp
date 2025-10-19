/*
 * Copyright (C) 2024,2025, Soar Qin<soarchin@gmail.com>

 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#pragma once

#include <cstdint>

namespace er::params {

struct WstringImpl {
    void *vtable;
    wchar_t *string;
    void *padding;
    uint64_t length;
    uint64_t capacity;
};

extern const wchar_t *wstringImplStr(const WstringImpl *str);
extern wchar_t *wstringImplStrMutable(WstringImpl *str);

}
