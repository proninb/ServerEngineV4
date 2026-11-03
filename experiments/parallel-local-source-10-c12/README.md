# PLS10-C12 — Existing Batch API Feasibility

**NO ADOPTION. Read-only API audit; no candidate code or benchmark.**

The current `graph::add_resolved_initializations()` preserves span order and
physical slots, but returns only aggregate `any_replaced`. It does not return
per-element slots/results or run producer policy before mutation. PLS10's
slot-indexed origins and exact per-cell warnings cannot use it without extra
lookup/mapping or an API change. This fails the specified feasibility gate.

Full evidence and limitations:
`docs/PLS10_C12_EXISTING_BATCH_API_FEASIBILITY.md`.

C10 remains the experimental baseline; production V2 and Graph are unchanged.
No tests/benchmarks are added or reported as new results for this source audit.
