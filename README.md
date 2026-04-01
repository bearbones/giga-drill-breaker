# giga-drill-breaker

A Clang LibTooling backend for safe, AST-aware C++ refactoring and static
analysis. It exposes two features as subcommands:

- **mugann** — detect fragile ADL/CTAD resolutions across translation units
- **lagann** — apply rule-driven, multi-pass AST matcher transformations

Designed as a backend for external systems (e.g. a Python script translating
a custom DSL) that generate matcher/replacement rule specifications.

---

## Features

### mugann — ADL/CTAD Fragility Analysis

ADL (Argument-Dependent Lookup) and CTAD (Class Template Argument Deduction)
can silently resolve to different declarations depending on which headers are
included in a given translation unit. This is a portability and correctness
hazard that standard compilers do not diagnose.

`mugann` runs a two-phase analysis:

1. **Index phase** — walks every translation unit and records all function
   overloads and deduction guides found in any header, building a
   project-wide `GlobalIndex`.
2. **Analysis phase** — re-walks each translation unit, comparing resolved
   call sites and CTAD usages against the global index. Any overload or
   deduction guide that exists globally but is invisible in the current TU
   (because its header is not included) is flagged as a fragile resolution.

Output is a list of diagnostics with source locations and human-readable
messages indicating which header to include or how to qualify the call.

### lagann — AST-Based Source Transformations

`lagann` parses matcher expressions from a JSON rules file and runs them
against source files using Clang's dynamic AST matcher API. It supports
multi-pass pipelines where each pass can build on the results of the
previous one.

Rules are specified as JSON objects containing:
- A matcher expression string (Clang's dynamic AST matcher DSL)
- A bind ID for the root matched node
- An action name (resolved to a built-in callback)

---

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

# Configure (first time is slow due to LLVM build if not using system LLVM)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build the tool
cmake --build build --target giga-drill-breaker

# Build and run tests
cmake --build build --target giga_drill_tests
cd build && ctest --output-on-failure
```

The build system first tries to find a system-installed LLVM/Clang 18
(`llvm-18-dev`, `libclang-18-dev`). If not found, it falls back to the
bundled submodule.

On macOS with Apple Silicon, AArch64 support is included automatically.
For Intel-only builds: `-DLLVM_TARGETS_TO_BUILD=X86`.

---

## Usage

### mugann — Analyze for Fragile ADL/CTAD

```bash
./build/giga-drill-breaker mugann \
  --build-path /path/to/compile_commands_dir \
  --source file1.cpp file2.cpp
```

Output example:

```
src/logic.cpp:42:5: Fragile ADL resolution: MathLib::scale(Vector, double) exists in
    Extension.hpp but is not visible here. The current call resolves to
    MathLib::scale(Vector, int). Include Extension.hpp or explicitly qualify the call.
```

### lagann — Apply Transformations

```bash
./build/giga-drill-breaker lagann \
  --rules-json rules.json \
  --build-path /path/to/compile_commands_dir \
  --source file1.cpp file2.cpp \
  --dry-run
```

The `--dry-run` flag collects replacements without writing them to disk,
which is useful for previewing changes.

---

## Project Structure

```
CMakeLists.txt                  Top-level build configuration
extern/llvm-project/            LLVM/Clang submodule (sparse checkout)

include/giga_drill/
  mugann/                       Public headers — ADL/CTAD analysis feature
    GlobalIndex.h               Project-wide declaration database
    Indexer.h                   Phase-1 AST visitor (index all declarations)
    Analyzer.h                  Phase-2 AST visitor (detect fragile resolutions)
  lagann/                       Public headers — transform pipeline feature
    MatcherEngine.h             Dynamic matcher parsing and execution
    TransformPipeline.h         Multi-pass transform orchestration

src/
  main.cpp                      CLI entry point (mugann / lagann subcommands)
  mugann/                       mugann implementation
    GlobalIndex.cpp
    Indexer.cpp
    Analyzer.cpp
  lagann/                       lagann implementation
    MatcherEngine.cpp
    TransformPipeline.cpp
  CMakeLists.txt

tests/                          Catch2 test suite
  test_matcher_engine.cpp       Unit tests for MatcherEngine::parse and addRule
  test_transforms.cpp           Integration test stubs for lagann transforms
  test_adl_ctad.cpp             Unit and integration tests for mugann analysis

examples/
  adl_fallback/                 ADL fragility example (include-order sensitivity)
    Core.hpp, Extension.hpp, Logic.hpp
    order_a.cpp, order_b.cpp
  ctad_fallback/                CTAD fragility example (deduction guide visibility)
    Container.hpp, Factory.hpp, Guide.hpp
    order_a.cpp, order_b.cpp
  macro_split/                  Boolean macro splitting transform example
    input.cpp, expected.cpp
  builder_to_struct/            Builder pattern to struct conversion example
    input.cpp, expected.cpp
```

---

## Architecture

### Two-Phase Analysis (mugann)

```
Phase 1 — Index:
  ClangTool(all files) → IndexerActionFactory
    → for each TU: IndexerVisitor
      → VisitFunctionDecl → GlobalIndex::addFunctionOverload
      → VisitCXXDeductionGuideDecl → GlobalIndex::addDeductionGuide

Phase 2 — Analyze:
  ClangTool(all files) → AnalyzerActionFactory
    → for each TU: AnalyzerVisitor(GlobalIndex)
      → VisitCallExpr → check ADL candidates vs global index
      → VisitVarDecl  → check CTAD usages vs global index
      → emit Diagnostic entries
```

### Transform Pipeline (lagann)

```
For each pass:
  MatcherEngine(pass rules)
    → parse matcher expressions (Clang dynamic parser)
    → register callbacks with MatchFinder
    → ClangTool::run → collect Replacements
  merge into allReplacements_
Apply replacements to disk (when not dry-run)
```

### Key Design Patterns

| Pattern | Where used |
|---|---|
| RecursiveASTVisitor | IndexerVisitor, AnalyzerVisitor |
| FrontendActionFactory | IndexerActionFactory, AnalyzerActionFactory |
| MatchFinder + MatchCallback | MatcherEngine (via CallbackAdapter) |
| Two-phase index/analyze | runAnalysis() in Analyzer.cpp |
| Multi-pass pipeline | TransformPipeline::execute() |

---

## Examples

### ADL Fragility (`examples/adl_fallback/`)

`order_a.cpp` and `order_b.cpp` contain identical code but include headers in
different orders. Due to ADL, the same unqualified call resolves to different
overloads depending on which `scale()` overload is visible at the call site.
`mugann` detects this without requiring compilation of both orderings.

### CTAD Fragility (`examples/ctad_fallback/`)

`Container c("hello")` deduces differently depending on whether `Guide.hpp`
(which contains an explicit `Container(const char*) -> Container<std::string>`
deduction guide) is included. `mugann` flags the case where the explicit guide
is absent.

### Boolean Macro Split (`examples/macro_split/`)

`input.cpp` uses a boolean macro for a compound flag. `expected.cpp` shows the
target form after splitting into separate named parameters. The `lagann`
transform for this pattern is a planned TDD target.

### Builder to Struct (`examples/builder_to_struct/`)

`input.cpp` uses a builder pattern with chained setters. `expected.cpp` shows
the equivalent aggregate initialization. Demonstrates a significant reduction
in boilerplate (1130 → 482 bytes) achievable via AST rewriting.

---

## Current Status

| Component | Status |
|---|---|
| MatcherEngine (parse + run) | Complete |
| TransformPipeline (multi-pass) | Complete (apply-to-disk TODO) |
| GlobalIndex | Complete |
| Indexer (two-phase phase 1) | Complete |
| Analyzer (two-phase phase 2) | Complete |
| mugann CLI subcommand | Complete |
| lagann CLI subcommand | Complete (JSON parsing TODO) |
| Test suite (unit) | Complete |
| Test suite (integration stubs) | Stubs present, impl pending |
