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
