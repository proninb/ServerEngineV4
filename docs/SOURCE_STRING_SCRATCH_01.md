# SOURCE-STRING-SCRATCH-01

2026-10-23. Implemented and validated as an isolated experiment. **Not enabled in production:** Source-stage savings are measurable in this series, but full-PUBLISH improvement is inconclusive.

## Change and lifetime

Apply the Header buffer-lifetime lesson to Source V2 without a new semantic model. `assign_string()` normally creates decoded-byte and endpoint-path vectors for every string assignment. The candidate owns these two vectors in the Source parser, clears them before each use, and retains capacity until parser destruction. Reference mode retains the original per-call vectors.

`string_literal()` is called only by `assign_string()`. No recursive string evaluation or borrowed buffer lifetime crosses assignments. G receives copied construction values and canonical endpoint handles; SourceMap receives targets and provenance. Neither holds references to scratch bytes or path components. Existing path-handle caching, batched Graph writes, bounds checks, zero-filled string tails, semantic order and warning behavior are unchanged. Capacity is not pre-reserved by array bound; the existing growth algorithm is retained.

`CW_SOURCE_STRING_SCRATCH_AB` and `CW_SOURCE_STRING_SCRATCH=0/1` exist only in standalone experimental targets. The production branch still uses local vectors. No global WHO/type database, Graph format change, or parallel dispatcher is introduced.

## Separate growth audit

Two full PUBLISH calls per mode using `SourceStringScratch01Audit`. All four calls process 15,899 string assignments and preserve both artifact goldens.

| Successful backing-buffer growths / requested payload bytes | Local control | Reuse |
|---|---:|---:|
| Decoded byte vector | 85,532 / 394,385 | 9 / 85 |
| Temporary path vector | 31,798 / 1,271,920 | 2 / 80 |
| Total | **117,330 / 1,666,305** | **11 / 165** |

Counters detect successful capacity changes around the two actual vectors, which correspond to new backing-buffer allocations in the installed MSVC STL. Bytes are new capacity times element size. This is not a whole-process allocator audit: allocator overhead, frees, unrelated allocations and failed requests are excluded. Requested bytes are cumulative, not peak or retained memory. Diagnostic times are not used for performance conclusions.

## Counter-free paired PUBLISH

MSVC Release /O2 /Ob2 + IPO, same executable, complete accepted post-SHA production object set with Source Parser and benchmark rebuilt. UnitProXL at the same `build/publish-subsecond-01/fixture/project.json` path in both modes. Existing Header/Source V2 production settings retained. No runtime worker/pretouch or scratch-growth counters enabled.

Two excluded warmup pairs, then 20 alternating measured pairs; 44 calls. No concurrent builds, filtering, or hash work inside the PUBLISH timer.

| Median, ms | Local control | Reuse |
|---|---:|---:|
| Full PUBLISH | 1290.850 | 1289.180 |
| Source | 417.863 | 412.3455 |

| Paired statistic | Full PUBLISH | Source |
|---|---:|---:|
| Median gain, ms | **0.680** | **7.5865** |
| Mean gain, ms | 6.769 | 6.97255 |
| Bootstrap mean 95% interval, ms | **[-6.1105, 23.2115]** | [0.84685, 13.5426] |
| Candidate faster | **10/20** | 12/20 |

Bootstrap uses 10,000 paired resamples with a fixed seed. Independent medians and paired median differences are distinct statistics. Full-PUBLISH variation is large enough that this series does not establish an overall speedup. Peak working-set medians are 2,573,641,728 versus 2,573,584,384 bytes; no meaningful memory reduction is claimed.

## Validation and artifacts

- Frontend regression suites pass with local buffers, reused buffers, and no experiment macro. Each includes the 22 Header V2 contract fixtures.
- New fixture alternates long, short, empty and adjacent string literals across different endpoint depths, verifies final Graph values after scratch mutation/destruction and overwrite-warning count. Existing tests cover inherited char arrays, escapes, invalid encodings and bounds errors.
- All 48 full calls (44 timing + four audits) preserve compiled SHA-256 `a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d` and runtime SHA-256 `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`, plus expected type/member counts. compiled.bin includes SourceMap; this is full artifact parity, not only value projection.
- Hash verification runs outside measured PUBLISH. No new production hashing is added.
- `git diff --check` and runner syntax compilation pass.

Raw results: `build/source-string-scratch-01/results-20261023/{manifest.json,measurements.csv,summary.json}`. Growth audit: `build/source-string-scratch-01/audit-20261023/`. Its original generic runner control label is 24, but `string_scratch_reuse=0` identifies local control; this does not change worker counts. Current timing labels are 0/1. Frozen executable/source hashes are stored per series. A later CMake addition adds only production-option-off tests and does not change the measured executable.

Reproduction: `experiments/source-string-scratch-01/README.md`. No production bridge or adoption is justified by the current full-PUBLISH result. Keep the experiment and regression coverage; retain production behavior.
