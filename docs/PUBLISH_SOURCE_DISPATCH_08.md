# Source assignment dispatch

Source V2 previously resolved an existing object name again as a type and,
on a miss, as a namespace before deciding whether the statement was an
assignment. The new dispatch peeks at the next token first. `.`, `[` and `=`
unambiguously select assignment syntax and avoid both extra identity searches.

Other tokens retain the type/namespace disambiguation needed by declarations
such as `Signal Signal; Signal other;`. Lookahead uses the existing token buffer;
the following advance consumes the same token. There is no Source model,
parallel binding, retained syntax representation or new cache. Endpoint
validation, canonical Graph mutations and provenance remain on the original
assignment path. Only Source V2 (PUBLISH) uses this dispatch; BUILD/REBUILD use
their existing semantic backends. PUBLISH USN/flush policy is unchanged.

## Validation

- All 22 CTest tests passed in Release.
- Added a Source V2 regression for object/type/namespace name collisions,
  subsequent declarations, member assignment, scalar assignment and array
  assignment in the same Source root.
- Nine existing integration scenarios passed, including matching diagnostics
  and artifact absence for the expected failure case.
- All 60 full-project benchmark runs (including warmups and stage runs)
  produced matching compiled.bin/runtime.bin SHA-256 hashes and passed
  independent baseline audits.

## Measurements

MSVC x64 Release + IPO, existing full UnitProXL copy, native execution outside
the filesystem sandbox. No builds or tests ran concurrently with timing.
Baseline includes the corrected Source timer from experiment 07. Source Model
and direct Source Model options are OFF for both executables.

The initial seven paired normal runs averaged 1440.503 ms baseline versus
1413.669 ms candidate. That apparent 26.8 ms gain included a slow baseline run;
all samples were retained. Three paired instrumented runs showed only 6.7 ms
Source improvement and 2.8 ms complete PUBLISH improvement. These are distinct
series and their stage and total differences must not be mixed.

Acceptance uses eight alternating ABBA/BAAB blocks, two runs of each variant
per block, with an additional excluded warmup block:

| Metric | Result |
|---|---:|
| Baseline mean PUBLISH | 1427.769 ms |
| Candidate mean PUBLISH | 1414.228 ms |
| Mean block saving | 13.542 ms |
| Student-t 95% interval for block saving | [2.047, 25.037] ms |
| Blocks favoring candidate | 7 / 8 |

This is a small measured improvement, about 0.95%. Subsecond PUBLISH is still
unachieved; the change does not establish a larger Source-stage speedup.

Baseline executable SHA-256:
`81C47EFEF267F334F69E4D008AEF58836427B74B80100005F4996343C4F66ED5`.
Candidate executable SHA-256:
`340C8E89136D130A02B9E4C057731FECD5411D254C16B4A317509FB321773456`.
Raw logs, scripts and CSVs: `build/publish-source-dispatch-08/`.
