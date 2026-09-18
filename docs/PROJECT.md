# Project Architecture

## Purpose

Project lifecycle is mode-oriented:

```text
LOAD
BUILD
REBUILD
UNLOAD
```

There is no universal `project_context`.

Each operation owns only the temporary state required by that operation.

The state contract is:

```text
server_context.project == nullptr
    == UNLOADED
```

## Lifecycle

```text
UNLOADED
    +-- LOAD success ------> LOADED Gn
    +-- LOAD failure ------> UNLOADED
    +-- REBUILD success ---> LOADED G0
    `-- REBUILD failure ---> UNLOADED

LOADED Gn
    +-- BUILD success -----> LOADED Gn+1
    +-- BUILD failure -----> UNLOADED
    `-- UNLOAD -----------> UNLOADED
```

Preconditions:

```text
LOAD     requires UNLOADED
REBUILD  requires UNLOADED
BUILD    requires LOADED
UNLOAD   requires LOADED
```

LOAD, BUILD, and REBUILD never substitute for each other.

Failure of LOAD, BUILD, or REBUILD always leaves the Server UNLOADED.

## Resident Project

Target resident state:

```text
project
    +-- Graph
    +-- Runtime
    `-- SHM
```

Resident Project must not retain construction-only state:

```text
project.json composition
configuration manifest
Source Manager
Parser/frontend
Builder
```

The current resident Project retains its entry path so BUILD can locate the
persisted construction artifacts belonging to Gn.

## LOAD

LOAD restores persisted resident state:

```text
persisted Graph
    -> Runtime
    -> SHM
    -> resident Project Gn
```

LOAD does not:

```text
parse project.json for construction
compose Project configuration
run Source Manager
perform source change detection
```

LOAD failure leaves UNLOADED.

## REBUILD

REBUILD starts only from UNLOADED and constructs a fresh generation:

```text
root project.json
    -> recursive Project configuration composition
    -> candidate configuration manifest
    -> explicit roots
    -> new Source Manager
    -> frontend / semantic construction
    -> Graph G0
    -> Runtime
    -> SHM
    -> coordinated persisted-state commit
    -> resident Project G0
```

REBUILD ignores previous incremental construction state.

### Current implementation boundary

The current V4 implementation completes:

```text
root project.json
    -> recursive type:"project" composition
    -> validation of every participating project.json
    -> candidate project_configuration_manifest
    -> aggregate project_configuration_hash
```

It currently stops before:

```text
Source Manager
G0
Runtime
SHM
coordinated commit
```

Because no G0 is published yet, the candidate manifest is deliberately not
persisted by the current incomplete REBUILD path.

## BUILD

BUILD starts only from resident Gn:

```text
resident Gn
    -> load committed project.manifest
    -> verify complete configuration input set
    -> Source Manager change detection
    -> candidate Gn+1
    -> coordinated commit
    -> resident Gn+1
```

BUILD has no external Project path argument. It uses the active Project.

### Manifest verification

For every manifest entry:

```text
change_token available and proves unchanged
    -> no file read

otherwise
    -> stable snapshot
    -> SHA-256
    -> compare persisted per-file content hash
```

If every participating `project.json` is byte-identical:

```text
configuration unchanged
    -> proceed directly to Source Manager change detection
```

If any file differs or disappears:

```text
recompose from root
    -> discover added/removed/reordered child Projects
    -> candidate manifest
    -> candidate aggregate configuration hash
```

### Current implementation boundary

The current V4 BUILD implements:

```text
load project.manifest
verify every configuration input
recompose when one input changed
compare aggregate configuration hash
```

It currently stops before Source Manager and `Gn -> Gn+1` construction.

BUILD failure destroys resident Gn and leaves UNLOADED.

## Configuration Manifest

The complete composed Project configuration proof is represented by:

```text
project_configuration_manifest
    configuration_hash
    files[]
        normalized root-relative path
        SHA-256 content hash
        optional file_change_token
```

Traversal order is:

```text
root-first
declaration-order DFS
normalized-path dedupe
platform filesystem case semantics
cycle detection
NO SORT
```

Repeated references to the same normalized configuration file produce one entry.

A reference to an active ancestor is a configuration cycle and fails.

### Path contract

Manifest paths are normalized relative to the root Project directory.

All declared configuration/tree paths are required to be relative to their
declaring `project.json`; absolute/rooted paths fail schema validation.

Filesystem path processing is isolated behind `project_path.hpp`.

```text
relative declared path
    -> resolve_project_path(path, output)
    -> absolute normalized path
    -> make_project_path_key(path, output)
    -> platform filesystem-equivalence key
```

Both path operations are status-returning no-exception boundaries.

```text
Windows -> invariant Unicode case-insensitive key
POSIX   -> case-sensitive key
```

Path processing is fail-closed. Failure to establish the absolute normalized
path or the platform key stops configuration composition. Windows never falls
back to a case-sensitive key, and failed absolute resolution never falls back
to the original unresolved path.

The generic manifest/composition layer contains no platform API code.

This keeps the aggregate configuration identity stable when the whole Project
tree is relocated without changing its internal structure or bytes.

Path is still a locator, not semantic identity.

## Identity Levels

The identity/proof levels are intentionally separate:

```text
file_change_token
    fast filesystem unchanged proof for one file

project_content_hash
    SHA-256 of exact bytes of one project.json

project_configuration_hash
    SHA-256 of the complete ordered configuration-input manifest

project_semantic_fingerprint
    future canonical semantic identity after semantic composition
```

`project_configuration_hash` includes:

```text
format domain
ordered root-relative paths
ordered per-file content hashes
```

It does not include change tokens.

A whitespace/comment-only change modifies byte identity and therefore the
configuration hash even if a later semantic stage may prove equivalent meaning.

## Manifest Persistence

Committed manifest location:

```text
<root-project-dir>/
    .serverengine/
        <root-project.json filename>/
            project.manifest
```

The file is:

```text
versioned
checksummed
variable-size
fail-closed
```

The manifest checksum protects the artifact bytes.

On load, the aggregate `project_configuration_hash` is also recomputed from the
decoded entries and must match the stored aggregate.

The manifest store is a narrow construction-persistence boundary. It does not
own Graph, Source Manager, Runtime, SHM, or resident Project state.

## Publication Contract

Candidate construction artifacts belong to the candidate generation.

They become authoritative only as part of the same successful coordinated commit
that publishes that generation.

Therefore:

```text
failed REBUILD
    must not commit candidate manifest

failed BUILD
    must not commit candidate manifest/baseline
    and destroys resident Gn
```

## Architectural Invariants

1. One Server owns zero or one resident Project.
2. `server_context.project == nullptr` exactly means UNLOADED.
3. LOAD and REBUILD require UNLOADED.
4. BUILD and UNLOAD require LOADED.
5. BUILD operates on resident Gn.
6. LOAD, BUILD, and REBUILD are distinct pipelines.
7. Failure of LOAD, BUILD, or REBUILD leaves UNLOADED.
8. Resident Project contains runtime-required state only.
9. There is no universal `project_context`.
10. Source Manager is construction state, never resident Project state.
11. One normalized configuration path appears at most once in the manifest.
12. Configuration composition is declaration-order DFS and is never sorted.
13. Change tokens are proof optimizations, never identity.
14. Per-file SHA-256 identifies exact configuration bytes.
15. Aggregate configuration hash identifies the complete ordered configuration input set.
16. Aggregate configuration identity is relocation-stable through root-relative paths.
17. Semantic fingerprint remains separate from byte/configuration identity.
18. Candidate persisted construction state is committed only with its successful generation.
19. Project absolute-path resolution must fail closed; unresolved paths must never become construction identities.
