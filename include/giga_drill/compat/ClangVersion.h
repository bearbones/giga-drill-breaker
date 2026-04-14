// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GIGA_DRILL_COMPAT_CLANG_VERSION_H
#define GIGA_DRILL_COMPAT_CLANG_VERSION_H

#include "llvm/Config/llvm-config.h"

// Version-check macros for conditional compilation.
//
// Usage:
//   #if GIGA_DRILL_LLVM_AT_LEAST(20)
//     // Clang 20+ code path
//   #else
//     // Clang 18/19 code path
//   #endif
#define GIGA_DRILL_LLVM_AT_LEAST(major) (LLVM_VERSION_MAJOR >= (major))
#define GIGA_DRILL_LLVM_VERSION_IN_RANGE(lo, hi) \
  (LLVM_VERSION_MAJOR >= (lo) && LLVM_VERSION_MAJOR <= (hi))

#endif // GIGA_DRILL_COMPAT_CLANG_VERSION_H
