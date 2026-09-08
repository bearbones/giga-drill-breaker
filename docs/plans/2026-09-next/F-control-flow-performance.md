# F — Selective control-flow access

## Outcome

Reduce one-shot context-query cost without changing results or turning the CLI
into a hidden resident service. This is a measurement-gated implementation.

## Evidence and dependencies

The repository's September 5 measurements report query-call-site-context at
3.3–3.5 seconds and 1.5 GB RSS for 6.37M contexts; get-callers was 0.87 seconds.
Reproduce or replace these historical baselines with an available, documented
workload. Start measurements/prototypes independently. Coordinate the access API
with B and wait for A's metadata/version changes before production layout edits.

## Work

1. Measure process startup, I/O/page faults, decode, query, and total wall time
   for exact-site, per-function, path-context, and full-dump workloads. Include
   repeated runs, cache conditions, peak RSS, index size, bake and refresh costs.
2. Prototype the smallest viable selective access: fixed/readable context records
   plus lookup offsets, or lazy per-function context loading. Keep the mutable
   index used by bake/reindex. Avoid a full graph rewrite or automatic daemon.
3. Compare the prototype to the same baseline binary/workload. Initial gate:
   at least 2x lower median one-shot exact-site latency and 2x lower peak RSS on
   the representative large fixture, with identical normalized semantic results.
   Investigate any bake/refresh/full-dump regression above 10%; do not silently
   trade those costs away. State noise and cache assumptions.
4. If the gate fails, deliver measurements and a no-go recommendation; that is
   a complete research outcome. If it passes, implement the selective read-only
   path, allocate the next snapshot version with A, and integrate B/C semantics.
5. Ensure full dumps remain streaming and immutable readers retain backing
   storage safely. Validate malformed offsets/lengths and section boundaries.

## Ownership

Own the read-only context access implementation and performance-specific tests.
A owns metadata/invalidation; B owns semantic traversal; D owns shared benchmark
reporting. Use separate prototype files until the shared interfaces settle.

## Acceptance

- Reproducible raw timings and go/no-go analysis using matched workloads.
- If implemented: eager/selective loads have equivalent answers across the
  regression corpus, including absent contexts and partial indexes.
- Snapshot rejection/round-trip tests, mutable reindex behavior, lifetime checks,
  and the supported LLVM matrix pass.
- Final integrated measurements include any costs introduced by A–C. Report
  point-query gains separately from path queries and full scans.

## Deliverables

Benchmark report and decision; production code only if the gate passes; parity
tests, format compatibility notes, and exact reproduction commands.
