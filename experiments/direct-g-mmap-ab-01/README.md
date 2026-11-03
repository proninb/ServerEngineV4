# DIRECT-G-MMAP-AB-01 — archived, implementation rolled back

The experiment regressed full PUBLISH. Direct mmap v27, its feature flag,
allocator, telemetry and tests have been removed from the active engine.
The prior RAM G + packed encoder implementation is restored.

Results: [DIRECT_G_MMAP_AB_01.md](../../docs/DIRECT_G_MMAP_AB_01.md).

Retained local evidence is under `build/direct-g-mmap-ab-01/`: all 150 timing
samples, logs, source/input manifests, frozen Release + IPO executables and
baseline/direct source snapshots, and semantic/runtime/SHM parity checks.

The original experiment scripts and final pre-rollback implementation are
archived under `build/direct-g-mmap-ab-01/rollback-archive/`. These are historical
artifacts, not active build targets. To reanalyse measurements, use those frozen
artifacts; the current source tree no longer offers a v27 build flag.
