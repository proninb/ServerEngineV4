# PLS10-C4/C5 — Project Visibility and the Need for a Catalog Barrier

2026-10-25. Production V2 remains selected. No persistence/G layout changes.

## Contract and experiments

The user's updated language contract makes Source objects project-visible;
extern is optional for cross-file visibility. Types must agree, unknown objects
and duplicate concrete definitions are errors. General forward visibility is
permitted by that language contract. Reference traversal remains temporal:
later binding replacement must not move earlier writes.

Two isolated candidates distinguish correctness of that contract from the
catalog/queue architecture needed by a particular corpus:

- **C4** registers all declarations before applying values/bindings. It supports
  genuinely future declarations, but its simple implementation holds all parsed
  file models until catalog completion. This is a prototype, not an assertion
  that a global barrier or this memory cost is fundamentally necessary.
- **C5** removes C3's file-local declaration restriction and otherwise preserves
  its streaming bounded queue. Objects already in G are usable without extern.
  It does not borrow future declarations. A future/unknown object stops this
  capability audit instead of waiting, looping or silently changing semantics.

C5 therefore answers whether *this corpus* needs future declaration discovery;
it does not fully implement the general forward-visibility language rule.

## C4 correctness and cost

C4 parses each file once. During registration it records WHO/type and validates
duplicate/type consistency, without evaluating declaration initializers or
updating reference bindings. Then it releases Source tokens and applies already
parsed events in project order, freeing each file model after use. Registration
errors are delivered at their Source event, allowing an earlier semantic error
to retain diagnostic priority. A conflicting definition does not get hidden by
a secondary missing-extern-definition error.

**136 native fixture runs PASS** (34 cases x four scheduling modes), including
cross-file use without extern, genuinely future objects/binding targets,
namespace scope, repeated extern, incompatible types, duplicate definitions,
unknown objects and declaration initializer order. The temporal C3 value/pointer
oracles and exact warning/scheduling parity are preserved.

Unmodified UnitProXL cold checks PASS at 1/4 workers. Full object storage matches
the accepted V2 oracle. The parsed model vector capacities total **182,960,089
bytes (~174.48 MiB)** before commit. This is retained payload capacity, not RSS.

C4 frozen native series: 15 paired full PUBLISH runs, V2 median **1624.76 ms**,
C4 **2202.73 ms**, paired regression **575.72 ms**, 95% bootstrap interval
**529.80..583.57 ms**, 0/15 candidate wins. **NO ADOPTION**.

## C5 forward-use audit: the important architectural result

The original UnitProXL fixture passes C5 with **3373/3373 Source files** committed
without a complete declaration catalog and without input adaptation. At every
object use, the object was already registered from Header, a prior Source file
or an earlier declaration/extern in the current file. No use required a later
undiscovered declaration on this supported-grammar execution path.

This is a direct execution audit, not a regex guess. Dedicated negative fixtures
prove that C5 rejects a genuinely future object; C4 accepts that same construction.
The C5 pass cannot be attributed to preregistration: it has no such phase and
does not retain later parsed models for catalog construction.

The prior C3 failures on S/SW and FWM_ECPU_HSBY were therefore caused by its strict
per-file extern requirement, not a need to discover future declarations.

**96 native fixture runs PASS** across 1/2/4 requested workers and reverse-window
dispatch. The original UnitProXL also passes cold runs with 1 and 4 workers:

- Same compiled.bin/runtime.bin across worker counts.
- 137,584 objects and 367,144 links.
- Exact SHA-256 across **2,116,048,327 object-storage bytes**, including defaults,
  padding and native references, matches V2:
  `cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.
- Four workers use an eight-file ring, observed peak ready count **8**.
- A consumed file model is destroyed before its window position is reused.

No Source files were changed or given synthesized externs. No full-project model
array, Alias Graph, resolved-value journal or separate Writer is instantiated.
The input/token storage and Graph itself still exist; a bounded parsed-file
count does not imply a fixed byte/RSS ceiling for arbitrary file sizes.

## C5 full-PUBLISH performance

Frozen executable, unchanged private copy of UnitProXL, same optimized native
executable for V2/candidate, alternating pair order, 15 pairs. Cold oracle scans
and file hashing are outside the PUBLISH timer. A core library built by C4 with
matching MSVC/IPO/Source flags was statically linked; the runner freezes and
hashes the final executable.

| Metric | Result |
| --- | ---: |
| V2 PUBLISH median | 1637.91 ms |
| C5 streaming PUBLISH median | 2028.98 ms |
| Paired regression | **383.58 ms** |
| 95% paired bootstrap interval | **365.96..396.93 ms** |
| C5 wins | **0/15** |
| Ordered commit + waiting median | 935.439 ms |

The last stage includes waiting for parsing and destruction of consumed models;
it is not a pure resolver CPU profile. This series proves no production speedup.
Do not subtract independent C4/C5 medians to claim a measured barrier-only gain.
No paired barrier-only comparison or Source-phase RSS audit was performed.

## Architectural decision

For UnitProXL, the complete parsing/catalog barrier and retention of every parsed
file are unnecessary. Prefer the bounded streaming architecture as the next
experimental baseline for this corpus. **Do not adopt it into production on
performance grounds**: it still loses materially to V2.

For arbitrary projects with true future declarations, a complete-catalog barrier
is only one possible implementation. A future waiting/discovery/spill mechanism
must separately establish progress when the needed declaration lies beyond the
bounded window, end-of-discovery handling of unknown objects, deterministic WHO,
type conflicts and first diagnostics. C5 does not pretend to solve that problem.
Workers must continue to avoid concurrent arbitrary mutation of G/bindings.

The next performance investigation belongs in ordered resolution/apply, not in
an assumed mandatory full-project model. Current binding lookup and value storage
already use existing Graph indexes and C2 slot metadata. No new winner index or
parallel Graph mutation was introduced.

Artifacts:

- `build/parallel-local-source-10-c4/fixtures/summary.json`
- `build/parallel-local-source-10-c4/ab-01/{summary,samples}.json`
- `build/parallel-local-source-10-c5/fixtures/summary.json`
- `build/parallel-local-source-10-c5/ab-01/{summary,samples}.json`
- Corresponding `build/parallel-local-source-10-c{4,5}-*.log` files

Reproduction and limitations are in each experiment's README. Python compilation
and diff whitespace checks pass. BUILD/REBUILD/LOAD and production dispatch are
unchanged; C3's stricter visibility report is historical and superseded here.
