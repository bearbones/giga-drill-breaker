# D — Validation corpus and deterministic output

## Outcome

A repeatable suite measures answer correctness, incompleteness, deterministic
output, and investigation cost. The open CLI golden-test work is resolved first.

## Start independently

At review time PR #64 was open at 64bb186: LLVM 18/20 passed and LLVM 21 failed
the Test step. The existing local build's cli_golden test passed. Recheck the
live state, retrieve the actual failure, and reproduce it before choosing a fix.
Do not assume the prior unordered-list fix resolved every mismatch.

## Work

1. Own PR #64 repair through complete CI. Prefer finishing its existing branch
   over a duplicate PR, coordinating any active owner. Do not weaken legitimate
   checks to hide platform differences. Normalize only documented environmental
   variation; keep semantic regressions visible.
2. Add direct deterministic-order tests in addition to order-insensitive golden
   comparisons. Exercise changed TU ordering, parallel bake ordering, warm
   reindex, and ties around pagination/top-N boundaries. Specify which lists are
   ordered and which are explicitly sets.
3. Create a small realistic corpus with expected witnesses and negative cases:
   compile-flag invalidation, header changes, overload identity, mixed exception
   protection, static-initialization or ODR divergence, and bounded path search.
   Reuse A/B fixtures through integration instead of writing competing engines.
4. Add an independent runner and report format recording revision, toolchain,
   corpus version, commands, expected/observed findings, missing or false findings,
   incomplete/unknown results, latency, and peak memory. Do not collapse semantic
   quality and speed into one score. Use repeated timings and report variability.
5. Separate small deterministic CI gates from explicitly invoked large-scale
   benchmarks. Pin external fixture revisions and avoid network at test time.
6. Integrate A–C and run the full corpus. Provide E patch-pair fixtures and F a
   reusable workload/report format. Keep large generated output out of Git.

## Ownership and boundaries

Own cli-golden.py, corpus/runner/reporting, benchmark orchestration and CI wiring.
Each feature agent remains responsible for its own regressions. Coordinate any
handler sorting edits with B/C. Land scaffolding early; final integration gates
follow A–C. Do not mark the package complete after only repairing #64.

## Acceptance

- Supported LLVM matrix passes, including the originally failing case.
- A deliberately introduced bad result is detected by the corpus; missing output
  cannot be normalized into a pass. Include expected-negative examples.
- Determinism checks compare raw contractual ordering, not only sorted multisets.
- Reports distinguish false/missing findings from declared unknown/incomplete
  results and include denominator/model scope for any quality rates.
- Another agent can reproduce the small corpus and benchmark report from the
  documented commands and fixture revisions.

## Deliverables

Resolved CI PR, corpus and runner, baseline report, deterministic-order contract,
and final integrated A–C validation evidence.
