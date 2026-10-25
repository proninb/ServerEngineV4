# PARALLEL-LOCAL-SOURCE-03 — Alias Cost Breakdown

## Scope and method

Isolated continuation of PLS02 on the same UnitProXL corpus. No workers,
materialization, production Graph or Source V2 changes. Source syntax is still
read once; global semantics and alias reduction consume owned local/bound data.

Two executables separate observation from timing:

- Release+IPO timing build: coarse phase boundaries only.
- Audit build: exact operation/allocation counters, path-reuse and lifetime
  audit, randomized 1/2048 inclusive method samples. Audit main uses `/GL-`
  because MSVC 14.51 ICEs with the global allocation observer under LTCG; the
  timing build retains IPO. Audit wall times are not performance evidence.

One candidate changes only the canonical-cell index: flat open addressing,
cached 64-bit hashes, exact WHO/path equality, one search except on growth.
Cell IDs and payload order retain first-insertion order. Final bindings,
reference cache, LWW heads, diagnostics, lifetimes and scheduling are unchanged.
PLS02 remains the baseline implementation.

Coarse phases deliberately preserve original loops: binding catalog includes
direct/indirect selection and convergence; reference resolution includes key
sorting, following chains and final binding output; write-target indexing
includes canonicalization and heads insertion. LWW includes winner/history
selection and value warnings; binding warnings are timed separately. These
are elapsed phase boundaries, not exclusive function CPU costs. Separating
every lookup with clocks would reproduce the observer distortion of earlier
Source experiments.

## Work counts (control audit)

| Operation | Count |
| --- | ---: |
| Writes / final value cells | 705,238 |
| Source bindings | 367,144 |
| Canonical cells, including intermediate/reference cells | 1,306,064 |
| Canonical requests | 1,806,670 |
| Direct / dereference-bearing requests | 1,773,854 / 32,816 |
| Cell-index find / insert API calls | 1,872,302 / 1,306,064 |
| Cell hash calls / step visits | 5,275,078 / 10,915,487 |
| Cell equality calls / compared steps | 3,019,525 / 989,591 |
| Follow calls / cache hits / misses | 399,960 / 32,816 / 367,144 |
| Actual typed traversal steps | 98,448 |
| Direct-path dereference scan steps | 4,586,124 |
| Binding lookup / cache lookup | 367,144 / 399,960 |
| Final-cell heads emplace API calls | 1,072,382 |
| Binding selection find / store | 367,144 / 367,144 |
| Cache store / visiting insert / visiting erase | 367,144 each |

These are exact API/work counts, not exclusive CPU samples. Non-cell map
counters do not include internal bucket probes or rehash work.

There are 1,345,416 distinct `(WHO, typed path, follow mode)` requests, 421,340
repeated groups, maximum 160 requests per key. All 32,816 dereference-bearing
paths are distinct. The existing cache already resolves the 367,144 selected
references once; later traversal uses 32,816 cache hits. Thus repeated full
alias traversal is not the dominant duplication on this corpus. Direct target
canonicalization, index insertion/rehash and node lifetimes are the stronger
candidate, without proving their individual exclusive CPU costs.

Rare method samples are inclusive, overlap and are sensitive to rehash outliers
and clock resolution. They must not be added or treated as a definitive split
of traversal versus key creation. Coarse stages and the clean A/B isolate the
practical cost of replacing the cell index.

## Allocations

| Alias/reduction audit | Control | Flat cell index |
| --- | ---: | ---: |
| Allocation calls | 3,945,691 | 2,639,633 |
| Requested bytes over lifetime | 393,281,424 | 418,603,358 |
| Peak simultaneously live requested bytes | 191,067,663 | 182,828,175 |
| Live requested bytes after alias-stage release | 39,195,225 | 39,195,225 |
| Flat-index capacity before release | 0 | 67,108,864 |

The candidate removes 1,306,058 allocations, but requests more bytes over its
lifetime due to geometric flat-table growth. It is not a claim that fewer
allocations imply less cumulative allocation traffic. Payload-only peaks
exclude input arenas, allocator headers/rounding, observer memory, G and RSS.
The final index is temporary and released before downstream consumption.

Control makes about 5.59 allocations and 7.48 canonical-cell hash calls per
final value cell, or 3.02 allocations and 4.04 hash calls per canonical cell.
These are aggregate ratios including binding work, not costs attributable to
one value write.

The candidate reduces cell hash calls to 1,839,486 and hashed step visits to
3,850,672. Actual traversal, cache hits/misses and canonical cell count stay
unchanged. The slightly different number of intern calls reflects routing
vector-path insertion through the same flat-table intern routine.

## Ownership audit (no lifetime change)

| Capacity measure | Bytes | MiB |
| --- | ---: | ---: |
| Local arrays | 182,960,089 | 174.48 |
| Bound arrays | 187,667,640 | 178.97 |
| Current maximum local + bound overlap | 370,627,729 | 353.46 |
| Estimated overlap releasing each consumed file | 215,945,859 | 205.94 |

The estimate includes the currently consumed file and its newly emitted bound
records. It is a capacity model, not a measured process peak or implemented
optimization. It excludes temporary structures and allocator retention.

Largest local columns: events 35,985,360 bytes, selectors 30,587,248,
values/literals 29,714,299, symbols 27,733,300, endpoint requests 23,316,160.
Largest bound columns: writes 58,792,664, typed paths 54,286,720,
endpoints 50,393,696, bindings 18,664,360.

Local selectors cannot simply be moved as typed paths: local names/origins
become resolved member/base/dereference indices and path lengths can change.
Events and decoded values also have different consumers/representations.
However, after a file is globally linked, its local names and literal buffers
are no longer needed; canonical strings and bound values/origins own their
data. Releasing file-local storage after last use is a separate next candidate,
not part of the measured flat-index change.

## Correctness and artifacts

All four CTest suites pass: normal/audit × control/candidate, 184 fixture runs.
They cover aliases, chains/cycles, nested bindings, repeated writes/bindings,
warnings, first diagnostics, origins, scopes, arrays/strings/enums and Project
tree order independent of file IDs, with forward/reverse extraction.

Four full UnitProXL audit runs (both modes × forward/reverse) pass the
independent V2 projection oracle and origin validation. Fingerprint is
`13649286433448276567`, linker token reads are zero. UnitProXL has zero
duplicate warnings; warning semantics are checked by fixtures rather than
inferred from that corpus. The V2 oracle covers its compatible projection,
not the intentionally changed winner semantics in alias-conflict fixtures.

Reproduction: `experiments/parallel-local-source-03/README.md`.
Raw run reports, profiles, executable hashes, alternating pair order and paired
bootstrap summaries are under `build/parallel-local-source-03/measurements/`.
Build artifacts are local and not source-controlled.

## Paired timing results

Fresh alternating AB/BA series, 15 pairs, one timing executable, no audit
counters, no oracle inside timed runs. Both modes retain identical ready-input
preparation and consumption. Confidence intervals are percentile bootstrap
intervals for the median paired saving (10,000 resamples, fixed seed).

| Metric, ms | Control median | Flat median | Median paired saving | 95% interval | Faster |
| --- | ---: | ---: | ---: | --- | ---: |
| Alias + reduction | 1,164.44 | 783.75 | 396.41 | 377.46–425.18 | 15/15 |
| Experimental semantic lifecycle | 1,804.55 | 1,416.37 | 401.45 | 352.08–417.33 | 15/15 |
| External process wall | 6,438.15 | 6,092.46 | 320.72 | 228.95–485.63 | 15/15 |

The lifecycle includes extraction, token release, linking, local release,
origin validation, aliases/reduction, fingerprint consumption and bound/final
destruction. Header/lexical preparation, oracle checks and report/profile I/O
are excluded. External wall includes those other activities and process costs,
so it is secondary evidence. This is not production PUBLISH or final G output.
The difference between medians is not the median paired saving.

Extraction stays 163.17 → 161.73 ms and global semantic linking stays
313.56 → 312.79 ms; neither is changed by the candidate.

| Coarse phase, median ms | Control | Flat |
| --- | ---: | ---: |
| Alias setup / Header seed | 0.02 / 0.00 | 0.02 / 0.00 |
| Final binding catalog / convergence | 157.34 | 101.78 |
| Reference resolution + binding output | 228.89 | 167.74 |
| Binding target canonicalization + heads | 109.77 | 83.82 |
| Binding diagnostics | 12.57 | 11.96 |
| Binding scratch cleanup | 27.28 | 27.18 |
| Write target canonicalization + final-cell heads | 379.52 | 223.18 |
| LWW + value diagnostics | 35.62 | 37.78 |
| Final value ordering | 28.19 | 28.89 |
| Write scratch cleanup | 37.53 | 37.70 |
| Diagnostic ordering | 0.001 | 0.001 |
| Alias-state release | 139.34 | 29.79 |

Stage medians do not sum to the exact total median. Some reference-resolution
time is canonical target/index work, not chain traversal alone. Material gains
in write indexing and alias-state release corroborate the chosen candidate;
the actual LWW loop is essentially unchanged.

Observer calibration: five fresh pairs of frozen PLS02 versus observed control
give alias medians 1,168.28 versus 1,172.57 ms. Median paired saving is 11.90 ms,
95% interval −19.06…76.96 ms. No directional alias-time observer effect is
demonstrated, but this small series does not establish zero overhead. Their
external process costs differ, including new lifecycle/report work, so that
comparison is not an observer CPU-cost estimate. Audit alias times are
1,613–1,616 ms for control and are explicitly excluded from performance claims.

## Decision

Keep the flat canonical-cell index as the successful **experimental PLS03**
candidate: about 33% lower alias-stage median and 22% lower experimental
lifecycle median, with unchanged semantics and final retained output.
Do not integrate production PUBLISH, add workers, or materialize G yet.

The main hypothesis is supported: expensive canonical-key/index maintenance
and destruction, rather than repeated full alias traversal or LWW itself,
account for a large removable cost. They do not explain all remaining time.
Next isolated candidate is file-local lifetime release, with measured process
peak and full lifecycle; the current memory estimate is not an implemented
benefit. Parallel extraction remains premature while linking/reduction still
dominates this experimental path.
