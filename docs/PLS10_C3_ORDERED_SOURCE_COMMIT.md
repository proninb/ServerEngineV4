# PLS10-C3 — Parallel Parsing / Ordered Direct Commit

Date: 2026-10-25. Implemented as an isolated native PUBLISH candidate.
Production V2 dispatch remains unchanged. Full UnitProXL acceptance/performance
is **not established** because its historical fixture violates the required
file-local declaration rule.

## New semantic baseline

Reference traversal uses the binding at the current operation, not a future or
final binding. For `a.ref=b; a.ref.x=10; a.ref=c;`, the final state is b.x=10,
c.x unchanged, a.ref->c. Binding replacement takes effect immediately before
the next operation. Traversal through an unbound reference fails at that point;
later declarations/bindings are not borrowed to repair it.

This intentionally supersedes PLS02–PLS10-B final-binding semantics. Old late
rebind/fixed-point fixtures are not automatically parity oracles for this path.
The new reference fixtures encode the user's corrected temporal contract.

## Implemented pipeline

1. Existing declaration-order DFS planner through project.json, groups and
   nested Projects produces an ordered vector of Source tasks. file_id is only
   identity, never the commit sort key.
2. A fixed worker pool extracts local syntax models independently, with no
   shared String Table, WHO creation or Graph mutation. The existing extraction
   grammar is reused; it is not parsed again during commit.
3. A bounded ring contains at most twice the actual worker count in-flight file
   models. Commit waits for the next configured file, consumes its existing
   event sequence and destroys the model before advancing the window.
4. Ordered commit resolves names/types/endpoints and immediately mutates G.
   C2's existing target-index callback always accepts the next Source operation.
   Only the previous file/Source range is retained by initialization slot.
5. Reference binding immediately uses `rebind_resolved_link()`. Previous origin
   is indexed by the returned stable link slot. Current binding traversal uses
   the existing link target index via a minimal read-only `link_at_target()`
   wrapper. No extra endpoint or binding index is built.
6. SourceMap contributions are added while the file's root is active; duplicate
   warnings contain previous/current file and Source range in commit order.
   Header defaults are not previous Source writes.

There is no sorting of operations, no winner comparison by event_id, no final
binding stabilization, no bound_model instance, no project-wide resolved values
array and no separate Writer. Per-event endpoint/path scratch is cleared and
reused. The original local model still carries syntax positions; these are not
used to select winners. A small extern catalog remains for object declaration
validity and deterministic unresolved-extern diagnostics, not cell reduction.

Workers share input tokens read-only. Tokens are released after workers join;
the bounded window does not claim to bound total acquired input/token memory or
the size of a single file. Workers never write G. Commit remains single-threaded.

## Correctness tests

MSVC Visual Studio 2026 Release + IPO. Native CTest **1/1 PASS**, containing
**88 PUBLISH checks** (22 fixtures x four scheduling modes): requested 1 worker,
2 workers with reverse-window dispatch, 4 workers, and 4 with reverse dispatch.
Actual workers are capped by task count. Multi-file fixtures exercise real
parallelism and the 21-file case wraps the eight-slot window repeatedly.

Independent cold SHM checks verify expected scalar values, defaults and native
reference pointers. compiled.bin/runtime.bin digests and exact warning origins
must match across scheduling modes; this also checks persisted identity and
SourceMap determinism. Hashing/oracle scans happen after the PUBLISH timer.

Coverage includes writes before/after rebind, 1->2->3 diagnostics, nested
references, explicit extern across files, extern before definition, nested
Project order, member/nested defaults, arrays, strings, inheritance, enum/const,
empty files, queue wrap, first-file error priority and negative configurations.
No worker completion order changes values, artifacts or first diagnostic.

C2 slot regression suite was rebuilt and rerun after adding the read-only link
query. Python generator/test/runner compilation and diff whitespace checks pass.

Logs and detailed fixture artifacts:

- `build/parallel-local-source-10-c3-tests.log`
- `build/parallel-local-source-10-c3/fixtures/summary.json`
- `build/parallel-local-source-10-c3-slot-regressions.log`

## UnitProXL preflight: unsupported input, no A/B result

The runner freezes the executable and copies the historical PLS08 UnitProXL
fixture. V2 cold preflight passes the accepted 2,116,048,327-byte object-storage
SHA-256 `cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.

The candidate fails in `Books/UnityProXL/UPX_FWM/FWM.ogd` at
`bool& FWM_S21; FWM_S21 = S[21][0];`: S is defined in globals.source but is not
declared/extern in the consuming file. This is rejected intentionally under the
user's file-local TYPE/extern requirement. A declaration in a different Source
does not implicitly import the object into this file.

A separately labeled diagnostic copy prepended explicit extern declarations for
S and SW to Source files. It then failed in FWM_MODEL.ogd at a use of
FWM_ECPU_HSBY, another cross-file name without a local declaration. The input
adaptation is recorded in input-adaptation.json; original projects were untouched.
No further implicit declarations were synthesized and visibility was not relaxed.

Artifacts: `build/parallel-local-source-10-c3/ab-01` and `ab-explicit-01`,
with corresponding `build/parallel-local-source-10-c3-run*.log`.
Both runs stopped before paired timing. Their isolated cold timings are **not**
performance comparisons. No PUBLISH speedup or UnitProXL semantic parity is claimed.

## Limits and next boundary

This is a supported-grammar experiment, not a production replacement. Assign
inputs and Source paths through union storage fail closed. Seed Graph targets
must use the same normalized single-member encoding; alternate raw keys are
rejected rather than silently creating/missing a second semantic cell. No
BUILD/REBUILD/LOAD, persistent schema or runtime format changes were made.

The remaining full-corpus prerequisite is a fixture/project whose Source files
explicitly declare their imported objects, or a separately agreed visibility
contract for declarations inherited through book/include context. Do not weaken
the current rule or label old final-binding results as new-contract parity.

Once input compatibility is resolved, the existing runner can perform cold
storage checks, 1/4-worker artifact parity and 15 paired full PUBLISH runs
against V2 in the same executable. No additional semantic model or Writer is
required to reach that benchmark boundary.

Reproduction: `experiments/parallel-local-source-10-c3/README.md`.
