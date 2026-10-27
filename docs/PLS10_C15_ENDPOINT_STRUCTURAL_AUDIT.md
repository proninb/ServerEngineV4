# PLS10-C15 — Endpoint Preparation Structural Audit

2026-10-25. **Audit complete; tested candidate NO ADOPTION.**

The C10 extraction → binder chain was traced in
`parallel-local-source-02/extract.hpp`, generated C10 `ordered_binder.hpp`,
`source_endpoint_resolver.hpp`, and `identity.cpp`. No new cache/index was added.

## Required and repeated work

1. Extraction deduplicates spelling into local name IDs, stores qualified
   components with scope and source ranges, and encodes member/index selectors.
   It does not assign global WHO or resolve members against mutable object state.
2. name() maps a local name ID to string_id using its existing per-file table.
   C14's 87.2% hits are not string interning. Hoisting every name earlier would
   change interning/diagnostic order and process potentially unused names.
3. symbol_entity() applies visibility and name kind. It cannot be skipped merely
   because a token hash matches: lookup depends on scope/kind/current declarations.
4. begin_endpoint() obtains object type and resets scratch. Member/index walking
   checks intermediate type, inheritance, bounds and physical diagnostic range.
   Index selectors are already numeric; they are not reparsed in ordered commit.
5. prepare_member() failure precedes member name interning; changing that order
   can change the first error. C7's domain/owner checks also determine whether
   the later typed view is safe for union/inheritance cases. They are not removed.
6. save() copies typed steps into owned event scratch. This copy protects the LHS
   when RHS endpoint_request() resets C10's endpoint buffer. A borrowed view alone
   is not an equivalent change for reference assignments.

A direct repeated operation exists for successful declarations. declare() calls
identities.resolve(parent, name, object, who), then registers/completes that object.
apply() immediately called symbol_entity() for the same declaration just to pass
WHO to sink.declaration(). Extraction constructs each declaration symbol from
the same scope and a single object-name component. The WHO is already available.

## Single candidate

Change the private declare() return from void to identity_ref and use that WHO
for provenance. Original extra lookup remains selectable in the same executable.
No failures are suppressed: declaration errors throw before returning/consuming
WHO. Scope, types, member traversal, ordering, Graph API, origin policy and
SourceMap remain unchanged. Native hot counters are absent.

The cold audit verifies every returned WHO against identity kind, record parent,
record name and local declaration symbol. It uses record access, not another
find(), so it does not reintroduce the eliminated find for counting purposes.

## Correctness and work counts

816 oracle runs PASS: direct/lookup, fast/fallback, audit, worker/reverse ordering,
temporal references, errors, origins, duplicates and lifetime. Added cases cover
same object name in root/namespace scopes and namespace reopening with extern
completion. All fixture bytes, first diagnostics and warnings match between modes.

Full UnitProXL audit:

| Counter | Extra lookup | Direct WHO |
|---|---:|---:|
| Declarations / WHO checks | 137584 | 137584 |
| identity_space::find | 1169037 | 1031453 |
| identity_space::resolve | 137584 | 137584 |
| Source C++ allocations | 993193 | 993193 |

Exactly 137584 finds are removed. Other WHO/Graph operations remain unchanged,
including 1216586 path interns, 705238 initialization applies and 367144 link
rebinds. Persisted digests match C10 per run; V2 has its own artifact domain.
Full 2116048327 native object-storage bytes match all arms with SHA-256
`cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.

## Frozen uninstrumented A/B

`build/parallel-local-source-10-c15/ab-01/summary.json`, `samples.json`, audit and
individual logs; MSVC Release + IPO, same UnitProXL path, 4 extraction workers /
window 8, C10 scratch on. Fifteen rotated/reversed four-arm blocks, all samples
retained, no concurrent builds/tests. Hashes/scans are outside measured PUBLISH.

| Arm | PUBLISH median |
|---|---:|
| Production V2 | 1346.23 ms |
| Original C10 | 1410.50 ms |
| C15 extra lookup | 1415.95 ms |
| C15 direct return | 1423.69 ms |

| Paired comparison (positive gain = faster candidate) | Gain | Bootstrap 95% CI | Wins |
|---|---:|---:|---:|
| Extra lookup → direct return | **-3.72 ms** | **-28.76–+13.06 ms** | **7/15** |
| Original C10 → direct return | -24.59 ms | -29.69–+6.85 ms | 6/15 |
| Original C10 → extra lookup | -9.99 ms | -32.02–+10.71 ms | 6/15 |
| V2 → direct return | -83.98 ms | -92.88–-71.11 ms | 0/15 |

The ordered-stage off/on gain is +5.98 ms, CI -10.31–+8.53, 9/15 wins; it does
not override the full PUBLISH comparison. No established acceleration. Absolute
times are not compared against earlier series; no extra rounds were added to
chase a positive result.

## Decision

C15 found and removed real repeated work, but **NO ADOPTION** of this candidate:
no measured PUBLISH benefit. Keep it as a reproducible source-level result, not
a new C10 baseline. The existence of redundant code does not establish a useful
critical-path optimization. Do not add another cache/index to this candidate.
Production V2 and accepted C10 remain unchanged.
