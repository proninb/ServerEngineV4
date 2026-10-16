# Header V2 semantic reuse

PUBLISH only. Graph and persistence schemas, BUILD, REBUILD and LOAD are unchanged.
The pre-existing experimental leaf cache has been replaced on the PUBLISH path
by an ordered, construction-local semantic event cache.

## Admission and fallback

A file is captured only after successful replay with an empty macro environment,
an active `#pragma once`, and complete global declaration boundaries. Active
`#define` invalidates every enclosing capture, even if a later `#undef` would
restore an empty table. Active no-op `#undef`, and `#ifdef`/`#ifndef` decisions
made while all macros are absent, can be reused. Nonempty macro environments
always take the ordinary parser: this is conservative empty-context reuse, not
a general cache keyed by arbitrary macro bindings.

`#include` does not by itself disqualify a file. The cache records an ordered
program of includes, active once markers, semantic contributions and semantic
dependencies. A preflight simulates the current root's once state and validates
that all required cached children are available, including the existing include
depth limit. Replay publishes each active physical include dependency, counts it,
respects the current root's once markers, and replays Source Map contributions
and dependencies at their original positions. No Project-global once set is used.
Missing cached children cause a normal parse, not a partial replay.

Namespace declarations, includes inside C++ bodies, incomplete declaration
fragments, headers without active `#pragma once`, and macro mutations use the
normal parser. Captures are invalidated up the enclosing include stack. These
checks prevent global semantic identities from being reused in another scope.

A declaration's Graph and provenance updates commit before consuming its final
semicolon and advancing into subsequent includes or EOF. This aligns physical
file completion with the completed semantic declaration. Typedefs use the same
boundary. Syntax and allowed declarations are unchanged.

## State and lifetime

The event cache belongs to one PUBLISH construction. It retains canonical
semantic references, not an AST or another Graph. It is discarded after parsing.
Neither the cache nor admission state is written into compiled.bin/runtime.bin.
The regular per-root macro and once resets remain in place.

## Validation

- Frontend regression executable, including Common/A/B parse-once assertions,
  exact root-local provenance/dependency checks, conditional macro contexts,
  namespace fallback, macro writes followed by undef, no-once headers,
  conditional missing includes, self-includes and cached-subtree depth limits.
- Synthetic integration fixtures compare both published artifact hashes to a
  preserved baseline and run the baseline's independent artifact audit.
- Full UnitProXL A/B uses the same absolute fixture path, Release /O2, IPO OFF,
  one warmup plus five measured alternating pairs. Both artifact SHA-256 values
  are compared after every PUBLISH; the saved baseline audits every artifact.
- See build/semantic-reuse-validation/ for raw logs, CSV, summary and fixtures.

The previous uncommitted leaf-only cache incorrectly reused a global header
inside a namespace. The namespace regression therefore checks two distinct
scoped types against the legacy REBUILD path and audits each image, rather than
requiring equivalence to that faulty PUBLISH output. UnitProXL's complete
PUBLISH artifacts remain byte-identical to the preserved baseline.
