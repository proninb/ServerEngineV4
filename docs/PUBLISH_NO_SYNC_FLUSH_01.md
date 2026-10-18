# PUBLISH-NO-SYNC-FLUSH-01

## Performance / crash-durability contract

`PUBLISH` encodes and validates `compiled.bin` and `runtime.bin` using writable file
mappings, then opens them read-only and constructs a Resident Project/SHM. The
`SERVER_ENGINE_PUBLISH_NO_SYNC_FLUSH` build option is **ON by default**. Under
this option only, PUBLISH omits explicit synchronous calls to mapping `flush()`.

`REBUILD` and other persistence operations retain synchronous flushes. No change
to `writable_file_mapping::flush()` or to file image formats.

The files remain readable from the system cache without an explicit flush on
ordinary operation. A power failure, kernel crash or storage failure may leave
invalid/incomplete files. Such files must not be trusted; recover by PUBLISH or
REBUILD from source. Ordinary subsequent LOAD continues to validate artifacts.

No change is made to Graph WHO/WHERE, runtime construction, SHM references,
parser rules, or semantic validation.

## Rollback / experiment

Set `-DSERVER_ENGINE_PUBLISH_NO_SYNC_FLUSH=OFF` to restore the synchronous
PUBLISH durability barrier. Compare the same project and compiler settings
using Release + IPO ON with fresh artifacts. Validate exact compiled.bin and
runtime.bin SHA256 parity, ordinary LOAD/audit, and all CTest tests.

The benchmark counter `compiled_flush_ms` measures only an explicit barrier
when the option is OFF. With the default ON, it measures only the negligible
post-encoding checkpoint. The OS may still perform background paging and I/O;
removing a synchronous flush does not promise a fixed wall-time speedup.
