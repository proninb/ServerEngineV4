# PLS05 — Bound + Alias Scratch Audit

Read-only diagnostic experiment, retaining PLS04 early release and PLS03 flat
index. No additional workers, materialization, production changes or new
optimization arm.

```powershell
cmake -S experiments/parallel-local-source-05 -B build/parallel-local-source-05
cmake --build build/parallel-local-source-05 --config Release --parallel 8 --target PLS05
ctest --test-dir build/parallel-local-source-05 -C Release --output-on-failure
python experiments/parallel-local-source-05/run.py build/parallel-local-source-05/measurements --pairs 5
python experiments/parallel-local-source-05/summarize.py build/parallel-local-source-05/measurements
```

The normal target is an unchanged generated PLS04 early-release baseline with
Release+IPO. The separate audit main uses `/GL-` and allocation-tagging
allocators to attribute requested payload bytes to individual bound columns,
output vectors and scratch containers. Original first-insertion order, hash
algorithm, load factor, equality, lookup, convergence and LWW stay intact.

Allocator tags are stored in allocation headers and survive moves/frees. Every
new alias-stage payload high-water mark saves the simultaneous live-by-owner
composition, including unclassified G/string/lifecycle data as `other`. This is
requested payload, not RSS or allocator overhead. Rehash/vector growth can
temporarily retain old and new buffers and is included in this ledger.

Stage checkpoints snapshot live allocation payload, bound size/capacity and
current working set. Observer logs and cell/path role-analysis buffers are
excluded from the allocation ledger, but still affect audit RSS/timing. Audit
times and RSS must not be used to rank production costs. Use normal-build
coarse clocks and compare its fresh baseline against frozen PLS04.

Role masks: value=1, final binding target=2, final binding source=4, warning=8.
The analysis counts union roots and unrooted cells plus distinct referenced
array slots for paths/writes/bindings. It is a current-output reachability audit,
not proof that storage can be freed earlier during linking or IDs renumbered.
Path slot counts are physical array positions, not semantic path deduplication.

The runner requires existing UnitProXL fixture/oracle and frozen PLS04
executable at its configured paths. It runs normal/audit forward/reverse oracle
checks, five fresh baseline calibration pairs and five observer-cost pairs.
Fingerprint/origins, zero token reparse, early release and allocation-ledger
totals are checked. Use a fresh output folder.

See `docs/PARALLEL_LOCAL_SOURCE_05.md` for dependency and last-consumer analysis.
