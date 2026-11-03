# Project UNLOAD diagnostics

See `docs/PROJECT_UNLOAD_MEMORY_01.md` and
`docs/PROJECT_UNLOAD_HEAP_SEGMENTS_02.md` for results and limitations.

The capture harness uses the isolated image and frozen fixtures already prepared
under `build/project-unload-memory-01`. It requires ordinary local TCP access and
read access to the target process. It never rebuilds the server or changes allocator
settings. Choose a fresh output directory under `build`:

```powershell
python experiments/project-unload-memory-01/run.py build/project-unload-memory-01 build/project-unload-memory-01/heap-segments-new --cdb
python experiments/project-unload-memory-01/compare_heaps.py build/project-unload-memory-01/heap-segments-new
```

`--cdb` captures baseline, first post-UNLOAD Private Bytes >128 MiB, subsequent
<=128 MiB, and repeated >128 MiB. It stops after that transition or fails if not
observed within 201 cycles. The absolute threshold selects diagnostic states; it
does not redefine the release gate's drift calculation. CDB suspends the target
during inspection. No performance conclusions may be drawn from this run.

The four CDB logs preserve `!heap -s`, `!heap -stat` and `!address`. The comparator
checks identity and supports only the observed NT heap summary/address formats.
It reports internal counters separately from actual region commit and does not
claim that plain `!heap -stat` supplied a busy-block histogram.
