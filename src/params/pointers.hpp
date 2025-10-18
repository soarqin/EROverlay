/*
 * Copyright (C) 2024,2025, Soar Qin<soarchin@gmail.com>

 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#pragma once

#include <cstdint>

namespace er::params {

class Pointers {
public:
    void init();
    uintptr_t csRegulationManager = 0;

private:
    bool initialized_ = false;
};

extern Pointers gPointers;

}
