// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// SPDX-License-Identifier: Apache-2.0

// Order B: Factory included before Guide — uses implicit guide.
#include "Factory.hpp"
#include "Guide.hpp"

int main() {
    auto c = make_container();
    // c is Container<const char*>
}
