# Source string initialization profile (34)

The Source V2 string path did not update the existing string statement and
element counters. It now records completed statements and expanded array
elements, and exposes `source_string_assignment_ms` in publish-profile.
The timer is disabled in publish-stage and ordinary PUBLISH.

Native Release measurements on the disposable UnitProXL fixture:

| Metric | Result |
| --- | ---: |
| String assignments | 15,899 |
| Expanded character initializations | 508,768 |
| All persisted initializations | 705,237 |
| Source in coarse stage run | 451.933 ms |
| String path in detailed profile | 74.7434 ms |
| Source in detailed profile | 687.829 ms |

These are single diagnostic runs, not a controlled optimization comparison.
Detailed input clocks add substantial overhead; the 74.7434 ms includes string
token processing, path interning, Graph writes, provenance and warnings. It
must not be interpreted as Graph-only time or as a guaranteed batch API saving.
The 72% share of initialization records is not a 72% share of Source time.
Even eliminating this whole measured string path would not close the current
gap to subsecond PUBLISH by itself.

Frontend regression tests passed after rebuilding the benchmark and frontend
targets. Both compiled.bin and runtime.bin retained the established golden
SHA256 hashes. Logs are in build/publish-subsecond-01/string-counters-*.
No representation or Graph algorithm change is enabled by this instrumentation.
