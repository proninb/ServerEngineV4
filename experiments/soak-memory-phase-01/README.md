# SOAK-MEMORY-PHASE-01

Diagnostic-only external eight-stage memory snapshots on the exact retained OFF image. No production rebuild/telemetry, allocator intervention, new PGO measurement or modification of the original gate harness. See docs/SOAK_MEMORY_PHASE_01.md for observed transitions and limits.

```powershell
python experiments/soak-memory-phase-01/run.py build/tcp-pgo-release-02 build/soak-memory-phase-new
python experiments/soak-memory-phase-01/analyze.py build/soak-memory-phase-new
```

Windows x64, normal unsandboxed local-server execution with existing local license/lease, frozen fixtures and reference oracle files. Output must be new. Stop after at least cycle 100 and high→low→high; safeguards default to 180 cycles or 300 seconds. High/low map selection uses absolute 128 MiB; this does not alter the per-case release drift gate.

Stages: before PUBLISH, after PUBLISH, after RUN/FREEZE, after UNLOAD1, after LOAD, after UNLOAD2, after TCP error/disconnect, after reconnect/GET_STATE. Snapshotting and VirtualQueryEx occur in the harness process. Maps are non-atomic read-only region classifications, with target private counters before/after; no page contents, thread suspension or heap trimming.

Imports only existing helper functions from production-pgo-01, tcp-reliability-01 and tcp-pgo-release-02. The gate harness is not edited. Timing perturbation is explicit; phase results cannot replace the earlier failed release gates. Portable results contain metadata/counters/virtual address region tables, not process memory contents, corpus, binaries or credentials.
