cmake_minimum_required(VERSION 3.20)
project(giga-drill-breaker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- LLVM/Clang: prefer system packages, fall back to submodule ---
set(LLVM_SUBMODULE_DIR "${CMAKE_SOURCE_DIR}/extern/llvm-project/llvm")

find_package(LLVM 18 CONFIG QUIET)
find_package(Clang CONFIG QUIET HINTS ${LLVM_INSTALL_PREFIX}/lib/cmake/clang)

if(LLVM_FOUND AND Clang_FOUND)
  message(STATUS "Using system LLVM ${LLVM_PACKAGE_VERSION}")
  message(STATUS "Using system Clang from ${Clang_DIR}")
  include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
  add_definitions(${LLVM_DEFINITIONS})
elseif(EXISTS "${LLVM_SUBMODULE_DIR}/CMakeLists.txt")
  message(STATUS "Using LLVM/Clang from submodule")
  set(LLVM_ENABLE_PROJECTS "clang" CACHE STRING "" FORCE)
  set(LLVM_TARGETS_TO_BUILD "X86;AArch64" CACHE STRING "" FORCE)
  set(LLVM_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(LLVM_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
  set(LLVM_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)
  set(LLVM_INCLUDE_DOCS OFF CACHE BOOL "" FORCE)
  add_subdirectory(${LLVM_SUBMODULE_DIR} EXCLUDE_FROM_ALL)
  include_directories(
    ${CMAKE_SOURCE_DIR}/extern/llvm-project/llvm/include
    ${CMAKE_BINARY_DIR}/extern/llvm-project/llvm/include
    ${CMAKE_SOURCE_DIR}/extern/llvm-project/clang/include
    ${CMAKE_BINARY_DIR}/extern/llvm-project/llvm/tools/clang/include
  )
else()
  message(FATAL_ERROR
    "LLVM/Clang not found. Install llvm-18-dev and libclang-18-dev, "
    "or initialize the submodule: git submodule update --init")
endif()

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
