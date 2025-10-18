/*
 * Copyright (C) 2024,2025, Soar Qin<soarchin@gmail.com>

 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include "wstring.hpp"

namespace er::params {

const wchar_t *wstringImplStr(const WstringImpl *str) {
    if (sizeof(wchar_t) * str->capacity >= 15) {
        return str->string;
    }
    return (const wchar_t*)&str->string;
}

wchar_t *wstringImplStrMutable(WstringImpl *str) {
    if (sizeof(wchar_t) * str->capacity >= 15) {
        return str->string;
    }
    return (wchar_t*)&str->string;
}

}
