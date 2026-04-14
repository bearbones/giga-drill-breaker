// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// SPDX-License-Identifier: Apache-2.0

// Order A: Extension included before Logic — correct resolution.
#include "Extension.hpp"
#include "Logic.hpp"

int main() {
    do_scaling();
    // Output: Scaled by double (Precise)
}
