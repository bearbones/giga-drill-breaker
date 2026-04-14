// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "Core.hpp"

// Non-template inline function. Lookup happens RIGHT HERE.
inline void do_scaling() {
    MathLib::Vector v;
    scale(v, 3.14); // 3.14 is a double. ADL is triggered for MathLib::scale
}
