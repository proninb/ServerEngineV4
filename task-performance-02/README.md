# TASK-PERFORMANCE-02

Diagnostic-only, opt-in timings. Patch current committed branch with `python task-performance-02/apply.py .` then rebuild ServerEngineV4. Set `TASK_PERF02_CSV` to an absolute path before launching benchmark. The server writes buffered CSV at process exit, not per tick. Results are microseconds, analyzer converts to milliseconds.

Stages: `cycle_interval` (time since preceding server tick; first sample ignore), `server_tick` (server task dispatch + bookkeeping), `manager_tick` (full Task Manager tick), `ipc_roundtrip` (RUN write through DONE receive), `barrier_wait` (waiting on futures after dispatch). `barrier_wait` is not entire wave duration; do not sum overlapping metrics. `tick_wait` is not separately measured; `cycle_interval - server_tick` is only an approximation, not an exact wait measurement.

No ABI/protocol change. Uses a mutex and buffered vector per process; measurement overhead is nonzero. Not a production performance patch. On abrupt termination samples may be lost. No commit/push.
