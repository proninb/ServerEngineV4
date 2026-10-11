# ServerEngineV4 — HEADER-SYNTAX-V3-31

**Experimental V3-31: record-local scope chain for named base-type resolution.**

## Goal

Compare unchanged **V3-27** against **V3-31**, with separate sampled diagnostics for:
- lexical scope-chain preparation and `identity_space::find` on cache misses;
- named-type token recognition / cache probes;
- Graph lookup + typedef-intrinsic-alias classification;
- original base resolution, declarator shape, type-ref preparation and derived cache.

Do not interpret sampled timings as full-project exclusive durations. The ordinary paired A/B has **no per-field clock calls**. V3-27 producer, its direct member index, base-type resolver and derived cache are byte-for-byte unchanged from V3-27 ZIP.

## Candidate design

V3-26 already caches simple named types by `string_id` per record. On a cache miss, it calls `v316::lookup_type`, which repeatedly calls `identity_space::record` while walking parents.

V3-31 takes a record-local snapshot of the lexical ancestry when entering a record. **Each subsequent named cache miss still uses `identity_space::find(scope, name, identity_kind::type)` at each ancestor**, in the same order, but it no longer repeatedly fetches each parent from `identity_space::record`.

- No global name cache; no caching of unresolved names.
- Record boundaries reset the existing name cache and rebuild ancestry.
- If the owner unexpectedly differs, fall back to `v316::lookup_type`.
- `__int64`, multi-token builtins, qualified/complex syntax and all other fallback forms are unchanged.
- Successful lookups still classify typedef intrinsic aliases using the native Graph.
- Graph API, native Graph records, WHO/WHERE order, construction, persistence and production PUBLISH/LOAD are unchanged.

**Caveat:** prebuilding ancestry for all records has a CPU cost. The experiment may be slower. Only paired UnitProXL measurements can justify accepting it.

## Run — Windows PowerShell

```powershell
$dest = "$env:TEMP\header-syntax-v3-31"
Expand-Archive "$env:USERPROFILE\Downloads\header-syntax-v3-31.zip" $dest -Force
$root = "$dest\header-syntax-v3-31"

& "$root\run_v3_31.ps1" `
  -RepoRoot "C:\Boris\ServerEngineV4" `
  -ProjectPath "C:\Boris\ServerEngineV4\build\unitproxl-transfer-check\project.json" `
  -OutputDirectory "C:\Boris\ServerEngineV4\build\benchmarks\v3-31-run1" `
  -WarmupPairs 2 -Pairs 7 -SampleStride 32
```

The runner creates an **isolated CMake overlay** under OutputDirectory, with its own Graph API changes inherited from V3-25. It never edits the production working tree. The first run requires building; do not specify `-SkipBuild`. For another output directory, use `-SkipBuild -ExistingBuildDirectory <run1/_experimental_v331/build>`.

## Output files

| File | Data |
| --- | --- |
| `v331-pairs.csv` | V3-27 vs V3-31, alternating order, per-pair parity |
| `v331-phases.csv` | Total phase durations of independent sampled pass |
| `v331-type-samples.csv` | Base/shape/derived plus scope/name/alias breakdown; `SampleStride` controls records sampled |
| `v331-summary.csv` | Median V3-27 / V3-31, median paired saved, wins, gates |
| `v331-allocations.csv` | Current-thread global `new` calls and requested bytes |

`HEADER_GRAPH_PARITY_PASS` requires equivalent native Header Graph to the Header V2 oracle: types, members, canonical/derived types, constructions, WHO/WHERE, nested defaults. `full_publish_gate=PARTIAL`: Header parity is **not** full PUBLISH parity.

## Checked locally

- Clang C++20 mock end-to-end A/B parity: PASS.
- Field initializers and constructor override/repeat fixtures: PASS.
- Positive unit tests for scope lookup, named cache, cv ordering, typedef aliases, cross-owner fallback: PASS.
- Negative Graph parity gate: stops with `HEADER_GRAPH_PARITY_FAILED`.
- Mock allocation executable: PASS.
- Real native project headers used to syntax-check the entire benchmark translation unit: PASS.
- CMake configure against existing benchmark overlay: PASS.
- Baseline code equality against original V3-27 ZIP: PASS.

**Verified on UnitProXL (MSVC Release):** Run 1: 6/7 wins, +2.7311 ms median paired saved. Run 2: 15/21 wins, +2.2584 ms median paired saved, V3-31 median 129.835 ms. HEADER_GRAPH_PARITY_PASS. V3-31 accepted as experimental performance baseline. Full PUBLISH remains PARTIAL; production PUBLISH/LOAD unchanged.
