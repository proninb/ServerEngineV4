# CANONICALIZER-01

Isolated model/canonicalization experiment for ADR-39. It neither replaces the
production Parser nor constructs Local G. No algorithm is selected as the final
PUBLISH architecture by this experiment.

## Build and run

```powershell
cmake -S experiments/canonicalizer-01 -B build/canonicalizer-01/kernel `
  -G "Visual Studio 18 2026" -A x64
cmake --build build/canonicalizer-01/kernel --config Release `
  --target Canonicalizer01 Canonicalizer01ModelTests
ctest --test-dir build/canonicalizer-01/kernel -C Release --output-on-failure

build/canonicalizer-01/kernel/Release/Canonicalizer01.exe self-test
build/canonicalizer-01/kernel/Release/Canonicalizer01.exe `
  build/canonicalizer-01/unitproxl.trace index 8
```

Other algorithms: `reference`, `sort`, `hierarchical`. Other inputs:
`synthetic-flat`, `synthetic-nested`. Append `diagnostic` to collect allocation
and string-comparison counters; do not use those times as performance results.
The main matrix is reproducible with run_matrix.ps1; analyze.py summarizes it.

## Input and model

Local symbols carry local parent references, kind, immutable spelling views,
logical producer and semantic event order. Test inputs additionally carry an
expected legacy WHO/string_id oracle. Kernels never read the oracle field.
An import supplies a parent's captured scope context and has no creation event;
it cannot by itself create a numbered global entity.

All algorithms resolve parent equivalence before comparing child keys:
`(CanonicalParent, Kind, exact name bytes)`. They support different local
aliases for a reopened parent. Local indices, physical file IDs and worker
completion order are not semantic keys. Hashes only route keys; equality
checks the entire spelling.

The independent serial reference recursively canonicalizes parents with an
ordered exact-key map. The interchangeable kernels are:

* **sort**: parallel chunk sort and merge of exact keys within each depth,
  followed by equivalent-key grouping;
* **index**: parallel full-key partitioning, then independent exact-key indexes;
* **hierarchical**: sort local child runs, group already-canonical parents and
  merge their child runs. A wide root can leave one serial merge even when
  sorting its local runs was parallel.

Depth barriers are a deliberate correctness baseline for this experiment,
not a required barrier design for a future streaming Parser.

The timed input supplies raw names, not free precomputed name hashes. The
index kernel allocates its fingerprint workspace, hashes names in parallel and
releases that workspace inside canonical_ms. Sort/hierarchical kernels do not
pay an unnecessary hashing pass. Oracle verification computes its own hashes
outside the timed kernel.

One common numbering implementation is used for the reference and all kernels.
It computes the first actual creation event per class, sorts those events and
assigns dense slots with existing kind packing. String classes have their own
common first-intern numbering, including strings without an identity owner.
Lexical key ordering never determines WHO. Parent-before-child creation,
missing declarations and ambiguous event orders are checked.

Visibility checks run after canonicalization against first-creation events;
finding a class in the complete set does not make it visible earlier. Captured
queries check exact-scope lookups. Synthetic queries additionally exercise
ancestor lookup and shadowing: a later local declaration must not hide an
earlier visible ancestor at an earlier event.

## Real event capture

The optional top-level CMake flag SERVER_ENGINE_CANONICALIZER_TRACE defaults
OFF. It defines capture code **only** for ServerEngineV4PublishBenchmark, not
the server or test targets. Configure a separate capture build with that flag,
IPO and benchmarks ON, then set CW_CANONICALIZER_TRACE to an output path for
one PUBLISH. Capture hooks record successful intern/resolve events and valid
find queries, including missing results, scoped to the active construction's
String Table/identity_space. Other temporary tables are excluded.

The trace contains a versioned header, monotonic event numbers, Header/Source
root context IDs, actual intern spellings/results and resolve/find results.
A required footer records inventories. The reader rejects truncation, unknown
parents, ID conflicts and incomplete streams. Capture is diagnostic and its
PUBLISH duration must not be used as a performance comparison.

Two real corpora are used:

* UnitProXL: 194,071,917-byte event trace, 2,724,481 intern calls, 161,611 resolve
  calls and 1,579,453 exact-scope queries; 188,630 strings / 161,612 identities.
* A real ServerEngineV4 Header/Source fixture with reopened N, forward A and
  its definition, separate root A, extern completion and Source writes. It
  supplies actual cross-root identity duplicates missing in the large corpus.

The loader projects the captured streams onto **16 fixed logical producers**.
It deduplicates string occurrences locally and supplies imported parent context
from the captured declarations. This preparation uses the legacy transcript;
it does not prove that a new parallel Parser can discover that context. The
timed kernels receive identical local records for execution worker counts
1/2/4/8/16. Preparation is reported separately as input_ms, never treated as free
PUBLISH work or included in a claimed pipeline speedup.

Input normalization also validates the local forest and constructs depth/child
indexes outside the timed agreement kernels. The global event order is supplied
by the legacy transcript; constructing equivalent events in a future parallel
Parser is not implemented here. These are explicit experiment boundaries.

The synthetic datasets use 10,000 type classes duplicated across all producers:
one flat root and one distribution across 256 namespaces. Unused strings,
shadowing, repeated parents and visibility are covered separately by model
fixtures. Invalid parents/cycles, future visibility, Z-before-A numbering and
forced hash collisions are tested under shuffled record positions.

## What parity means here

Every real run validates all captured bindings and per-local-symbol legacy
numbers. It also serializes and byte-compares an independent oracle projection:
ordered strings with IDs/lengths/bytes, followed by WHO/parent/name-ID records.
This is **identity/string projection parity**, not a claim that a new parallel
Parser has produced the complete compiled.bin. No such Parser is implemented.
Definition compatibility, overload/access rules and language parsing remain
outside this isolated kernel. Existing PUBLISH remains their implementation.

Capture PUBLISH and ordinary PUBLISH separately retain the complete established
compiled.bin/runtime.bin golden hashes and pass an independent artifact audit.
Capture after the final diagnostic-build cleanup reproduced the original trace SHA.

## Measurements

The matrix launches a fresh process for each algorithm/worker/sample. Real
UnitProXL has seven measured repeats per configuration; the other corpora have
five. One excluded warmup round per corpus precedes measured rounds. Execution
order rotates and reverses. No builds or other tests run during timing.

Reported timings separate strings/entities canonicalization, common numbering,
input preparation and worker-pool startup. kernel_ms is canonicalization plus
numbering; it excludes parsing, trace decoding, validation, G and publication.

Counter-enabled runs are separate:

* tracked_peak_bytes: high-water requested bytes through the PMR resource for
  kernel/output/numbering arrays, map nodes and merge workspaces. Input storage,
  thread stacks, allocator bookkeeping and small control objects are excluded;
* input_peak_ws_bytes and kernel_peak_ws_bytes: native process peak working
  set before and after the kernel, including input and runtime overhead. A
  higher input-construction peak can mask kernel changes; subtracting these
  values does not recover exact temporary allocation size;
* string_comparisons: actual calls to full-spelling comparison;
* comparison_span_bytes: summed logical spans passed to memcmp, **not** a count
  of bytes physically inspected by SIMD/hardware;
* cross_worker_*_duplicates: sum of distinct declaring producers minus one
  per canonical class, excluding roots and imported-only aliases.

Allocation tracking and comparison increments can perturb scaling, which is
why diagnostic times are not used to rank kernels. Pool startup is also reported
separately and must be included when evaluating a nonpersistent worker design.

This prototype checks the identity model and agreement kernels. It establishes
neither full C++ binding correctness nor a lower bound in milliseconds for
production PUBLISH, and it does not justify subtracting its kernel time from
the current PUBLISH time.
