cmake_minimum_required(VERSION 3.20)
project(giga-drill-breaker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- LLVM/Clang from submodule ---
set(LLVM_ENABLE_PROJECTS "clang" CACHE STRING "" FORCE)
# X86 for Linux, AArch64 for Apple Silicon Macs
set(LLVM_TARGETS_TO_BUILD "X86;AArch64" CACHE STRING "" FORCE)
set(LLVM_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLVM_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_DOCS OFF CACHE BOOL "" FORCE)

add_subdirectory(extern/llvm-project/llvm EXCLUDE_FROM_ALL)

# Make LLVM and Clang headers visible
include_directories(
  ${CMAKE_SOURCE_DIR}/extern/llvm-project/llvm/include
  ${CMAKE_BINARY_DIR}/extern/llvm-project/llvm/include
  ${CMAKE_SOURCE_DIR}/extern/llvm-project/clang/include
  ${CMAKE_BINARY_DIR}/extern/llvm-project/llvm/tools/clang/include
)

# Match LLVM's RTTI setting (off by default) to avoid linker errors
if(NOT LLVM_ENABLE_RTTI)
  if(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /GR-")
  else()
    string(APPEND CMAKE_CXX_FLAGS " -fno-rtti")
  endif()
endif()

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
