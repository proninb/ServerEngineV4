# SOURCE-LINK-INDEX-01

Isolated 2x/4x growth experiment for the existing Graph link-target index.
Result: candidate rejected; no reliable full-PUBLISH speedup and final capacity doubles.
See [report](../../docs/SOURCE_LINK_INDEX_01.md) for numbers and reproduction.

All switches are compile-time experiment-only. Production uses the original 2x
routine. `SourceLinkIndex01Audit` has counters; `SourceLinkIndex01` is counter-free;
`SourceLinkIndex01Tests` runs the compiled/runtime suite with both policies and one
forced larger-allocation failure. Runtime selector: `CW_GRAPH_LINK_INDEX_REFERENCE`
(`1` reference, `0` candidate); it does not exist in the production executable.
