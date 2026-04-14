// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "Container.hpp"
#include <string>

// Explicitly tell the compiler: if you see a const char*, deduce std::string
Container(const char*) -> Container<std::string>;
