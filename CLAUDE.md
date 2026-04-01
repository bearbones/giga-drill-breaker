# CLAUDE.md — Developer Guide for AI Assistants

This file is the canonical reference for AI coding assistants (Claude and
others) working in this repository. Read it before making changes.

---

## What This Repository Does

`giga-drill-breaker` is a **Clang LibTooling backend** for C++ static analysis
and source-to-source transformation. It uses Clang's AST (Abstract Syntax
Tree) infrastructure to:

1. **Detect fragile ADL/CTAD resolutions** — header-order-sensitive name
   lookups that silently resolve to different declarations depending on what
   is included.
2. **Apply AST matcher transformations** — rule-driven rewrites of C++ source
   code using Clang's dynamic matcher DSL.

It is designed to be called by external tooling (e.g. a Python orchestrator)
that supplies matcher/rule specifications.

---

## Naming Reference (TTGL)

The project and its features are named after *Tengen Toppa Gurren Lagann*, a
mecha anime whose central motif is the spiral drill — a force that evolves,
combines, and breaks through any limit.

- **giga-drill-breaker** — the Giga Drill Breaker finishing move: an
  impossibly large spiral drill that pierces through whatever stands in the way.

- **lagann** — Simon's core mech. Its nature is *transformation and
  combination*: it bores into other machines and reshapes them into new forms.
  Fitting for the subcommand that drills into ASTs and rewrites source code.

- **mugann** — the Anti-Spiral's hunter-killers. Their defining trait: they
  appear inert, give no warning, and the danger is entirely in *how they
  resolve* — a Mugann killed in the wrong context detonates catastrophically.
  Fragile ADL/CTAD resolutions have the same character: they compile silently
  and only detonate when include order shifts.

---

## Feature Layout

The codebase is split into two named feature folders:

### `mugann` — ADL/CTAD Analysis

**Headers:** `include/giga_drill/mugann/`
**Sources:** `src/mugann/`

| File | Purpose |
|---|---|
| `GlobalIndex.h/.cpp` | In-memory database of all function overloads and deduction guides discovered across the project |
| `Indexer.h/.cpp` | Phase-1 AST visitor: walks every translation unit and populates `GlobalIndex` |
| `Analyzer.h/.cpp` | Phase-2 AST visitor: compares each TU's resolved names against `GlobalIndex`; emits `Diagnostic` entries for fragile resolutions |

The main entry point is `giga_drill::runAnalysis(compDb, files)` defined in
`Analyzer.h`. It orchestrates both phases and returns a `vector<Diagnostic>`.

### `lagann` — AST-Based Transformations

**Headers:** `include/giga_drill/lagann/`
**Sources:** `src/lagann/`

| File | Purpose |
|---|---|
| `MatcherEngine.h/.cpp` | Parses Clang dynamic matcher expression strings; registers callbacks with `MatchFinder`; runs `ClangTool` and accumulates `Replacement` objects |
| `TransformPipeline.h/.cpp` | Orchestrates multiple passes of `MatcherEngine`; merges replacements across passes; optionally writes to disk |

The main entry point is `giga_drill::TransformPipeline::execute(buildPath, files, dryRun)`.

---

## CLI Entry Point

`src/main.cpp` uses LLVM's `CommandLine` library with two `cl::SubCommand`
objects:

```
giga-drill-breaker mugann --build-path <dir> --source <files...>
giga-drill-breaker lagann --rules-json <file> --build-path <dir> --source <files...> [--dry-run]
```

Each subcommand has its own scoped options (declared with `llvm::cl::sub(...)`).
To add a new subcommand, follow the pattern in `main.cpp`:
1. Declare a `static llvm::cl::SubCommand MyCmd("name", "description")`.
2. Declare option variables with `llvm::cl::sub(MyCmd)`.
3. Add an `if (MyCmd) { ... }` branch in `main()`.

---

## Build System

`CMakeLists.txt` (top-level):
- Requires CMake 3.20+, C++17.
- Tries system LLVM/Clang 18 first; falls back to `extern/llvm-project` submodule.
- Disables RTTI (`-fno-rtti`) to match LLVM's default.

`src/CMakeLists.txt`:
- Builds `giga_drill_lib` from `mugann/*.cpp` and `lagann/*.cpp`.
- Builds `giga-drill-breaker` executable from `main.cpp`.
- Links against: `clangTooling`, `clangDynamicASTMatchers`, `clangASTMatchers`,
  `clangAST`, `clangBasic`, `clangFrontend`, `clangSema`, `clangSerialization`,
  `clangRewrite`, `clangToolingCore`, `LLVMSupport`.

`tests/CMakeLists.txt`:
- Fetches Catch2 v3.7.1 via FetchContent.
- Defines `PROJECT_SOURCE_DIR` for example file paths.

---

## Build and Test Commands

```bash
# Configure (Release)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Configure (Debug — faster iteration)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build the CLI tool
cmake --build build --target giga-drill-breaker

# Build tests
cmake --build build --target giga_drill_tests

# Run tests
cd build && ctest --output-on-failure

# Run a specific test tag
cd build && ctest -R "GlobalIndex" --output-on-failure
```

Run tests from the project root, or ensure `PROJECT_SOURCE_DIR` is set
correctly (the CMake build sets it automatically via a compile definition).

---

## Adding a New Analysis to mugann

1. **If new data is needed globally**, extend `GlobalIndex` (`GlobalIndex.h`
   and `GlobalIndex.cpp`) with a new entry struct and lookup method.

2. **Collect data** by extending `IndexerVisitor` in `Indexer.cpp`:
   - Add a `VisitXxxDecl(clang::XxxDecl *decl)` method.
   - Populate the new `GlobalIndex` fields.

3. **Detect fragility** by extending `AnalyzerVisitor` in `Analyzer.cpp`:
   - Add a `VisitXxxExpr` or `VisitXxxDecl` method.
   - Query `GlobalIndex` and push `Diagnostic` entries.

4. **Add tests** in `tests/test_adl_ctad.cpp` following the existing pattern
   of constructing in-memory code strings and running `runToolOnCodeWithArgs`.

---

## Adding a New Transform to lagann

1. Create a `TransformRule` struct with:
   - `matcherExpression`: a Clang dynamic matcher string
     (e.g. `"functionDecl(hasName(\"foo\"))"`)
   - `bindId`: the bind ID for the root node (e.g. `"fn"`)
   - `callback`: a `ReplacementCallback` lambda that receives the
     `MatchFinder::MatchResult` and returns `vector<Replacement>`

2. Add the rule to a pipeline pass:
   ```cpp
   TransformPipeline pipeline;
   pipeline.addPass({ rule1, rule2 });
   pipeline.execute(buildPath, sourceFiles, dryRun);
   ```

3. Test using the example files in `examples/` as TDD targets.

The JSON rules format for the `lagann` CLI subcommand is not yet implemented
(see TODOs below). For now, rules are added programmatically.

---

## Key Design Patterns

### Two-Phase Analysis

`runAnalysis()` runs two separate `ClangTool` passes over the same file set:

```
Pass 1: IndexerActionFactory → IndexerConsumer → IndexerVisitor
  → fills GlobalIndex with all declarations across all TUs

Pass 2: AnalyzerActionFactory → AnalyzerConsumer → AnalyzerVisitor
  → reads GlobalIndex, walks each TU, emits Diagnostics
```

The two-pass design is necessary because a single TU cannot know what
declarations exist in other TUs that are not included.

### RecursiveASTVisitor

All AST traversal uses Clang's `RecursiveASTVisitor<Derived>` CRTP base.
Override `VisitXxxDecl` or `VisitXxxExpr` methods; return `true` to continue
traversal, `false` to stop.

### FrontendActionFactory

`ClangTool::run()` takes a `FrontendActionFactory*`. The pattern used here:

```
XxxActionFactory → creates XxxAction per TU
XxxAction → creates XxxConsumer
XxxConsumer::HandleTranslationUnit → drives XxxVisitor
```

### MatchFinder Callbacks

`MatcherEngine` uses Clang's `MatchFinder` with a nested `CallbackAdapter`
class that bridges between `MatchFinder::MatchCallback` and the user-supplied
`ReplacementCallback` function objects.

---

## Development Branch

All changes go to: `claude/reorganize-features-document-aPqVo`

Commit convention: descriptive imperative messages, no ticket numbers required.

---

## Current TODOs

| Area | TODO |
|---|---|
| `lagann` CLI | Parse `--rules-json` and populate `TransformPipeline` passes |
| `TransformPipeline` | Apply replacements to disk between passes (not just collect) |
| `TransformPipeline` | Write final replacements to disk when `!dryRun` |
| Tests | Implement full integration tests in `test_transforms.cpp` (currently stubs) |
| Matcher DSL | Define and document the JSON schema for rules files |

---

## Namespace

All types and functions live in `namespace giga_drill`. There are no nested
namespaces per feature — the feature folders are a filesystem organization
only, not a namespace boundary.

---

## External Dependencies

- **LLVM/Clang 18** — system package or git submodule at `extern/llvm-project`
- **Catch2 v3.7.1** — fetched automatically by CMake during test configuration
- No other runtime dependencies
