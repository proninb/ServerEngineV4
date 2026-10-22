# Source ready-only tokens with batched strings (36)

This experiment combines `SERVER_ENGINE_SOURCE_TOKEN_ONLY_V2=ON` with
`SERVER_ENGINE_SOURCE_STRING_BATCH_V2=ON`. The baseline is experiment 35:
batched strings with ready tokens plus compact words. Both use Source lexer
tokens and parallel prehash. Header token-only, Source Model and overlapping
Header/Source preparation remain disabled.

The ready-only producer and mandatory parser input were implemented in 33;
no new token representation is introduced here. Source compact words are
not produced during PUBLISH. BUILD/REBUILD keep their compact lexical input.
Character initializations use the batch Graph API implemented in 35.

## Method

Native MSVC Release IPO, same disposable UnitProXL fixture. No builds or tests
run concurrently with timing. Six stage calls (first pair excluded), then an
excluded warmup block and eight mirrored ABBA/BAAB ordinary-PUBLISH blocks.
Both golden artifact SHA256 values and an independent baseline audit are
checked after every call. Full-operation inference uses paired block means,
not the two stage samples or the historical results from experiment 33.

## Initial validation

Frontend regression tests passed, including ready-only cursor consumption,
empty roots, long offsets and repeated string initialization. Nine integration
scenarios and sixteen real parser-error comparisons matched baseline; the
latter include string overflow, missing semicolon and a duplicate string
followed by an invalid member. Scripts reject invalid configuration fixtures.

Logs and reproducible scripts: build/publish-source-ready-batch-36/.
The complete combined-mode build passed all 22 CTest tests. Golden hashes
also matched after the final measured PUBLISH. `git diff --check` passed.

## Representation checks

Both variants consume 5,889,225 ready Source tokens and zero compact-cursor
tokens. The candidate stores zero Source compact words; baseline stores
5,889,225. Both process 15,899 string statements / 508,768 character records.
Header compact words remain 3,622,698 in both variants. These counters verify
the requested combination rather than a fallback to the previous mode.

## Results

| Metric | Result |
| --- | ---: |
| Ready + words + batch, ordinary PUBLISH mean | 1387.169 ms |
| Ready-only + batch, ordinary PUBLISH mean | 1379.549 ms |
| Paired saving | 7.620 ms |
| Student-t 95% interval, eight block differences | -5.955 to 21.195 ms |
| Winning paired blocks | 4/8 |
| Baseline stage Source mean (two samples) | 435.886 ms |
| Candidate stage Source mean (two samples) | 417.539 ms |
| Baseline peak working set (two stage samples) | 2,573,524,992 bytes |
| Candidate peak working set (two stage samples) | 2,549,934,080 bytes |

The measured peak reduction is about 22.5 MiB. The two stage pairs are
diagnostic; their larger timing saving does not override the ordinary-PUBLISH
comparison. Additional speedup from ready-only is not established. Keep
SOURCE_TOKEN_ONLY_V2 OFF by default and SOURCE_STRING_BATCH_V2 ON. The combined
mode is implemented and available in the candidate build for explicit use.
Historical means from 33 and 35 must not be subtracted from this experiment.

To build the combined mode:

```powershell
cmake -S . -B build/publish-source-ready-batch-36/candidate `
  -DSERVER_ENGINE_SOURCE_TOKEN_ONLY_V2=ON `
  -DSERVER_ENGINE_SOURCE_STRING_BATCH_V2=ON
cmake --build build/publish-source-ready-batch-36/candidate --config Release `
  --target ServerEngineV4PublishBenchmark
```
