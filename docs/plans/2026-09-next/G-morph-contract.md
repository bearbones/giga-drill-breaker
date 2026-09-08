# G — Optional: honest sequential morph semantics

## Priority and outcome

Independent, lower-priority package. Dispatch only if transformation work matters
now. Success means each pass observes preceding edits and dry-run computes the
same final transformation without modifying source files.

## Evidence

TransformPipeline.cpp collects all pass replacements against the original files
and applies them at the end. The README describes passes as building on each
other. Actual final writes and JSON rules parsing already exist; some AGENTS.md
TODOs are stale. Inspect implementation instead of rebuilding completed features.

## Work

1. Add a two-pass fixture where pass two matches only the result of pass one.
2. Execute passes against staged contents, preferably a virtual file overlay or
   isolated staging tree. Reparse between passes and keep offsets tied to the
   correct intermediate version. Dry-run must follow the identical pipeline.
3. Treat conflicting replacements and parse/application failures as errors.
   Validate every staged output before beginning final writes. Define atomicity
   and rollback limits explicitly; do not claim a multi-file transaction without
   implementing one. Detect source changes between staging and application.
4. Emit a reviewable final diff or structured edit plan. Update README/AGENTS.md
   to the implemented contract and remove stale TODOs only after verification.

## Ownership and boundaries

Own morph pipeline/rules integration and transformation tests. Avoid changes to
anneal/megascope or a new transformation language. Coordinate build registration
edits with other agents if needed.

## Acceptance

- Pass two sees pass one's edits; dry-run leaves original bytes unchanged and
  predicts the applied result exactly.
- Conflicts, parse failures, multiple files, repeated execute calls, and concurrent
  source edits have deliberate tested behavior.
- Final output reparses; staged failure does not leave partially transformed
  source. Any final-write failure recovery limitation is documented and tested.
- Relevant tests and supported LLVM CI pass.

## Deliverables

Correct sequential/staged execution, focused integration tests, honest CLI/docs
contract, and a reviewable example transformation.
