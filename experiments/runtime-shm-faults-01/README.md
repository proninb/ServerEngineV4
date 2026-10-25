# RUNTIME-SHM-FAULTS-01

Resolve Windows kernel paths beneath Runtime Object materialization from the
existing PUBLISH-CRITICAL-PATH-02 capture. No production or timing changes.

Result: 93821 kernel-leaf samples under apply_api_one, 93430 in KiPageFault stacks,
63232 in MiResolveDemandZeroFault stacks. Working-set updates and shared spin-lock
paths are prominent. Samples do not count faults or measure individual latency;
the original CPU trace has no fault events.

See [report](../../docs/RUNTIME_SHM_FAULTS_01.md) for exact evidence and commands.
Public OS PDB validation follows Microsoft GUID/Info-age/DBI-age rules, separately
from the unchanged application PDB verifier. Wrong GUID and DBI age are rejected.
Only OS symbol identifiers are requested from Microsoft; no trace data is uploaded.

The reader inspects module identities/event inventory with two arguments, or
exports CPU stacks with seven arguments (see Program.cs). It directly resolves
validated OS modules by RVA through DIA, retaining ordinary TraceEvent app lookup.
export.py checks event-level and application-stack parity against the old export.
All outputs/PDBs stay under ignored build/runtime-shm-faults-01/.

Next candidate: change only the Object worker cap (4/8/16 versus 24). Do not
combine with pretouch, large pages, a new scheduler, validation cache or lifetime.
