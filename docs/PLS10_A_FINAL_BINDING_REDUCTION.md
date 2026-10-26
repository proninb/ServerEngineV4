# PLS10-A — Final Binding Reduction

2026-10-25. **Step A implemented and measured; B/C remain unimplemented.**
Production V2, G ABI/persistence format, Header type catalog, Runtime and worker
counts are unchanged. No new Graph, Semantic Linker or persistent operation
history was introduced. BUILD/replay is out of scope.

## Exact scope

The new kernel computes the final reference-binding winner table from the
existing already-typed input. It reads const G and uses the existing canonical
path/reference primitives; it does not look up unknown Source names later or
build another type catalog. Its temporary state contains operation IDs, not a
second transport model of all writes/endpoints/origins under a different name.

For this isolated A/B, the existing typed-input adapter, value reduction and
Writer remain in the publication harness. **The complete path still contains
bound_model and the old value/Writer stages.** Removing these is PLS10-B/C, not
a result claimed here. Existing extraction is sequential in this measured
harness; the local task format is reused and no Source worker pool was added.

Object visibility remains the control adapter's existing policy. The finalizer
accepts resolved WHO/type/path operations, not Source syntax. The required local
TYPE/extern input rule must be enforced by the eventual PLS10 typed producer;
this A-only harness is not presented as a new complete frontend implementing
that stricter rule. New dependency fixtures use explicit local declarations and
an explicit cross-file extern case. Existing legacy fixtures are retained as
control cases, not silently assigned a new visibility policy.

## Binding kernel

`binding_reduction.hpp` has no Graph dependency. It holds:

* One dense winning-operation ID per direct reference target, selected by Source
  event order, plus a list of occupied cells.
* Only when indirect binding targets exist, a context overlay with current/next
  winning IDs, generation stamps and a list of touched cells.

Direct bindings require neither `bindings = direct` nor a whole-catalog next-map
copy. Reference source chains and cycles still use the proven B1 indexed
cache/visiting state. Types and object registrations are not rediscovered.

For dependent targets, every operation resolves against the previous binding
context. Only after all target evaluations is the sparse next delta committed.
If a target moves away, its previous cell restores the direct winner or becomes
unbound. Reference cache/visiting resets, iteration bound, source ordering and
first unresolved/nonconvergence diagnostic retain the established semantics.
There is no copy of all direct winners per round.

### Why not promise one pass for every nested target?

With complete Header type `struct N { N& next; };` and declared objects a/b:

```cpp
a.next = a;
b.next = a;
b.next.next = b;
```

both `(a.next -> b, b.next -> a)` and `(a.next -> a, b.next -> b)` satisfy a
simultaneous final-target winner equation. The existing direct-seed policy
selects the former. Another input:

```cpp
a.next = a;
b.next = b;
a.next.next = b;
```

oscillates: redirecting a.next moves the indirect target, whose removal restores
the original a.next. The existing nonconvergence error must remain. This is a
dependency between known objects/references, not an unknown Header type or an
undeclared object being accepted for later lookup.

A replaces the node-based whole-catalog mechanism, but does not falsely claim
that all such simultaneous dependencies have disappeared. Sparse context
stabilization is retained only for the indirect-target subset.

## Two independently measured variants

* **A1**, `PLS10_FINALIZE=1`: dense/sparse binding winner reduction; unchanged
  binding diagnostic projection for comparison.
* **A2**, `PLS10_FINALIZE=2`: A1 plus a proven no-conflict fast path. For
  direct-only targets, if N operations produce N unique final binding targets,
  there cannot be duplicate Source bindings. Skip the second canonicalization
  pass and diagnostic heads/history allocation entirely. Conflicting or nested
  cases retain the original diagnostic projection in this slice.

The independent A1 series was frozen/completed before A2 was built or timed.
Do not infer an incremental millisecond gain by subtracting results from the
two series; there was no direct paired A1/A2 latency comparison.

`PLS10_FINALIZE=0`, `PLS09_INDEXED=1` is the B1 control. All variants share one
optimized native PUBLISH executable per series; dispatch is outside the kernel.

## Correctness and dependency audit

All 17 CTest suites pass. They exercise the original Source/writer/negative/native
resident cases in three modes; context retry, state growth, stable seed,
oscillation/nonconvergence, explicit file-local externs, exact warnings/origins
and canonical numbering are also checked.

Fresh UnitProXL cold runs for A1 and A2 preserve:

| Invariant | Result |
|---|---:|
| Objects | 137,584 |
| Final values | 705,238 |
| Final bindings | 367,144 |
| Semantic fingerprint | 13649286433448276567 |
| Ordered cell ID → WHO/type/path fingerprint | 7605832117458648793 |
| Linker token reads | 0 |

Independent query/native pointer checks cover every final value and binding.
All 2,116,048,327 object/default storage bytes retain accepted SHA-256
`cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.
Every timed run retains exact same-contract compiled.bin/runtime.bin digests.
Cold validation, history scans, SHA and allocation observers are outside timers.
Native acquisition, persistence, runtime.bin, resident SHM and cleanup remain
inside PUBLISH. The old diagnostic zero-initialized ~2 GiB vector is not timed.

On this corpus:

* **367,144 / 367,144 binding targets are direct**: no dependent context rounds,
  no indirect target evaluations, no overlay allocation.
* All final Source binding targets are unique. A2 creates no binding conflict
  state and performs no duplicate-target canonicalization pass.
* The final binding table's vector capacities total 3,732,872 bytes (~3.56 MiB).
* **32,816 / 705,238 writes (4.65%)** use a reference-dependent target in the
  unchanged write projection. This is the measured opportunity for step B,
  not evidence that B has already stopped storing other writes.

## Allocation evidence, separate diagnostic executable

| Metric | B1 | A1 | A2 |
|---|---:|---:|---:|
| Whole alias allocation calls | 1,200,114 | 465,843 | **98,683** |
| Binding catalog allocations | 367,209 | 117 | 117 |
| Binding catalog copy allocations | 367,145 | 0 | 0 |
| Binding diagnostic-head allocations | 367,159 | 367,159 | **0** |
| Canonical requests | 1,806,670 | 1,806,670 | **1,439,526** |

A2 removes 1,101,431 allocation calls and 367,144 canonical requests relative to
B1. No canonical cells/results are discarded or renumbered: removed requests
were repeated lookups of already known direct binding targets.

Cumulative alias requested payload drops from 345.90 to 286.04 MiB (−59.86 MiB).
Peak tracked alias live payload drops from 147.29 to 134.45 MiB (−12.84 MiB).
A1 and A2 have the same measured tracked peak: the removed diagnostics scratch
was already destroyed before the later alias high-water mark. Tracked retained
payload and flat-index capacity are unchanged. These are requested C++ payload
figures, not process RSS or whole-pipeline storage. Observer timings are not
used for the native acceleration claim.

## Full native PUBLISH measurements

Each series uses 31 predeclared alternating B1/A pairs, followed by a separate
15-pair production V2/A comparison. Bootstrap95 is 20,000 resamples of paired
gains (control minus A). No slow pairs are excluded or pooled between variants.

### A1: catalog reduction alone

| Metric | B1 median | A1 median | Median paired gain | Bootstrap95 | Faster |
|---|---:|---:|---:|---:|---:|
| Native PUBLISH | 1,916.79 ms | 1,788.36 ms | 127.71 ms | 101.06…152.05 ms | 30/31 |
| Alias interval | 361.67 ms | 235.15 ms | 126.91 ms | 119.59…136.74 ms | 31/31 |

The separate V2/A1 series has V2 median 1,296.44 ms and A1 1,850.32 ms; paired
regression 559.26 ms, CI 501.35…585.93 ms, A1 faster 0/15.

### A2: catalog reduction and no-conflict diagnostic fast path

Frozen native executable SHA-256:
`62943eda3dd340e4108b856f7ca8fa3aa3a49f03c19bcefcb6d47e6b95e440ef`.

| Metric | B1 median | A2 median | Median paired gain | Bootstrap95 | Faster |
|---|---:|---:|---:|---:|---:|
| **Native PUBLISH** | **1,850.35 ms** | **1,665.40 ms** | **190.52 ms** | **184.13…202.54 ms** | **31/31** |
| Alias interval | 345.93 ms | 153.87 ms | 193.14 ms | 189.62…196.91 ms | 31/31 |
| Operation + resident teardown | 2,143.18 ms | 1,956.30 ms | 181.79 ms | 163.42…207.02 ms | 31/31 |

Operation includes construction cleanup. Operation-plus-teardown adds resident
Project destruction but excludes intervening reporting/cold checks, as in PLS08-C.
Process peak remains about 2.57 GB, with a paired B1/A2 peak difference CI crossing
zero. Do not claim a process-memory reduction from the transient payload result.

Separate V2/A2 series:

| Metric | V2 median | A2 median | Median paired regression | Bootstrap95 |
|---|---:|---:|---:|---:|
| Native PUBLISH | 1,212.83 ms | 1,656.34 ms | **443.83 ms** | **394.50…468.58 ms** |
| Operation + teardown | 1,517.81 ms | 1,928.31 ms | 425.21 ms | 384.86…467.90 ms |

A2 faster 0/15 versus V2. These are separate pair populations; do not compare
independent medians across sessions or subtract the new gains from historical
1,884/3,090 ms results.

One separate phase-only profile has Source 1,060.74 / 848.37 / 369.09 ms for
B1/A2/V2, with Header ~203 / 203 / 201 ms and resident ~190 / 189 / 198 ms.
These are individual diagnostic profiles, not another paired stage estimate.
The remaining overhead is still the experimental Source route.

## Decision and next boundary

PLS10-A2 is a useful experimental final-binding kernel. Direct winners plus
the uniqueness proof remove work, rather than merely moving all old records into
a renamed model. Production V2 is unchanged and remains faster.

Still outstanding:

1. **PLS10-B**: direct-write winners plus sparse reference-dependent writes,
   preserving warnings from all operations that converge on the same final cell.
   Current A still uses the original write transport/reducer as an oracle.
2. **PLS10-C**: direct commits and SourceMap via existing G APIs, removing the
   separate Writer and full bound transport from the new path. No new ABI or
   permanent operation-history tables.

There is no claim of a complete PLS10 pipeline or subsecond PUBLISH yet.

Reproduction: `experiments/parallel-local-source-10-a/README.md`.
Raw samples, frozen binaries and summary JSON: `build/parallel-local-source-10-a/ab-01/`
(A1) and `ab-a2/` (A2). Tests: `build/parallel-local-source-10-a-tests.log`.
