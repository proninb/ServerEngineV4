# Current V2 CPU sampling — 2026-10-23

Dataset: `build/source-cpu-sampling-02/capture-20261023-125921/`.
Frozen binary: `build/source-cpu-sampling-02/frozen-current-v2/`.
Production algorithms include lazy Header state and 4x member-name index growth.
Release /O2 + IPO and matching native PDB; no hot-path audit counters or A/B switches.
All 98 full PUBLISH calls passed compiled/runtime golden hashes.

## Quality and interpretation

Eight ETL blocks: matching captured module GUID/age, 1 ms sample interval,
zero ETW lost buffers/events, no truncated traces and no unresolved application frames.
359,920 target CPU samples; 22,491 recognizable Source samples across all eight blocks.
336 samples have no stack (0.0934% of target samples; worst-case 1.49% of Source count).
The strict zero-missing-stack gate is **not passed**. Broad hotspot ranking is usable
with these limitations; counts do not represent exact semantic CPU costs.
OS frames remain addresses; IPO inline removal and ICF-shared names limit attribution.
Filtering uses sampled block/PID pairs, without an additional process-lifetime filter.

All timing outliers are retained. Control mean/median total: 1447.68/1425.75 ms;
sampled: 1595.18/1559.69 ms. Paired block-mean observer deltas:

| Stage | Mean delta | Median delta |
| --- | ---: | ---: |
| Full PUBLISH | +147.49 ms | +146.95 ms |
| Header | +4.39 ms | +4.71 ms |
| Source | +12.50 ms | +11.88 ms |

Control block means range 1328.39–1542.23 ms, with large first-process outliers.
The observer differences combine ETW overhead and environmental drift; they are
not clean causal estimates or a V2 regression measurement. Do not compare this
series' median against previous A/B medians to infer an optimization effect.

## Source exclusive samples

| Group | Samples | Share of recognizable Source |
| --- | ---: | ---: |
| Graph writes | 4512 | 20.06% |
| Cursor | 2101 | 9.34% |
| String interning | 1923 | 8.55% |
| Endpoint | 1501 | 6.67% |
| Path interning | 1317 | 5.86% |
| Binding | 1059 | 4.71% |
| Member transitions | 971 | 4.32% |
| Provenance | 383 | 1.70% |
| Other (including native/STL leaves) | 8724 | 38.79% |

Exclusive samples partition Source; inclusive group counts overlap.
Largest clearly named individual leaves include `string_table::intern_prehashed`
(1574), `read_endpoint` (1501), `insert_link_target_index` (1442), `assignment`
(1280), `advance` (1033), `upsert_initialization` (967).

The apparent `vector<derived_type_record>::push_back` leaf (2275) has immediate
callers in endpoint path interning (1391), member append (692), and link writing
(136), among others. Its name cannot justify optimizing derived-type creation.

## Header

9197 recognizable Header samples. Top exclusive leaves: `bind_current` 2684
(29.18%), apparent `hash_derived` lambda 809 (8.80%), member index insert 648
(7.05%), `parse_type` 505 (5.49%), member index capacity 272 (2.96%).
806 of the lambda samples have immediate caller `derive_resolved`; this strengthens
attribution to that path but still does not establish which hash instructions dominate.
Historical member-index capacity share was 549/9588 (5.73%). The lower current
share agrees with the accepted rehash reduction; cross-series shares are not a
paired speedup estimate. The previous cursor cached-view candidate remains rejected.

## One next experiment

Investigate **Source link-target index growth only**, retaining the existing index,
WHO, link rules and Direct G path. `insert_link_target_index` contributes 6.41%
of recognizable Source exclusive samples, and its existing capacity routine rebuilds
the table on 2x growth. First separate new inserts from rehash inserts and count
probes/capacities in a diagnostic build. Then test a single alternative growth policy
without counters, with golden parity, failure/fallback checks, memory accounting,
and paired full-PUBLISH A/B. Do not assume the Header growth result transfers or
change endpoint/initialization indices in the same experiment.

## Reproduction

```powershell
python experiments/source-cpu-sampling-02/export.py build/source-cpu-sampling-02/capture-20261023-125921 build/source-cpu-sampling-02/reader/Reader.dll build/source-cpu-sampling-02/frozen-current-v2
python experiments/source-cpu-sampling-02/phase_report.py build/source-cpu-sampling-02/capture-20261023-125921
```

Raw reports: `analysis/cpu-summary.json`, `functions.csv`, `groups.csv`,
`phase-report.json`, per-block sample JSONL, metadata and trace-loss logs.
