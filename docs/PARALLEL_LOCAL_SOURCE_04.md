# PARALLEL-LOCAL-SOURCE-04 — File-local Lifetime

## Contract and isolated change

PLS03 flat canonical index is enabled in both control and candidate. The only
semantic-path difference is the lifetime of local file columns: the candidate
releases them after the file's last event and error check, before processing
the next file. Control holds all locals until global linking completes.
No alias-linking, LWW, provenance, materialization or scheduling change.
Production PUBLISH is unchanged.

The generated PLS04 binder takes mutable local models, sorts pointers once by
source order, then releases each consumed model's vectors/string via empty
swaps. It clears its current-local pointer immediately. It never constructs a
new Parser state on release. `model = {}` was rejected during preliminary
allocation audit because the default model allocates its initial scope; this
added 3,373 unwanted allocations. The preliminary series was stopped and is
excluded from final results.

Bound typed paths own their array entries, construction values contain owned
bits/IDs, and origins contain file IDs/ranges. Canonical strings have been
interned. Cross-file extern/type/identity state contains IDs and origins,
not pointers into local arenas. Local selector/name/value syntax is not read
again. Source tokens remain released before the global linker starts.

## Measurement

Normal executable is Release+IPO. One executable, `PLS04_EARLY=0/1`, both with
`PLS03_MODE=1`. Allocation audit is a separate build; only its main uses `/GL-`
to avoid the MSVC 14.51 allocation-observer LTCG ICE. PLS03 detailed per-call
instrumentation is disabled, leaving coarse alias phase clocks unchanged.

Lifecycle starts with Header/lexical preparation ready. It includes extraction,
Source token release, global linking, all local destruction, origin validation,
alias/reduction, fingerprint consumption and bound/final destruction. It
excludes oracle checks, profile analysis, report I/O and Header/lexical setup.
Construction G, strings, identities and input files retain the PLS03 lifetime.
This is neither full PUBLISH nor final G materialization.

Local destruction is separately timed. Candidate per-file destruction remains
inside semantic linking and lifecycle; the final locals-vector teardown is
also included. Destruction time is never subtracted from the performance result.

Windows memory measurements:

- `WorkingSetSize`: actual current resident working set (RSS equivalent),
  including shared pages. Sampled by a separate observer thread plus phase
  boundaries. Requested polling interval is 2 ms; scheduling/timer granularity
  may extend it. Sample counts are saved. These peaks are lower bounds.
- `PeakWorkingSetSize`: OS process high-water mark, saved before and after the
  semantic experiment. Includes earlier Header/lexical preparation; it cannot
  be reset per phase and is reported separately.
- `PrivateUsage`: private committed bytes, not resident memory.
- Audit allocation payload: requested bytes, calls, frees and peak live
  requested bytes, with original allocation-phase ownership. Excludes
  pre-existing input/Header allocations, allocator metadata and stack. Audit
  RSS/timing include observer headers and are not normal-build evidence.

Both timing arms use the same memory sampler. Five additional pairs compare
the frozen PLS03 flat executable against the PLS04 control, to bound combined
observer/code effects. No heap purge or working-set trimming is performed.

## Correctness

Four CTest suites pass, 184 fixture invocations: normal/allocation-audit builds
and control/early modes, each with forward/reverse extraction. Existing fixtures
cover alias chains/cycles, nested targets, repeated writes/bindings, warnings,
negative diagnostics, origins, arrays/strings/enums, visibility and Project DFS
order independent of file IDs.

The corpus runner additionally checks all 3,373 files were released, and their
released column capacity totals 182,960,089 bytes in the early mode; control
releases none inside the binder. It checks the UnitProXL fingerprint,
705,238 writes, 367,144 bindings, zero linker token reads and retained origins.
Forward/reverse runs use the independent V2-compatible projection oracle.
Changed alias-conflict semantics are checked by fixtures, not by the old oracle.
This retains the PLS02/03 supported grammar and fail-closed limitations; it does
not establish complete production Source grammar coverage or Runtime parity.

## Reproduction and artifacts

See `experiments/parallel-local-source-04/README.md`.
Final series: `build/parallel-local-source-04/measurements-final/`.
Includes executable hashes, each run's report/memory/phase profile, alternating
pair order, CSV and bootstrap summary. Preliminary `measurements/` results from
the discarded default-model release are not part of this experiment's result.

## Final paired results

15 alternating AB/BA pairs, same timing executable, Release+IPO. Intervals are
percentile bootstrap 95% intervals for median paired savings, 10,000 resamples,
fixed seed. Positive savings mean lower candidate time/memory. Independent
medians must not be substituted for the paired statistic.

| Timing, ms | Control median | Early median | Paired saving | 95% interval | Lower in pairs |
| --- | ---: | ---: | ---: | --- | ---: |
| Full experimental semantic lifecycle | 1,321.31 | 1,319.09 | −4.69 | −40.20…23.33 | 6/15 |
| Global semantic linking (includes early destruction) | 297.59 | 314.60 | −17.74 | −23.71…−14.50 | 3/15 |
| Alias/reduction | 707.16 | 715.45 | −5.19 | −32.73…9.73 | 6/15 |
| Local destruction, file + final tail | 19.80 | 17.87 | 2.02 | 0.85…3.00 | 12/15 |
| Bound/final destruction | 6.03 | 6.25 | −0.08 | −0.51…0.47 | 6/15 |
| External process wall | 5,978.33 | 5,959.62 | −16.07 | −87.37…69.58 | 6/15 |

Candidate file destruction median is 17.71 ms; its final locals-vector teardown
is 0.12 ms. Control does all local destruction at the tail, 19.80 ms. These
separate medians need not sum to the median total. The increase of the semantic
timer largely reflects moving destruction inside that timer; the full lifecycle
already includes both locations. There is **no demonstrated lifecycle speedup**.

| Resident/committed memory, MiB | Control median | Early median | Paired saving | 95% interval | Lower in pairs |
| --- | ---: | ---: | ---: | --- | ---: |
| OS entire-process peak working set | 653.87 | 607.61 | 45.82 | 44.64…47.43 | 15/15 |
| Sampled semantic-lifecycle peak working set | 617.95 | 606.02 | 12.48 | 11.34…24.20 | 15/15 |
| Sampled linking peak working set | 617.95 | 456.07 | 163.18 | 158.29…168.35 | 15/15 |
| Sampled peak private committed memory | 740.45 | 720.55 | 20.42 | 16.58…52.33 | 15/15 |

OS process peak before the experiment is around 365–366 MiB, below both final
peaks. The entire-process high-water mark therefore demonstrates a real
resident-peak reduction, not just the earlier array-capacity estimate. It still
includes all earlier process activity and differs from a resettable stage peak.

The sampled high-water mark misses short spikes: only 92/91 active-phase
samples (control/early medians) were collected. Windows scheduling stretches the
requested 2 ms interval. The exact OS process peak is consequently higher than
the sampled maximum, especially in control; do not equate the two measurements
or use the sampled stage values as exact peak bounds from above.

Sampled phase medians show the remaining pressure clearly:

| Phase working set, MiB | Control | Early |
| --- | ---: | ---: |
| Extraction | 518.41 | 515.24 |
| Linking | 617.95 | 456.07 |
| Post-link local-release boundary | 616.95 | 432.51 |
| Alias/reduction | 605.00 | 606.02 |
| Consumer | 459.52 | 459.04 |

Thus the 147.5 MiB capacity-overlap estimate from PLS03 was a useful linking
estimate, not a prediction of the whole-process peak. Early release removes
linking overlap; the alias-stage working set is essentially unchanged and now
limits the full lifecycle memory gain.

## Allocation and retained-result audit

Forward and reverse audit results agree exactly on counts/bytes and peak live
payload for each mode:

| Audit metric | Control | Early |
| --- | ---: | ---: |
| Allocation calls during ready-input lifecycle | 4,918,502 | 4,918,502 |
| Cumulative requested bytes | 1,652,382,385 | 1,652,382,385 |
| Peak simultaneously live requested bytes | 434,184,087 | 391,386,230 |
| Tracked requested bytes still live after bound/final release | 20,890,220 | 20,890,220 |
| Bound column capacity before reduction | 187,667,640 | 187,667,640 |
| Final column capacity | 39,195,108 | 39,195,108 |

The 40.82 MiB live-payload reduction is not an RSS measurement. Remaining
tracked allocations belong to construction G/strings, whose lifetime was not
changed. Allocation count and cumulative requested traffic are unchanged: the
candidate changes when memory dies, not how much semantic work is allocated.

All four UnitProXL oracle runs pass with fingerprint
`13649286433448276567`. Exactly 3,373 local models and 182,960,089 bytes of local
column capacity are released inside the early binder; origins remain valid.

Observer calibration (five pairs): frozen PLS03 flat lifecycle median 1,310.31
ms versus PLS04 control 1,335.87 ms; paired saving −34.78 ms, interval
−92.93…49.21 ms. This does not establish zero observer overhead or a directional
effect. The main A/B pays the same observation overhead in both arms. Historical
PLS03 timings are not a fresh speed comparison.

## Decision and next boundary

PLS04 is successful for memory/lifetime and correctness, **not an acceleration
claim**. Keep early release available as an isolated experimental candidate;
do not integrate it or the new semantic pipeline into production PUBLISH.
The latest experimental lifecycle is about 1.32 seconds and still excludes
Header/lexical preparation and G materialization.

File-local release already removes most avoidable local/bound overlap during
linking. Streaming/ownership transfer into compact global columns may still
reduce traffic, but cannot be justified solely by that old overlap estimate.
The next memory/cost audit should address retained bound columns plus alias
scratch at the alias-stage peak, where locals are already gone. Any compact
representation or streaming candidate should be separate, preserve owned
origins/values/typed paths, and retain the current flat index and alias contract
for its A/B. Adding extraction workers remains outside this experiment.
