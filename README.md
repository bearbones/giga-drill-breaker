# giga-drill-breaker

Using LLVM libraries to make refactoring safe and easy.

A tool that parses AST matcher expressions from strings and applies
source-to-source transformations using Clang's LibTooling. Designed as a backend
for external systems (e.g. a Python script translating from a custom DSL) that
generate matcher/replacement rule specifications.

## Building

Requires CMake 3.20+, a C++17 compiler, and Ninja (recommended).

```bash
# Clone with submodules
git clone --recurse-submodules https://github.com/bearbones/giga-drill-breaker.git
cd giga-drill-breaker

# The submodule uses sparse checkout; after cloning you may need:
cd extern/llvm-project
git sparse-checkout set llvm clang cmake third-party
cd ../..

# Configure (first time is slow due to LLVM build)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build the tool
cmake --build build --target giga-drill-breaker

# Build and run tests
cmake --build build --target giga_drill_tests
cd build && ctest
```

On macOS with Apple Silicon, the build includes AArch64 support automatically.
For Intel-only builds, set `-DLLVM_TARGETS_TO_BUILD=X86`.

## Usage

```bash
./build/giga-drill-breaker \
  --rules-json rules.json \
  --build-path /path/to/compile_commands_dir \
  --source file1.cpp file2.cpp \
  --dry-run
```

Rules are specified as a JSON file containing matcher expressions and action
names. The matcher expressions use Clang's dynamic AST matcher DSL.

## Project structure

```
CMakeLists.txt              Top-level build configuration
extern/llvm-project/        LLVM/Clang submodule (sparse checkout)
include/giga_drill/         Public headers
  MatcherEngine.h           Core: dynamic matcher parsing + execution
  TransformPipeline.h       Multi-pass transform orchestration
src/                        Implementation
  main.cpp                  CLI entry point
  MatcherEngine.cpp         Dynamic matcher parsing + MatchFinder integration
  TransformPipeline.cpp     Pipeline execution
tests/                      Catch2 tests
  test_matcher_engine.cpp   Unit tests for matcher parsing
  test_transforms.cpp       Integration test stubs for transforms
examples/                   Example input/expected pairs for TDD
  macro_split/              Boolean macro splitting
  builder_to_struct/        Builder pattern to struct conversion
```

## Future analysis modes

- **ADL/CTAD detection**: Identify implicit conversions from argument-dependent
  lookup or class template argument deduction that may be undesired.
- **Coverage diagnostics**: Analyze whether/why a header-defined function won't
  have a coverage record emitted in a translation unit or binary.
