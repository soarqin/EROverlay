/*
 * Copyright (C) 2024,2025, Soar Qin<soarchin@gmail.com>

 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#pragma once

#include "param.hpp"

namespace er::params {

extern bool paramLoadTable();
extern void paramUnload();
extern const ParamTable *paramFindTable(const wchar_t *name);

}
