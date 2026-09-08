# Next major improvements: dispatch guide

Prepared 2026-09-08 from a repository review. These are implementation plans,
not claims that the proposed capabilities already exist.

## Objective

Make cross-TU answers dependable enough to support change-impact review, while
preserving the CLI-first workflow and measured performance gains.

## Packages and dependencies

| ID | Package | Start | Completion depends on |
|---|---|---|---|
| A | [Index freshness and provenance](A-index-provenance.md) | Now | — |
| B | [Call-site-accurate path analysis](B-path-analysis.md) | Now | — |
| C | [Shared result and completeness contract](C-result-contract.md) | After A and B publish interface proposals | A and B integrated |
| D | [Validation corpus and deterministic output](D-validation.md) | Now | Final cross-package validation after A–C |
| E | [Semantic diff and change impact](E-change-impact.md) | Fixture/design preparation now; implementation after C | A–C; D corpus |
| F | [Selective control-flow access](F-control-flow-performance.md) | Measurement/prototype now | Storage implementation after A; final parity after B/C |
| G | [Optional: morph pass semantics](G-morph-contract.md) | Independently, if desired | — |

For three agents, dispatch A, B, and D first. Dispatch C after A/B publish
their proposed interfaces, then E after those foundations land. F can measure
in parallel if a slot is available; its storage implementation is sequenced
after A to avoid competing snapshot formats. G is optional and lower priority.

## Shared execution rules

- Read the repository AGENTS.md and the assigned package. Work in a dedicated
  worktree and topical branch; never change another agent's active checkout.
- Start from current origin/main. At planning time, the inspected checkout was
  `feat/megascope-cli-golden` at `64bb186`; its PR #64 was still open. D owns
  resolving that PR. Do not independently cherry-pick or repair it elsewhere.
- Treat package ownership as responsibility, not a ban on necessary edits.
  Coordinate shared-file changes before implementing them. Keep mechanical
  registration/build-file edits small and identify them in the handoff.
- Publish a short interface proposal early: types, serialized fields, owning
  module, invariants, and compatibility behavior. Dependent agents can build
  fixtures and designs against it; do not integrate against an uncommitted draft.
- Do not expand into a daemon, general symbolic execution, whole-program alias
  analysis, or a rewrite of the complete storage engine.
- Keep CLI and MCP as adapters over shared query logic. Preserve existing
  payload fields where feasible; document intentional behavior changes.
- Self-review, run relevant tests and CLI goldens, and use a focused PR. Wait for
  the full supported LLVM CI matrix before merging. Recheck branch state and
  authorization in the dispatching session before any merge.
- Existing measurements in docs/megascope-cli-review.md are historical. Capture
  binary revision, toolchain, input, commands, and raw results for new claims.

## Shared-file ownership and integration

| Surface | Lead owner | Other packages |
|---|---|---|
| Snapshot metadata, version, invalidation | A | F waits for A before changing layout |
| Path traversal and exception semantics | B | C consumes completeness facts; E consumes witnesses |
| ToolContext, common result contract, adapters | C | A/B expose producer-side data before adapter integration |
| Corpus runner, CI goldens, benchmark reporting | D | Each package still owns its feature-specific tests |
| Diff/impact tools and baseline format | E | Consume A–C contracts |
| Read-only context access | F | Coordinate with A/B; preserve public query semantics |
| Morph pipeline | G | Coordinate only shared build/docs registrations |

Merge A and B independently, then C. D's CI repair and corpus scaffolding may
land early; its final validation PR follows A–C. E and F must rebase onto their
dependencies, rerun their own gates, and explicitly test the integrated result.
Snapshot format versions are allocated at integration time, not independently
by concurrent agents.

## Required handoff

Each agent reports: branch and exact commit/PR, completed scope, public contract
changes, verification evidence, limitations, shared-file changes, and remaining
dependencies. Leave reproducible commands and fixture paths in the repository.
An unresolved semantic or measurement gate means the package is incomplete;
do not hide it by weakening a test or silently narrowing a claim.

## Dispatch prompts

Use each prompt in a separate agent task with this repository available.

- **A:** Implement `docs/plans/2026-09-next/A-index-provenance.md`. Read the dispatch guide, use an isolated worktree, publish the provenance interface early, and deliver the tested freshness/provenance changes with a focused PR and handoff.
- **B:** Implement `docs/plans/2026-09-next/B-path-analysis.md`. Read the dispatch guide, use an isolated worktree, publish the path/completeness interface early, and deliver call-site-accurate analysis with adversarial regressions and a focused PR.
- **C:** Implement `docs/plans/2026-09-next/C-result-contract.md` after obtaining A/B interface handoffs. Own the shared result contract and adapter integration; preserve compatibility and verify incomplete results cannot become exhaustive verdicts.
- **D:** Implement `docs/plans/2026-09-next/D-validation.md`. Own PR #64's CI investigation and the validation corpus. Start independently, then validate the integrated A–C changes without duplicating their feature work.
- **E:** Implement `docs/plans/2026-09-next/E-change-impact.md`. Prepare fixtures now; build the semantic diff and impact tools after A–C land. Deliver stable, bounded results and positive/negative patch fixtures.
- **F:** Execute `docs/plans/2026-09-next/F-control-flow-performance.md`. Measure first and publish a go/no-go result; coordinate with A before storage edits and proceed only if the prototype passes the stated performance and parity gates.
- **G, optional:** Implement `docs/plans/2026-09-next/G-morph-contract.md` in an isolated worktree. Make sequential passes and dry-run behavior honest and consistent, with focused integration tests and no unrelated analysis changes.
