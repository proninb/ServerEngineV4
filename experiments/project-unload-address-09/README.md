# Synchronized replay address observation 09

See docs/PROJECT_UNLOAD_CONTROL_ADDRESS_09.md: INCONCLUSIVE / NO ADOPTION.
No production or original replay source/binary is modified.

prepare.py copies THREAD-06 replay.cpp and injects replay_checkpoint09.hpp hooks
only at initialized baseline and boundary IDs 0..3. It refuses to overwrite the
diagnostic source. Build the copy with the same MSVC flags and the observer include
path, into build/project-unload-address-09/replay_address09.exe.

```powershell
python experiments/project-unload-address-09/test_maps.py
python experiments/project-unload-address-09/run_address.py
python experiments/project-unload-address-09/compare_maps.py
```

The runner uses the fixed diagnostic build and a new run-01 directory; it refuses
to overwrite prior captures. It executes exactly six single-mode processes with
absolute arguments, blocks at five observer markers, checks child PID/creation
FILETIME and gathers external VirtualQueryEx maps. Original JSONL is independently
validated with the previous 274-boundary/counter validator. Hashes are checked
around every process. Compare only allocation bases within a process; numeric
addresses across processes have no shared-owner meaning.

Region CSV uses actual Win32 numeric state/type/protect values, byte lengths,
region BaseAddress and AllocationBase. Map summaries retain PrivateUsage before
and after querying. Capture is non-atomic; equal before/after PrivateUsage is
recorded, not converted into an ownership claim. Comparison preserves residual
differences between PrivateUsage deltas and MEM_PRIVATE committed-region deltas.

The five observer pauses and new binary can perturb the control. Smaller spread
than CONTROL-07 does not pass release or establish stable startup heap behavior.
