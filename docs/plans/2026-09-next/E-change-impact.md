# E — Semantic diff and change impact

## Outcome

Compare before/after indexes and explain changed program relationships, then use
a Git diff to focus an investigation on affected functions and their callers.

## Dependencies and first milestone

Prepare patch fixtures and a CLI/result proposal now. Implement against landed
A–C contracts, using D's corpus format. The first release is graph/context diff
and bounded impact queries. Anneal finding deltas are a follow-up once a stable
finding export/identity contract exists; do not invent one implicitly here.

## Work

1. Define a deterministic snapshot-independent semantic comparison format for
   function identity, call edges, and relevant exception/lock context. Never use
   interner IDs or insertion order as cross-index identity. Treat file movement,
   line shifts, lambdas, and changed USRs explicitly; report ambiguous matching
   rather than guessing continuity. A line shift alone should not become a new
   program relationship.
2. Add a bounded diff query over two indexes: added/removed functions and calls,
   changed edge confidence/execution context, and changed protection/lock facts
   where supported. Explain build/model differences before comparing facts.
   Refuse or label comparisons whose incompatible coverage makes absence unclear.
3. Add impact traversal from changed functions/headers: affected callers and
   entry points with witness paths, depth/work budgets, and completeness metadata.
   Treat impact as a candidate affected set, not proof a test will fail.
4. Add an optional Git-diff adapter for explicit base/head revisions. Map changed
   source ranges to identities using available index facts; describe unmapped
   edits and fall back to a clearly labeled broader scope when necessary.
   Do not silently create/overwrite users' indexes or change their checkout.
5. Provide focused CLI examples: newly introduced route to a target, changed
   call-site protection, and callers affected by a changed function. Keep the
   query engine transport-neutral and the Git adapter separate.

## Ownership

Own new diff/impact modules, their schemas and registrations, patch fixtures,
and user documentation. Coordinate shared registry/build-file edits with C/D.
Do not reopen snapshot layout or replace the shared path engine.

## Acceptance

- Equivalent clean/warm/reordered bakes produce zero semantic differences.
- Whitespace-only changes and unrelated line shifts do not invent edge changes.
- Curated patches add/remove the expected edge or entry-to-target witness and
  change the expected protected/unprotected call site.
- Deleted/renamed/ambiguous identities and partial or differently configured
  indexes have explicit behavior and tests.
- Results have stable order, bounded output, inspectable witnesses, and truthful
  completeness. Diff and impact costs are measured on a nontrivial graph.

## Deliverables

Focused graph/context diff and impact tools, documented comparison identity,
patch-pair regressions, shell recipes, measured costs, and an explicit follow-up
for anneal finding baselines rather than an incomplete promise of support.
