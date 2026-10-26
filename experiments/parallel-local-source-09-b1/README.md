# PLS09-B1 — Indexed Reference State

PUBLISH-only experiment. Only reference cache/visiting representation varies.
Production V2, binding catalogs/copy, duplicate heads, canonical index, traversal,
LWW, Writer, SourceMap, ABI, Runtime and worker counts are unchanged.

```powershell
cmake -S experiments/parallel-local-source-09-b1 -B build/parallel-local-source-09-b1
cmake --build build/parallel-local-source-09-b1 --config Release --target PublishReferenceStateB1 PublishReferenceAuditB1 SourceReferenceSemanticB1 ReferenceStateTestsB1 --parallel 8
ctest --test-dir build/parallel-local-source-09-b1 -C Release -R '^B1_' --output-on-failure
python experiments/parallel-local-source-09-b1/run.py build/parallel-local-source-09-b1/ab-new --pairs 15
```

If the initial interval is inconclusive, the independent fixed 31-pair replication
can reuse that exact frozen binary and private fixture (no new algorithm/build):

```powershell
python experiments/parallel-local-source-09-b1/repeat.py build/parallel-local-source-09-b1/ab-new build/parallel-local-source-09-b1/replication-new
```

Keep and report both series, including negative/slow pairs; the replication has
no early stopping or outlier exclusion.

The standalone project freshly builds current core via the existing experiments.
Generated aliases have two compile-time specializations selected outside alias
evaluation by `PLS09_INDEXED=0/1`. Legacy retains the original follow body and
containers, with no runtime strategy branch per lookup. Indexed specialization
does not construct unused map/set containers. Missing selector means legacy.
The native executable still supports PLS08's `PLS08_CANDIDATE=0` production V2
control. All publication occurs only in a private build fixture tree.

`reference_state.hpp` stores 12-byte entries indexed by canonical cell ID, not
global WHO. Resolved and visiting generations implement the paired cache/visiting
clear at every existing site, including caught unresolved operations. No pointer
or reference into the vector is held across recursion; callers re-index by cell
ID. Rollover resets the array before reusing generation 1. Exception behavior is
kept: failed traversals retain marks until the original context-clear site.

State tests cover cell zero, sparse IDs, growth across suspended frames, stale
cache/visiting context invalidation, generation rollover and comparison with the
original containers. Extra semantic fixtures force caught unresolved rounds,
deep recursion/vector growth and the unchanged depth-limit error. Existing Source
and Writer fixtures run both strategies with exact warning/origin checks.

The semantic driver adds a cold fingerprint over every ordered canonical cell's
ID, WHO, type and path. It also retains the old semantic fingerprint. These,
reverse extraction/physical insertion, expected native values/references and
full object/default storage parity are checked before timing.

The runner freezes three binaries, uses one executable for legacy/indexed/V2
native PUBLISH, verifies same-contract compiled/runtime digests after every run,
then performs 15 legacy/indexed pairs followed by 15 V2/indexed pairs. V2 has a
separate digest domain because its Source contract/representation differs.
Diagnostic allocation/counter runs and phase profiles are separate from timing.
No SHA, observer counters, history scan or artificial zeroed 2 GiB vector is in
the timed Source path. The existing native SHM materialization remains timed.

CTest's native expected-data script reuses the existing 17 PLS08 fixture projects
under build/parallel-local-source-08/writer-fixtures. Prepare the PLS08 tests first
on a fresh workspace. Other B1 fixture projects are generated independently.

The generated native driver is an actual publisher to its supplied directory.
Use the runner/private fixtures, not a live production project. Unsupported
Assign, union paths and scalar literal writes directly to references retain the
explicit PLS08 boundaries. No BUILD/REBUILD/LOAD implementation is changed.

Report: [PLS09-B1](../../docs/PLS09_B1_INDEXED_REFERENCE_STATE.md).
