# B — Call-site-accurate path analysis

## Outcome

Graph, exception, and lock path queries share bounded traversal semantics and
retain the exact call site for each edge. Exception conclusions describe the
paths actually observed and never infer exhaustiveness from a bounded sample.

## Evidence and starting points

The existing binary returned `never_caught` with two uncaught paths for this
fixture, which has one protected and one unprotected call:

```cpp
void target() { throw 1; }
int main(int argc, char**) {
  if (argc > 1) { try { target(); } catch (...) {} }
  else { target(); }
}
```

Inspect ControlFlowOracle::findPathsToTarget/queryExceptionProtection and their
first-matching-context lookup, GraphTools.cpp's edge-preserving traversal, and
LockTools.cpp's bounded search. The oracle currently caps exception paths at 100.

## Work

1. Turn the fixture into a failing regression. Verify both call-site witnesses,
   not only the final label. Add an overload identity regression.
2. Introduce a shared traversal result carrying exact caller/callee USRs, call
   sites, edge kind/confidence/execution context, and structured stop reasons.
   Define depth units, path count limits, work budgets, cycle handling, and hub
   pruning. Preserve useful existing search optimizations.
3. Migrate graph/exception/lock path consumers without losing lock semantics.
   Join control-flow context on exact identity/site rather than display names or
   the first caller/callee match. Distinguish missing context from empty context.
4. Correct exception propagation order: matching handlers, catch-all, rethrows,
   and termination at noexcept boundaries. Separate asynchronous execution from
   a synchronous stack. Represent unsupported facts as unknown; do not turn a
   lexical path into a proof of feasible execution.
5. Publish producer-side completeness facts for C. Keep universal verdicts
   conditional on exhaustive search within the declared graph/model scope.
6. Document a migration path for anneal's name-level exception summaries to the
   shared USR-based facts. Full anneal migration is a follow-up, not a prerequisite
   for landing this bounded package.

## Ownership and dependencies

Own path traversal, oracle logic, and tool-specific path consumption. Coordinate
GraphTools/LockTools edits with D's deterministic ordering fixes. C owns common
serialization/adapters; publish the contract before broad handler migration.
Avoid snapshot changes unless required for a missing semantic fact; coordinate
those with A and document compatibility.

## Acceptance

- Fixtures: protected/unprotected duplicate calls, overloads, recursion, more
  paths than the limit, depth/work/hub cutoffs, typed handlers/inheritance,
  rethrows, noexcept termination, and thread/callback boundaries.
- Reordering TU input or index insertion does not change semantic classification.
- Unexplored paths cannot produce an unconditional always/never verdict.
- Existing graph and lock answers retain their documented meaning; every changed
  verdict has a regression and an explanation.
- Query latency on deep chains and high-fan-in graphs has no unexplained major
  regression. Relevant tests and the supported LLVM matrix pass.

## Deliverables

Shared traversal API, consumer migrations, adversarial tests, documented semantic
limits, and an exact contract/commit handoff to C, D, and E.
