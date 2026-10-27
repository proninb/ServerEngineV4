# PLS10-C13 — String Endpoint Path Reuse

2026-10-25. **NO ADOPTION.** Correct bounded one-entry memo, no demonstrated
PUBLISH speedup. C10 remains RETAIN; production V2 unchanged. No Graph API/ABI,
slot-origin callback, Source semantics, batching or additional endpoint index.

## Feasibility and chosen scope

V2 (`source_parser_v2.cpp:363–422`, `2014–2056`) uses an unordered_map keyed by
root type, array type and the complete typed prefix, holding individual element
handles. Its limits are 8192 prefixes and array bounds up to 256. Porting this
dictionary would introduce the keyed structure excluded by C13's boundary.

Within one assignment, different indices are different paths. Graph exposes
individual canonical handles, not an API guarantee that element handles form a
consecutive range. A fresh statement therefore cannot infer other handles from
its first element's ID. Reuse must refer to handles actually obtained before.

C13 tests **one last-eligible-prefix memo**, not a dictionary, hash table, dense
index or endpoint → slot map. Its key comprises actual resolved root type,
resolved element type, full typed prefix (all step fields), and bound. Root and
prefix determine array type in the immutable Header domain. It stores up to
256 individual handles returned by `intern_resolved_endpoint_path()`.

The key is checked inside encode(), after the existing temporal reference
resolution. WHO is not part of the path key and is never reused: the returned
endpoint always uses current resolved WHO. Intermediate reference encodings
with a different value type bypass the memo. Key changes clear handles; arrays
over 256 and allocation failures fall back to C10 interning. Capacity is reused.
Handles live only with their ordered committer/Graph; no reset/removal occurs
within this lifetime. No consecutive-ID assumption or raw Graph span is retained.

Only the private C13 generated path changes. C10 source/executable and production
dispatch were not modified. Native counters/verification are compiled out. The
separate audit validates every hit against the Graph path's actual root, value
type and steps without calling intern a second time.

## Correctness

**864 oracle runs + 8 cold boundary/growth runs PASS**:

* Existing C7 oracles plus temporal string rebindings across different WHO and
  different subobjects, prefix alternation and equal member offsets across types.
* Memo off/on, fast/fallback, audit, 1/2/4 workers and reversed scheduling.
* Exact fixture artifact bytes, errors, warning order and origins across modes.
* Bounds 256/257 (memo/fallback), including repeated writes and zero filling.
* Memo handles reused after hundreds of intervening new typed paths and Graph
  storage/index growth; cold persisted and native storage bytes match C10.

Full UnitProXL compiled/runtime digests match C10 after every invocation. V2 has
its own artifact domain. Native object storage matches across original C10,
off/on, audit and V2 over 2116048327 bytes:
`cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.
Cold scans and hashes run outside PUBLISH timing.

## Exact audit: insufficient adjacent locality

| Counter | Memo off | Memo on |
|---|---:|---:|
| String statements | 15899 | 15899 |
| Eligible string element encodes | bypass | 508768 |
| Memo hits | 0 | **11776** |
| Memo misses | bypass | 496992 |
| Key initializations/changes | 0 | 15531 |
| Hits verified against Graph | 0 | 11776 |
| Graph path-intern calls, all Source | 1216586 | 1204810 |
| Source C++ allocation calls | 993193 | 993195 |
| Cumulative requested bytes | 1010237464 | 1010237624 |
| Peak live requested payload | 190142428 | 190142588 |

Only **2.31%** of string intern calls are eliminated (0.97% of all Source intern
calls). Approximately 97.69% of string statements initialize/change the one
eligible key rather than reusing the previous prefix. The two memo vectors add
two allocations / 160 requested bytes on this corpus; no bound fallback or cache
allocation failure occurs in UnitProXL. Boundary fallback is covered separately.

C11's 92.82% Graph path hits describe previously existing canonical paths across
the corpus; they do not establish adjacent-prefix locality. Most reuse is too
far apart for this one-entry memo. All other 11 WHO/Graph operation counters
match C10, including all 705238 slot applies and origin callback opportunities.
The reduction in interner calls is exactly the verified memo-hit count.

Allocation numbers are from a separate C10-derived count-only audit, not timed
performance evidence. Requested payload excludes malloc/mappings and pre-Source
allocations; it is not process RSS or guaranteed retained storage.

## Frozen full PUBLISH A/B

Results: `build/parallel-local-source-10-c13/ab-01/summary.json`, `samples.json`,
`audit.json`, per-run logs and frozen binary hashes. Same UnitProXL path,
MSVC Release + IPO, matching C10 core, four parsing workers/window eight,
15 rotated/reversed blocks. C10 endpoint scratch stays on in every candidate arm.
Original C10, memo-off, memo-on and V2 are measured in one series. No concurrent
build/test/audit during timing; all samples retained. No C11 clocks or hot audit
counters are present in the native executable.

| Arm | PUBLISH median |
|---|---:|
| Production V2 | 1347.57 ms |
| Original C10 | 1415.18 ms |
| C13 memo off | 1407.09 ms |
| C13 memo on | 1412.28 ms |

Positive paired gain means faster candidate:

| Comparison | Paired gain | Bootstrap 95% CI | Wins |
|---|---:|---:|---:|
| Memo off → on | **-0.82 ms** | **-19.83–+8.61 ms** | **6/15** |
| Original C10 → memo on | +1.36 ms | -6.44–+13.13 ms | 9/15 |
| Original C10 → memo off | +10.44 ms | +1.43–+15.50 ms | 14/15 |
| V2 → memo on | -63.68 ms | -75.78–-52.77 ms | 0/15 |

The regenerated disabled candidate itself differs from original C10, so a small
absolute median difference cannot be credited to reuse. The decisive same-binary
off/on control does not establish a benefit. Its ordered-stage gain is -3.27 ms,
CI -8.13–+5.11 ms, 5/15 wins. Peak WS off/on has paired gain -253952 bytes,
CI -462848–+573440 bytes; no RSS improvement is established.

Absolute V2/C10 gaps must not be compared against another experiment's times;
the fresh measurements do not show a regression caused by changing C10 (it was
not changed). The confidence interval includes zero for the reuse hypothesis;
no further sample extension or alternative cache design was used to chase a win.

## Decision

**C13 = NO ADOPTION.** Do not integrate this memo into C10 or production. It is
retained only as a reproducible experiment. C10 remains the experimental baseline.

The existing API can safely reuse previously returned handles, but a one-entry
design has too little locality here. Widening it into V2's dictionary would be a
different indexed-cache experiment and is not silently adopted. Neither this
result nor the count reduction justifies changing G, slot callbacks, warnings,
temporal reference behavior or batching.

Reproduction: `experiments/parallel-local-source-10-c13/README.md`.
