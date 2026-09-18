# Project Architecture

## Purpose

Project lifecycle is mode-oriented:

```text
LOAD
BUILD
REBUILD
```

Each mode owns its own temporary pipeline state. There is no universal
`project_context`.

Server owns zero or one resident Project:

```text
server
    |
    `-- server_context
            |
            `-- project
```

`server_context.project == nullptr` exactly means UNLOADED.

## Resident Project

Resident `project` contains only data required while the Project is active.

Target state:

```text
project
    |
    +-- Graph
    +-- Runtime
    `-- SHM
```

It must not retain `project.json`, composition state, Source Manager,
parser/frontend state, Builder state, or mode-local temporary state.

## LOAD

LOAD restores persisted resident state:

```text
persisted Graph
    -> Runtime
    -> SHM
    -> project
```

LOAD does not parse `project.json` for construction, run Source Manager, or
perform source change detection.

## BUILD

BUILD begins with persisted Project input proof.

```text
project.identity
    |
    v
Project identity decision
```

Identity has three distinct levels:

```text
file_snapshot_observation
    size + write time
    cheap observation only

file_change_token
    volume serial + file reference + per-file USN
    O(1) unchanged proof when supported

project_content_hash
    SHA-256 of exact project.json bytes
    authoritative byte identity

project_semantic_fingerprint
    canonical composed Project meaning
    produced after composition
```

Path is location, not identity.

BUILD decision order:

```text
persisted change_token
    |
    +-- proves unchanged
    |       -> no read
    |       -> no parse
    |
    `-- unavailable / changed
            |
            v
        acquire one stable snapshot
            |
            v
        SHA-256
            |
            +-- same
            |       -> no parse
            |
            `-- different
                    |
                    v
                streaming schema
                    |
                    v
                composition
                    |
                    v
                semantic fingerprint
```

After Project semantics are proven unchanged, BUILD proceeds to Source Manager
change detection:

```text
sources unchanged -> reuse persisted Graph
sources changed   -> Gn -> Gn+1
```

If Project semantics changed, BUILD recomposes explicit roots and performs the
required Gn -> Gn+1 construction. REBUILD remains the explicit forced-G0 mode.

## REBUILD

REBUILD ignores incremental construction state:

```text
stable project.json snapshot
    |
    +-- SHA-256 content hash
    |
    `-- exact same bytes
            |
            v
        streaming schema
            |
            v
        composition
            |
            v
        semantic fingerprint
            |
            v
        explicit roots
            |
            v
        new Source Manager
            |
            v
        G0
            |
            v
        Runtime -> SHM -> project
```

A successful REBUILD persists the identity belonging to that committed
generation.

## Project Identity Persistence

Identity is stored separately from runtime state:

```text
<project-dir>/
    .serverengine/
        <project.json filename>/
            project.identity
```

`project.identity` is fixed-size, versioned, and self-checking.

Logical payload:

```text
header
    magic
    format_version
    flags

content_hash          32 bytes
semantic_fingerprint  32 bytes

change_token
    volume_serial      8 bytes
    file_reference     8 bytes
    file_usn           8 bytes

checksum              32 bytes SHA-256
```

The checksum covers the complete decision payload before the checksum field.
Unknown flags, invalid tokens, noncanonical absent fields, size mismatch, magic
mismatch, version mismatch, or checksum mismatch fail closed.

The store is intentionally narrow:

```text
project_identity_store
    load()
    save()
```

It is not a generic persistence manager.

## Configuration Byte Ownership

`project.json` is read once into an owning snapshot.

```text
project_content_snapshot.bytes
    |
    +-- SHA-256
    |
    `-- parser consumes string_view over the same bytes
```

On successful validation there is no source-text copy.

On diagnostic error ownership of the same string moves into
`diagnostic_collection`:

```text
snapshot.bytes
    -> diagnostics.add_source(..., std::move(bytes))
```

Thus:

```text
success: one read, zero source copies
error:   one read, zero source copies
```

## Publication Rule

All modes construct a candidate resident Project and publish only after complete
success.

```text
UNLOADED + success -> LOADED
UNLOADED + failure -> UNLOADED
```

Partial mode state is destroyed on failure.

## Architectural Invariants

1. One Server owns zero or one resident Project.
2. Resident `project` contains runtime-required state only.
3. LOAD, BUILD, and REBUILD are separate pipelines.
4. There is no universal `project_context`.
5. Path is location, not Project identity.
6. Filesystem observation is never authoritative content identity.
7. USN token is a proof optimization, not semantic identity.
8. SHA-256 identifies exact configuration bytes.
9. Semantic fingerprint identifies composed Project meaning.
10. Hashing and parsing use the same acquired bytes.
11. Project identity persistence is independent from resident Project state.
12. Source Manager is construction state, never resident Project state.
13. Publication happens only after complete mode success.
