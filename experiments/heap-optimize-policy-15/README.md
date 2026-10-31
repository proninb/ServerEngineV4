# Heap optimization policy diagnostic 15

No production adoption, allocator change or new architecture. Real ServerEngineV4,
one separate Release+IPO / PGO OFF executable for OFF_, EACH and POL_.

## Frozen policy hypothesis

Prior HEAP-14 measured OFF samples (warmup excluded) have a largest adjacent
post-project.reset PrivateUsage gap from 38965248 to 202309632 bytes. The fixed
page-aligned midpoint is **120635392 bytes / 115.046875 MiB**. calibration.json
records the method and source counts. This is a workload-derived test threshold,
not a new release gate or a proven globally optimal/minimal frequency.

- OFF_: never call HeapOptimizeResources.
- EACH: call after every successful UNLOAD.
- POL_: call only if the measured PrivateUsage after project.reset is at least
  the fixed threshold. No clock/count cooldown, no unrecorded trigger tuning.
- All modes measure/log the same values in the same image. Invalid measurements
  invalidate analysis. API failure is recorded, not reported as released memory.

## Protocol fixed before results

Six independent sequential runs, each ten warmup cycles then 600 measured seconds:
**OFF_, EACH, POL_, EACH, POL_, OFF_**. OFF brackets the series; EACH and POL_
alternate. Same frozen fixtures/order/reconnect pattern, same TCP across both
UNLOADs, artifact/native SHM/value/state assertions, same first5/last5 per-fixture
128 MiB/16-handle gate. All environments originate from one driver snapshot,
with only the four-character SE_HEAP_OPT15 mode changed by the child harness.

External operation samples separately record TCP PUBLISH and LOAD latency,
PrivateUsage before/after and process PageFaultCount delta. The counter includes
soft/hard faults and concurrent transport activity; it is not ETW hard-fault or
fault-owner attribution. DWORD wrapping is handled by unsigned subtraction.
Full lifecycle intervals and UNLOAD latency remain separate from these operation
measurements. API time/faults/physical commit are logged after each successful
UNLOAD, including skipped POL_ decisions.

## Interpretation rules

Both POL_ runs must preserve the original memory gate and correctness; all called
API operations must succeed. OFF must reproduce the retained-memory problem to
support a remedy claim. Call fractions (calls/UNLOADs) measure reduced frequency,
not raw call totals across runs with differing throughput. Compare per-run fixture/
operation medians, tails and fault counts against OFF and EACH; two runs per mode
are not a high-confidence general performance or release certification.

Do not retune the threshold after seeing these results, substitute a new memory
window, hide failures with a cooldown, or claim globally minimal frequency from
one threshold. Production stays unchanged; release BLOCKED and PGO OFF.

prepare.py/prepare_pilot.py make fresh copies from the frozen source/prior harness;
pilot.py checks the four fixtures in every mode. driver.py runs the six long arms;
analyze.py requires all arms complete, checks exact trigger decisions, recomputes
the original memory gate and preserves per-operation metrics in comparison.json.
