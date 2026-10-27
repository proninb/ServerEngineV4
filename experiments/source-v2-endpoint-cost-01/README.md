# SOURCE-V2-ENDPOINT-COST-01

Read-only reanalysis of existing SOURCE-V2-CRITICAL-01 ETW-derived full stacks.
No process launches, new sampling, candidate implementation or production edit.
Report: `docs/SOURCE_V2_ENDPOINT_COST_01.md`.

```powershell
python experiments/source-v2-endpoint-cost-01/analyze.py build/source-v2-critical-01 build/source-v2-endpoint-cost-01-new
```

Output must be a new directory under build/. It contains summary.json,
endpoint-partition.csv and provenance.json. The script verifies matching current
versus frozen source hashes and unchanged stack inputs. It expects the recorded
four blocks at 1 ms sample interval, with no loss/truncation; it does not replace
the earlier capture's EXE/PDB, missing-stack or ICF quality audit.

Endpoint partition uses the first observed child of read_endpoint, assigning each
sample once. Hidden inline bodies are not reconstructed. Intern distribution is
caller context, not exact semantic token role; root names can be read before
entering read_endpoint. Reported CPU estimates are sample weights per launch,
not exact call durations or wall-time savings. No new A/B is justified by this
run: large removable duplication has not been demonstrated.
