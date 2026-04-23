# PCH/SDK Mismatch: Root Cause Analysis and Fix Plan

## Symptoms

When `--pch-dir` is used, ~24% of TUs crash (SIGSEGV, now caught by the crash
guard). The crashes affect TUs that include libc++ headers not covered by the
PCH preamble (e.g., `<future>`, `<memory/allocator_arg_t.h>`). Errors include:

```
error: variable has incomplete type 'struct _LIBCPP_TEMPLATE_VIS'
error: redefinition of 'allocator_arg'
error: redefinition of '__uses_alloc_ctor_imp'
```

Without `--pch-dir`, the same TUs parse fine.

## Root Cause

**The PCH is compiled with a different header search order than the TU parse.**

The PCH compilation uses an external `clang++` binary via `system()`. The TU
parse uses `ClangTool` (LLVM's in-process tooling API). Even though both are
LLVM 21, they resolve libc++ headers differently:

### External `clang++` driver (PCH compilation)

When LLVM 21's `clang++` is invoked with `-isysroot .../xcode-16.2/.../MacOSX.sdk`:

```
Header search order:
1. /opt/homebrew/.../llvm/21.1.0/include/c++/v1/      ← LLVM 21's libc++ (driver-injected)
2. .../MacOSX.sdk/usr/local/include
3. /opt/homebrew/.../llvm/21.1.0/lib/clang/21/include  ← resource-dir builtins
4. .../MacOSX.sdk/usr/include                           ← SDK system headers
```

LLVM 21's libc++ is at position 1. `<__config>` resolves to LLVM 21's version.
This is what gets baked into the PCH.

### ClangTool (TU parsing)

ClangTool invokes the cc1 frontend directly, bypassing some driver logic. Our
`getResourceDirAdjuster()` injects `-resource-dir`, and the compile command's
explicit `-I` or `-isystem` paths to the SDK's libc++ may have different
priority relative to the injected paths.

The result: some headers resolve to LLVM 21's libc++ during PCH compilation
but to the Xcode SDK's libc++ during TU parsing (or vice versa). When a TU
loads the PCH and then includes a header that pulls in a sub-header already
baked into the PCH, the two versions' definitions clash → "redefinition" →
error recovery → corrupt AST → SIGSEGV.

## Why Non-PCH Mode Works

Without PCH, every header is parsed fresh in a single pass. The ClangTool's
header search order is consistent within each TU. The Xcode SDK libc++ headers
and LLVM 21's `__config` macros are resolved once, in the same search order, so
there's no cross-version conflict.

## Requirements for a Fix

1. **Identical header resolution**: The PCH must be compiled with EXACTLY the
   same header search order, macro definitions, and argument adjustments that
   the TU parse will use. No discrepancy is tolerable — even a single different
   `-I` path ordering can cause redefinition errors.

2. **No external process**: Shelling out to `clang++` fundamentally cannot
   guarantee identical resolution because the driver and cc1 behave differently.
   The fix must use the same in-process path.

3. **Caching**: The compiled PCH must be reusable across TUs that share the same
   preamble header. Recompiling per-TU defeats the purpose.

4. **Thread safety**: PCH compilation must complete before any TU that uses it
   starts parsing. The current architecture already enforces this (PCH is built
   before `buildCallGraph`).

## Proposed Solution: In-Process PCH via GeneratePCHAction

Clang provides `GeneratePCHAction` — a `FrontendAction` that produces a `.pch`
file. It can be run via `ClangTool::run()` just like our AST visitors, going
through the exact same argument adjustment pipeline (`getResourceDirAdjuster`,
`getStripIncompatibleFlagsAdjuster`, etc.).

### Implementation

Replace the `system()` call in `PchCache::buildFromCompileCommands` with a
`ClangTool` run using `GeneratePCHAction`:

```cpp
// For each unique PCH source header:
// 1. Create a temporary .cpp that #include's the PCH header
// 2. Create a ClangTool with that temp file and the first matching TU's flags
// 3. Override the output file to point to our cache dir
// 4. Run GeneratePCHAction via the tool
// 5. The .pch is now compiled with identical header resolution as TU parsing

class PchGenerateAction : public GeneratePCHAction {
  // Override to set the output path to our cache dir
};

class PchGenerateFactory : public FrontendActionFactory {
  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<PchGenerateAction>();
  }
};
```

The key insight: `makeClangTool()` applies all our adjusters (resource-dir,
incompatible flag stripping). By running `GeneratePCHAction` through the same
`makeClangTool()`, the PCH sees the exact same headers as a normal TU parse.

### Challenge: Output Path Control

`GeneratePCHAction` writes the PCH to the path specified by `-o` in the compile
command, or derives it from the input filename. We need to either:

- Inject `-o <cache_path>.pch` into the command via an argument adjuster
- Or override `shouldEraseOutputFiles()` and capture the output path

### Challenge: Input File

`GeneratePCHAction` expects to compile a source file, not a header directly.
The PCH header (e.g., `cmake_pch_arm64.hxx`) is referenced via `-include` in
the TU's command, not as the main source file. Options:

**Option A**: Create a synthetic temp `.cpp` that `#include`s the PCH header,
then compile it with `-emit-pch`. The temp file's compile flags come from the
first TU that uses this PCH.

**Option B**: Use `-x c++-header` mode with `GeneratePCHAction`. This requires
setting the input kind to header, which may need a custom `CompilerInvocation`
rather than going through `ClangTool`.

**Option C**: Use `clang::PrecompiledPreamble` API directly. This is what
clangd uses for preamble caching. It handles the input-file question internally.

## Experiments Needed

### Experiment 1: Verify the header search order hypothesis

Run the same TU (BaseWrapProp.cpp) in two modes, capturing `-v` verbose output:

```bash
# External clang++ (simulating PCH compilation)
clang++ -v -x c++-header <flags from compile_commands> <pch_header> -fsyntax-only 2>&1 | grep "search starts"

# ClangTool (simulating TU parsing)
# Add -v to the argument adjuster temporarily
```

Compare the `#include <...> search starts here:` lists. If they differ, we've
confirmed the root cause. If they're identical, the issue is elsewhere.

### Experiment 2: In-process PCH via GeneratePCHAction

Minimal test: create a `ClangTool` with `GeneratePCHAction` for the PCH header,
run it, check if the .pch is produced. Then load that .pch in a second
`ClangTool` run for BaseWrapProp.cpp and see if the redefinition errors are gone.

### Experiment 3: PrecompiledPreamble API

Test `clang::PrecompiledPreamble::Build()` with the PCH header content. This
API handles the input-file question and may be simpler than wrapping
`GeneratePCHAction`. clangd's source code (`clangd/Preamble.cpp`) is the
reference implementation.

### Experiment 4: No-PCH with Buck2 database (baseline)

Run the full 3184-TU set with the Buck2 compilation database (expanded, no PCH,
no resource-dir injection). If this works without crashes, it confirms that the
crashes are purely a PCH/resource-dir issue and we can use Buck2 as the
no-PCH fast path while working on the PCH fix.

## Decision: Which approach for PCH compilation?

| Approach | Complexity | Correctness guarantee | Notes |
|----------|-----------|----------------------|-------|
| GeneratePCHAction via ClangTool | Medium | High — same pipeline | Need to solve output path + input file |
| PrecompiledPreamble API | Medium | High — designed for this | More API surface, but clangd proves it works |
| Fix the system() invocation | Low | Low — fundamental mismatch | Band-aid; will break again with toolchain changes |
| Don't use PCH, rely on parallelism | Zero | N/A | 3.3x speedup instead of 10x+ — may be sufficient |

**Recommendation**: Start with Experiment 1 to confirm the hypothesis. If
confirmed, implement the `GeneratePCHAction` approach (it's the most direct
fix). Fall back to `PrecompiledPreamble` if `GeneratePCHAction` proves difficult
to wire up for header inputs.

## Experiment Results (2026-04-22)

### Experiment 1: Confirmed

The driver injects `-internal-isystem /opt/homebrew/.../include/c++/v1` (LLVM
21's libc++) before all other search paths. ClangTool's cc1 path does NOT inject
this. The PCH (compiled via driver) bakes in LLVM 21's libc++ headers; the TU
(parsed via ClangTool cc1) resolves to the Xcode SDK's libc++ headers →
redefinition errors → SIGSEGV.

### Experiment 2b: Fix confirmed

When BOTH the PCH and TU are compiled via the same driver invocation (same
binary, same flags, same `-resource-dir`), the libc++ redefinition errors
disappear completely. The only remaining errors are missing generated headers
(`AssetsUploadApi/ErrorInfoModel.h`) — normal compilation issues that produce
graceful errors, not crashes.

| Test | PCH via | TU via | Mismatch? | Result |
|------|---------|--------|-----------|--------|
| Current | driver `system()` | ClangTool cc1 | YES | SIGSEGV (24% of TUs) |
| Exp 2b | driver | driver | NO | Works (clean libc++) |
| Fix | ClangTool cc1 | ClangTool cc1 | NO | Expected: works |

### Conclusion

Implement `GeneratePCHAction` through `ClangTool::run()`. This guarantees
both PCH and TU go through identical cc1 argument processing with identical
header search paths. No driver-injected `-internal-isystem` mismatch.
