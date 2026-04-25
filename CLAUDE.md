# CLAUDE.md — Developer Guide for AI Assistants

This file is the canonical reference for AI coding assistants (Claude and
others) working in this repository. Read it before making changes.

---

## What This Repository Does

`giga-drill-breaker` is a **Clang LibTooling backend** for C++ static analysis
and source-to-source transformation. It uses Clang's AST (Abstract Syntax
Tree) infrastructure to:

1. **Detect fragile ADL/CTAD resolutions** (`mugann`) — header-order-sensitive
   name lookups that silently resolve to different declarations depending on
   what is included.
2. **Apply AST matcher transformations** (`lagann`) — rule-driven rewrites of
   C++ source code using Clang's dynamic matcher DSL.
3. **Query control flow and exception context** (`cfquery`) — one-shot CLI
   queries for call site guards, try/catch scopes, and exception safety.
4. **Interactive call graph MCP server** (`mcp-serve`) — pre-bakes a multi-TU
   call graph and control flow index, then serves interactive queries via
   JSON-RPC for LLM-assisted code analysis (security audits, dead code, etc.).

It is designed to be called by external tooling (e.g. a Python orchestrator,
or an LLM agent using the MCP server for security vulnerability analysis).

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

The codebase is split into four feature areas:

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

### `callgraph` — Call Graph and Control Flow Analysis

**Headers:** `include/giga_drill/callgraph/`
**Sources:** `src/callgraph/`

| File | Purpose |
|---|---|
| `CallGraph.h/.cpp` | Graph data structure: nodes (functions), edges (calls), class hierarchy, virtual overrides |
| `CallGraphBuilder.h/.cpp` | Two-phase AST visitor: Phase 1 indexes nodes/hierarchy, Phase 2 builds edges |
| `CollapseFilter.h/.cpp` | Path-based edge collapse — skips internal edges in specified directories |
| `ControlFlowIndex.h/.cpp` | Per-call-site record of enclosing try/catch scopes and conditional guards |
| `ControlFlowContextVisitor.cpp` | Phase 3 AST visitor: snapshots exception/guard context at each call site |
| `ControlFlowOracle.h/.cpp` | Query engine: exception safety, path analysis, call site context |

**Three-phase build** (used by both `cfquery` and `mcp-serve`):
1. `buildCallGraph(compDb, files, collapsePaths)` — Phase 1 (index) + Phase 2 (edges)
2. `buildControlFlowIndex(compDb, files, graph, collapsePaths)` — Phase 3 (CF context)

**Edge collapse**: When `collapsePaths` is non-empty, edges where BOTH caller and callee
are in collapsed paths are skipped. Boundary edges (non-collapsed caller → collapsed callee)
are preserved. This reduces noise from utility/math headers while keeping entry points visible.

### `mcp` — MCP Server

**Headers:** `include/giga_drill/mcp/`
**Sources:** `src/mcp/`

| File | Purpose |
|---|---|
| `McpServer.h/.cpp` | JSON-RPC dispatch loop, holds call graph + CF index in memory |
| `McpProtocol.h/.cpp` | Content-Length framing for MCP stdio transport |
| `McpTools.h/.cpp` | Tool implementations: lookup, callers, callees, call chains, exception safety, dead code, class hierarchy |

**8 MCP tools**: `lookup_function`, `get_callees`, `get_callers`, `find_call_chain`,
`query_exception_safety`, `query_call_site_context`, `analyze_dead_code`, `get_class_hierarchy`

---

## CLI Entry Point

`src/main.cpp` uses LLVM's `CommandLine` library with four `cl::SubCommand`
objects:

```
giga-drill-breaker mugann     --build-path <dir> --source <files...>
giga-drill-breaker lagann     --rules-json <file> --build-path <dir> --source <files...> [--dry-run]
giga-drill-breaker cfquery    --build-path <dir> --source <files...> --mode <dump|query> [--collapse-paths <pattern>...]
giga-drill-breaker mcp-serve  --build-path <dir> --source <files...> [--entry-point <name>...] [--collapse-paths <pattern>...]
```

Each subcommand has its own scoped options (declared with `llvm::cl::sub(...)`).
To add a new subcommand, follow the pattern in `main.cpp`:
1. Declare a `static llvm::cl::SubCommand MyCmd("name", "description")`.
2. Declare option variables with `llvm::cl::sub(MyCmd)`.
3. Add an `if (MyCmd) { ... }` branch in `main()`.

### cfquery vs mcp-serve

- **cfquery**: One-shot CLI tool. Parses AST per invocation, outputs JSON to stdout. Best for quick single-file investigations.
- **mcp-serve**: Persistent server. Pre-bakes a unified cross-TU call graph at startup, then serves interactive queries via JSON-RPC over stdio. **Always use mcp-serve for security audits or multi-file analysis** — cfquery is single-TU and cannot see cross-file callers.

### Edge Collapse (--collapse-paths)

Both `cfquery` and `mcp-serve` accept `--collapse-paths` to reduce noise from
header-inlined utility code. Patterns are path component substrings:

```bash
--collapse-paths Client/Math --collapse-paths Client/Core
```

This matches paths containing `/Client/Math/` or `/Client/Core/` as directory
components. Internal edges (both endpoints in collapsed paths) are skipped;
boundary edges (non-collapsed caller → collapsed callee) are preserved.

---

## Build System

`CMakeLists.txt` (top-level):
- Requires CMake 3.20+, C++17.
- Iterates `GIGA_DRILL_SUPPORTED_LLVM_VERSIONS` (currently `20 18`, newest
  first) via `find_package()`. To add a new version, append it to that list.
- Falls back to `extern/llvm-project` submodule if no system package matches.
- Disables RTTI (`-fno-rtti`) to match LLVM's default.
- A lightweight compatibility header (`include/giga_drill/compat/ClangVersion.h`)
  provides `GIGA_DRILL_LLVM_AT_LEAST(major)` for version-conditional code.

`src/CMakeLists.txt`:
- Builds `giga_drill_lib` from `mugann/*.cpp`, `lagann/*.cpp`, `callgraph/*.cpp`, and `mcp/*.cpp`.
- Builds `giga-drill-breaker` executable from `main.cpp`.
- Links against: `clangTooling`, `clangDynamicASTMatchers`, `clangASTMatchers`,
  `clangAST`, `clangBasic`, `clangFrontend`, `clangSema`, `clangSerialization`,
  `clangRewrite`, `clangToolingCore`, `LLVMSupport`.

`tests/CMakeLists.txt`:
- Resolves Catch2 v3.10+ in three tiers (no network at configure time):
  1. Pre-existing `Catch2::Catch2WithMain` imported target.
  2. `find_package(Catch2 3.10 CONFIG)` — system / vcpkg / Conan / Spack.
  3. `extern/Catch2` submodule (pinned to v3.10.0 upstream by default).
- Defines `PROJECT_SOURCE_DIR` for example file paths.

---

## Build and Test Commands

```bash
# Configure (Release) — picks the newest supported LLVM automatically
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Configure against a specific LLVM version (e.g. 20)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/usr/lib/llvm-20

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
| `mcp-serve` | Add `--source-glob` flag for directory-based source selection |
| `find_call_chain` | Annotate path edges with try/catch depth and caught types |
| `call-site-context` | Include catch handler type and body summary in response |
| Concurrency | `query_raii_scopes_at_callsite` — list RAII objects live at a call site |
| Concurrency | `query_locks_held(function)` — trace callers to find implicit locks |
| Concurrency | `query_same_lock(fn_a, fn_b)` — confirm two functions share a lock |
| Scaling | Parallel multi-TU graph construction (currently serial) |
| Scaling | Incremental indexing with binary serialization |

---

## Namespace

All types and functions live in `namespace giga_drill`. There are no nested
namespaces per feature — the feature folders are a filesystem organization
only, not a namespace boundary.

---

## External Dependencies

- **LLVM/Clang 18, 20, or 21** — system package preferred (`find_package`),
  git submodule at `extern/llvm-project` as fallback. Selectable via
  `CMAKE_PREFIX_PATH=/usr/lib/llvm-NN`.
- **Catch2 v3.10+** (tests only) — resolved in three tiers: pre-existing
  imported target -> `find_package` -> `extern/Catch2` submodule.
  Network is not required at configure time. See README.md "Hermetic
  and internal-package-manager builds" for organizations using
  internal mirrors or alternate package managers.
- No other runtime dependencies.
