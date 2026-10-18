# SOURCE-V2-STRING-PATH-REUSE-01

## Scope

Source Parser V2 reuses canonical `endpoint_path_handle` values for repeated
`char[N]` initializations. The cache is a transient acceleration structure
owned by the Source parser. It does **not** change Graph, runtime layout,
persistence, or the Source Map.

## Feature switch

`SERVER_ENGINE_SOURCE_STRING_PATH_REUSE_V2` (CMake, default `ON`) defines
`CW_SOURCE_V2_STRING_PATH_REUSE_01=1` for all project targets. `OFF`
restores the original `G.intern_resolved_endpoint_path()` call per element.
No diagnostic printing, token-level clocks or normal-PUBLISH counters are
introduced.

## Algorithm and invariants

- Key: exact root object TYPE, array TYPE and the full sequence of
  `endpoint_path_step`s before the final array-index step.
- Hit: reuse the *existing* Graph handle for that index. Graph path identity
  is type + path; the object WHO is not part of the Graph path key.
- Miss: call canonical `intern_resolved_endpoint_path()` and record the
  handle only after Graph success. New handles are created in exactly the
  old order. Existing handles/WHERE are preserved.
- No changes to per-element `commit_construction()`, warnings,
  initialization/source-map dependencies, object WHO, or diagnostics.
- Cache limits: 8192 prefixes and 256 elements per array. Oversize or
  allocation failure uses the original Graph intern path.
- The cache lasts for a single `parse_source_roots_v2()` invocation.

## Accepted diagnostic A/B (prior to production integration)

Windows Release + IPO ON; 15 alternating A/B pairs; `compiled.bin` and
`runtime.bin` SHA parity passed on every run:

| Metric | Paired median candidate - baseline | Faster pairs |
|---|---:|---:|
| Source V2 | -18.736 ms | 13 / 15 |
| Full PUBLISH | -25.130 ms | 10 / 15 |
| PUBLISH minus compiled flush | -13.488 ms | 12 / 15 |

Diagnostic candidate reused 472256 of 508768 `assign_string()` element
handles (92.82%), leaving 36512 Graph path intern calls; 15899 string
assignments, 1141 distinct cache prefixes, zero fallbacks. These are
**measurements of the instrumented experiment**, not guaranteed production
performance. Use `run_production.ps1` for a fresh A/B of production code.

## Validation and promotion

Runner builds OFF/ON variants from the same detached checkpoint with
Release + IPO ON, runs CTest for both, checks binary SHA parity and produces
paired Source/PUBLISH measurements. Option `-CommitAndPushOnPass` commits
only the three allowed files and pushes the tested commit to the remote branch
if the promotion gate passes and the remote head still matches the checkpoint.
The main working tree is never edited by the runner.
