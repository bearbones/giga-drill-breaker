// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <iostream>

namespace MathLib {
    struct Vector {};

    // Fallback: takes an int
    void scale(Vector, int) {
        std::cout << "Scaled by int (Fallback)\n";
    }
}
