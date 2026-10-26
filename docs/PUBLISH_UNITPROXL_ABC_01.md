# UnitProXL PUBLISH reference — A/B/C and execution-mode control

2026-10-26. The large historical/current timing discrepancy is reproduced by
changing **default tool sandbox versus unsandboxed execution**. There is no
demonstrated large historical V2-versus-modern-control code regression in the
controlled unsandboxed series. Earlier 1928.16 → 1659.42 ms / 13.9% reader
results were obtained inside sandbox and must be scoped to that environment.
The unsandboxed reduction in this series is approximately 3%.

## Frozen reference and controls

All arms used the exact original
`build/parallel-local-source-10-c9/ab-01/fixture/project.json`, its files and the
same `.serverengine/project.json` artifact directory. The complete 10,770-file
input manifest, including ordered project.json bytes, is checked before/after.
Original compiled/runtime files were backed up and restored after the series.

| Arm | Executable | Mode |
|---|---|---|
| A | Historical `ab-01/baseline.exe` | `PLS08_CANDIDATE=0` |
| B | Frozen `PublishFastAcquire02.exe` | `FAST_ACQUIRE_02_REFERENCE=1` |
| C | Same executable as B | `FAST_ACQUIRE_02_REFERENCE=0` |

A SHA-256: `841c7848aaf4631fef25c8bf6eb28de7cac072f9b01dd66dcf0cae7b0a1adde5`.
B/C SHA-256: `1a356c04c7d0458a35c1be877b5374f120417d79a663ecf4fd347c105ec448ab`.

Inherited CW/PLS/experiment variables were removed and controls set explicitly.
Separate process per operation. ABI windows_x64, pack 8, FIXED_DIRECT, fixed VA
0x10000000000 match in available mains. Historical SHM name contains the PID;
modern name is constant. Sequential processes have no name collision. Neither
binary was rebuilt. A's trailing argument is the exact requested `0` (skip type
assertion); B/C require `24027`. That assertion is outside timing. The harness
checks all counts: 24,027 types / 137,584 objects / 367,144 links.

Available historical build descriptions specify the same four Source V2 flags,
MSVC toolset v145, /O2, /GL / IPO and imported PLS08Core.lib. Modern B/C are
MSVC 19.51 Release + IPO, PGO OFF. Generated main/parser, caches, vcxproj and
current library hashes are retained in `current-build-provenance.json`. These
are current file observations, **not proof of original historical link inputs**.
The available generated source and output confirm V2 selection, not complete
instruction-level algorithm equivalence between historical/current binaries.

## Unsandboxed A/B/C

15 native blocks, after warmup, cycling all six A/B/C permutations. Native means
no internal phase timers. Every observation retained; no concurrent builds/tests.
The internal total_ms ends before resident teardown in all arms.

| Arm | Native PUBLISH median |
|---|---:|
| A, historical V2 | 1354.65 ms |
| B, modern metadata reader | 1354.13 ms |
| C, modern fast reader | 1313.58 ms |

Paired gain = first arm's total minus second arm's. Bootstrap 95% CI, 20,000
resamples of block differences, deterministic seed:

| Pair | Median paired gain | 95% CI | Second arm wins |
|---|---:|---:|---:|
| A → B | 7.58 ms | −26.95 to 21.61 ms | 9/15 |
| A → C | 35.92 ms | 26.68 to 64.36 ms | 13/15 |
| B → C | 36.70 ms | 34.93 to 62.29 ms | 13/15 |

A/B have no demonstrated difference. Same-build B/C isolate the reader switch.
Their marginal medians differ by 40.55 ms, 2.99%; paired median is a different
statistic. A/C support a whole-executable improvement in this series, not
exclusive causal attribution to reader.

Separate seven-block unsandboxed profile, ms:

| Phase median | A | B | C |
|---|---:|---:|---:|
| Total | 1353.67 | 1352.38 | 1319.46 |
| Acquisition / lexical preparation | 135.541 | 135.657 | 90.533 |
| Semantic | 738.852 | 749.234 | 754.292 |
| Persistence | 206.292 | 202.335 | 205.640 |
| Resident | 213.761 | 215.647 | 208.481 |

B→C paired profile gain: 29.56 ms, CI 16.30–58.59 ms, wins 6/7. A/B phase
structure is similar. Do not sum phase medians as an exact native decomposition.

## Same-binary execution-mode crossover

First direct probe of the requested historical command: sandbox 1814.18 ms,
unsandboxed 1269.89 ms. Then seven alternating native pairs changed only the
outer tool execution mode; command, cwd and cleaned child environment match.

| Historical A | Default sandbox | Unsandboxed |
|---|---:|---:|
| Native total median, 7 pairs | 1992.93 ms | 1395.82 ms |
| Profile total median, 3 separate pairs | 1945.42 ms | 1334.68 ms |
| Profile acquisition / lexical median | 620.966 ms | 137.614 ms |
| Profile Semantic median | 841.177 ms | 739.436 ms |
| Profile Persistence median | 206.076 ms | 203.027 ms |
| Profile Resident median | 202.157 ms | 203.255 ms |

Unsandboxed wins 7/7 native pairs. Median paired sandbox cost 550.41 ms,
95% CI 519.06–666.96 ms; individual pair costs 434.97–757.80 ms. Absolute
times drift, hence paired controls rather than isolated absolute comparisons.

This identifies an execution-environment effect, **not its internal OS
mechanism**. IsTokenRestricted returns false in both modes. Do not claim a
Windows restricted-token bit specifically caused the slowdown. Which sandbox
mediation/security interaction accounts for each ms has not been established.

## Correctness and limitations

All 66 timed A/B/C operations and 13 additional checks/warmups match exact
compiled/runtime bytes. Common SHA-256:

- compiled.bin: `b30bdd279af2472096243b99360d02ac70e6337af556c1a9f33739ea5fbafb67`
- runtime.bin: `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`

Compiled byte equality includes persisted G, numbering and SourceMap; runtime
equality includes persisted physical plans. A artifact LOAD/audit and B/C
PUBLISH/LOAD produce common logical SHM SHA-256
`61334dee4dd897389b17658ff54092df60d62c54024856673b4567db60ed9711`.
Historical A lacks a compiled-in live SHM digest hook: **A's own in-process SHM
was not directly hashed**. Common-loader parity is the available check.

Process kernel/user CPU, page faults and system busy CPU time are retained for
each A/B/C observation. They cover full process lifetime including teardown,
not PUBLISH-only CPU. Native median page faults: A 835632, B 835521, C 835682.
Hardware: Ryzen 9 9900X, 24 logical CPUs, Windows 11 build 26200. Active power
plan/settings retained. Dynamic CPU frequency, individual competing-process
attribution and cold-cache behavior were not measured. No cache purge or manual
affinity/priority changes. Registry MHz is not a dynamic-frequency measurement.

## Retained evidence and reproduction

`build/publish-unitproxl-abc-01/unrestricted/` contains commands, manifests,
binary hashes, every stdout/stderr, samples, summary, checks, artifact backups
and verification. `token-crossover/` contains seven native and three profile
pairs with launch-mode labels, commands and observations.
`frozen-binaries/` retains additional SHA-256-checked copies of A and B/C.

Run outside sandbox with a **new** output directory:

```powershell
python experiments/publish-fast-acquire-02/abc.py build/publish-unitproxl-abc-01-new unrestricted 15 7
```

`token_probe.py` runs one historical observation. Its caller must actually
select the sandbox/unsandboxed tool mode: its label alone does not change mode.
`abc_report.py` summarizes probes and captures current build/host provenance.

No Parser, reader, production source or production settings were changed during
this investigation. Future ordinary PUBLISH comparisons should explicitly use
unsandboxed frozen controls and the same input manifest.
