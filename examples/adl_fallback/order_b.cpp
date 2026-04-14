// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// SPDX-License-Identifier: Apache-2.0

// Order B: Logic included before Extension — silent fallback.
#include "Logic.hpp"
#include "Extension.hpp"

int main() {
    do_scaling();
    // Output: Scaled by int (Fallback)
}
