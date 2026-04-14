// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "Core.hpp"

namespace MathLib {
    // Better match: takes a double
    void scale(Vector, double) {
        std::cout << "Scaled by double (Precise)\n";
    }
}
