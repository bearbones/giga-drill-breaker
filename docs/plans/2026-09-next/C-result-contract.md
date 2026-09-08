# C — Shared result and completeness contract

## Outcome

CLI, batch, and MCP communicate result status, indexed coverage, and search
completeness consistently. Human prose is not used to determine machine status.

## Dependencies

Obtain A's provenance/outcome interface and B's traversal/stop-reason interface
before implementation. Prepare contract tests while those packages develop;
integrate against their committed interfaces and land after A/B.

## Work

1. Define structured status/error codes, provenance references, indexed-scope
   coverage, and query stop reasons in the transport-neutral query layer.
   `Tools.h` currently classifies argument errors by message prefixes such as
   "Missing" and "Invalid"; replace that coupling with typed status.
2. Preserve existing payload members and exit-code meanings where feasible.
   Define additive/versioned metadata and document any unavoidable break. An
   empty, complete search is different from an unavailable or incomplete search.
3. Carry A's selected/indexed/failed scope and B's limits/skipped hubs/unresolved
   boundaries into each applicable answer. Identify exactly what completeness
   means: exhaustive within indexed scope and model is not whole-program proof.
4. Establish consistent behavior for incomplete universal verdicts: report
   observed witnesses and uncertainty, not an unconditional always/never claim.
   Include snapshot freshness policy explicitly; queries must not pretend that
   an old snapshot was freshly validated if they did not perform that check.
5. Wire CLI JSON, NDJSON summaries, TSV policy, batch, and MCP from the same
   contract. Avoid repeating large provenance lists on every record; use compact
   references and an inspectable info response.
6. Update help and consumer docs. Coordinate golden changes with D.

## Ownership

Lead owner of query/Tools.h, common result helpers, ToolContext, and common
CLI/MCP result mapping. A/B own generation of underlying facts; do not recreate
their state heuristically in the adapters.

## Acceptance

- Contract tests cover success, complete empty, ambiguity, usage error, index
  failure, partial bake, truncated search, and absent semantic information.
- CLI and MCP agree on meaning; NDJSON retains summary metadata; batch preserves
  per-request status. Existing scripts have a documented compatibility path.
- Changing an error message cannot change an exit code.
- A capped search and an index missing a requested TU cannot silently emit an
  exhaustive safety verdict. Full coverage still does not erase model limits.
- Relevant unit/integration tests, goldens, and supported LLVM CI pass.

## Deliverables

Result-contract documentation, shared implementation, adapter integration,
compatibility notes, tests, and a committed schema handoff to D/E.
