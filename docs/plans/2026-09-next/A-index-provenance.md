# A — Index freshness and provenance

## Outcome

A warm index reflects the effective compilation inputs. Consumers can distinguish
successfully indexed TUs from failed, skipped, or partial parses.

## Evidence and starting points

On 2026-09-08, changing only a compile command from no definition to
`-DNEW_TARGET` caused `megascope index` to report zero refreshed TUs and retain
the old callee; `--force` selected the new callee. See `src/main.cpp` warm-start
configMatch, `SnapshotIO::dirtyTUs`, `SnapshotMeta`, and `BuildStats`.

## Work

1. Add a regression using one TU with `#ifdef NEW_TARGET` selecting between two
   callees. Change only its compile command; verify refresh matches a clean bake.
2. Define a deterministic per-TU effective-input fingerprint: compile arguments,
   working directory, actual extra arguments/sysroot, relevant toolchain and
   analyzer identity, and configuration that affects indexed facts. Handle
   multiple compile commands deliberately; do not merge variants accidentally.
   Preserve argument order where it changes semantics. Document what ambient
   inputs are supported and what requires explicit invalidation.
3. Persist the fingerprints and compare them on warm refresh. Retain existing
   source/header invalidation and source-selection semantics. Address same-size
   dependency edits within the recorded timestamp resolution with a justified
   strategy; do not claim content correctness from coarse timestamps alone.
4. Persist requested TU scope and per-TU outcomes with reasons. Follow outcomes
   through serial, pooled, isolated, incremental, and shard-merge paths. A failed
   parse must not silently become a healthy cached TU; define retry behavior.
5. Publish a compact immutable provenance/coverage API for C. C owns the common
   query envelope; avoid a competing adapter-wide schema in this package.

## Ownership and boundaries

Own Snapshot.h/.cpp, input fingerprint helpers, bake outcome production, and
warm-start wiring in main.cpp. Coordinate with F before any format work. Inspect
anneal checkpoints for the same risks and record a follow-up if applicable;
rewriting anneal checkpointing is outside this package.

## Acceptance

- Compare semantic query results from incremental and clean builds after TU,
  header, compile-flag, working-directory/configuration, and selected-file changes.
- Cover failed/skipped TU persistence and recovery, including isolated workers.
- Cover format round trips, old-format rejection, partial section loads, and
  unchanged refresh. No decode of the full graph solely to check freshness.
- Record unchanged-refresh overhead against a fresh baseline; explain material
  regressions and the accuracy/performance tradeoff of any content hashing.
- Supported LLVM matrix and relevant snapshot/worker/CLI tests pass.

## Deliverables

Implementation and tests; fingerprint/provenance contract; reproducible stale
compile-command regression; measured refresh costs; exact handoff to C and F.
