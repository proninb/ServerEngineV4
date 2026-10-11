# HEADER-MODEL-V2-01 — bounded syntax-model experiment

## Contract

This is an **experimental first slice**, NOT a full structured Header Model.
It does not replace Header V2 record-member/type validation. Its purpose is
measuring read-only per-file syntax preparation and exercising model-driven
Header V2 scope declaration dispatch without changing Graph G.

- Parallel physical syntax model: `file_id`, declaration source offset and
  declaration kind (`namespace`, `struct`, `class`, `union`, `typedef`).
- Struct/class/union bodies and type operands remain opaque to the model.
- Header V2 still semantically parses those declarations and commits directly
  to G; the model only dispatches root/namespace declaration kind.
- No second G, AST or separate WHO table. No persistent model artifact.
- Model recognizes only directive-free physical roots with no configured
  predefines and a self-contained supported scope structure.
- Preprocessor-sensitive roots (`#include`, conditionals, macros, etc.) and
  unknown syntax follow the original Header V2 path unchanged.
- Header V2 includes are still prepared and published under the original
  contract. The workers only read already-prepared physical tokens.
- `SERVER_ENGINE_HEADER_MODEL_V2=OFF` disables model construction and dispatch.

## Measurements

`header_v2_model_ms` is *only* physical syntax-model wall time (not full Header
semantic parsing). `header_ms` remains Header V2 validation plus Graph commits.
`header_v2_model_dispatched` counts declarations actually dispatched by the
syntax model. A value of 0 means this experiment did not exercise the fast path.

Compare `header_ms + header_v2_model_ms` ON against `header_ms` OFF under the
same benchmark workload in interleaved runs. Require byte parity and existing
Header/Source regression tests, including include resolution, diagnostics,
inheritance, construction, nested records and determinism.

## Follow-on

HEADER-MODEL-02 would need real per-file member/type/initializer operand IR and a
validator consuming those operands without re-parsing their syntax. Do NOT
claim HEADER-MODEL-01 completes this transition or promises an acceleration.
