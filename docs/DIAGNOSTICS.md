# Diagnostics Architecture

## Contract

Diagnostics explain why one Server operation failed or deserves attention.

```text
server_status -> control-flow result
diagnostic    -> stable reason and source presentation
```

Every externally visible Server operation owns:

```text
one operation_id
one diagnostic_collection
```

Nested layers append to the same collection.

## Physical structure

```text
server_engine/
├── operation.hpp
└── diagnostics/
    ├── diagnostic.hpp
    ├── diagnostic_builder.hpp
    ├── diagnostic_collection.hpp
    ├── diagnostic_collection.cpp
    ├── diagnostic_descriptor.hpp
    ├── diagnostic_registry.hpp
    ├── diagnostic_source_cache.hpp
    ├── diagnostic_source_cache.cpp
    ├── diagnostic_formatter.hpp
    └── diagnostic_formatter.cpp
```

## Clang-style diagnostic model

A source/document location contains:

```text
file
line        one-based
column      one-based byte column
offset      zero-based byte offset
length      byte range
file_id     operation-local source-cache identity
```

A record contains:

```text
diagnostic id
severity
operation id
location
message override (optional)
detail
```

`message` is the short primary diagnostic text.

`detail` explains specifically what is wrong for this occurrence.

When no message override is supplied, the descriptor's catalog message is used.

## Text output

The formatter emits:

```text
file:line:column: severity: message
source line
    ^~~~~
detail: what is wrong
```

`length == 0` produces a caret-only location.

A non-zero range renders `^~~~~` up to the end of the current source line.

## Source cache

Complete source text is not copied into every diagnostic record.

`diagnostic_collection` owns one `diagnostic_source_cache` for the operation.

```text
source file text
    ↓ stored once
diagnostic_file_id
    ↓
diagnostic_location
    ↓
formatter recovers source line
```

This keeps diagnostic records compact while supporting console and future
Engineering Studio presentation.

## Stable catalog

`diagnostic_descriptor` provides:

```text
stable numeric id
domain
default severity
stable symbolic name
default message
```

Compile-time checks enforce unique IDs and names.

Human-readable message/detail text is not a machine protocol key.

## JSON-with-comments offset rule

JSON configuration files such as `server.json` support:

```text
// line comments
/* block comments */
```

Comment removal must preserve source byte positions.

The JSON parser consumes comments as trivia while offsets continue to reference
the original byte stream. Schema diagnostics can therefore map offsets directly
to the original file for line/column/caret presentation.

## Presentation boundary

Diagnostics storage does not print itself.

```text
diagnostic_collection
    ↓
diagnostic_formatter
    ├── stderr / console now
    ├── logging later
    ├── TCP/JSON later
    └── Engineering Studio later
```

No subsystem should parse human-readable messages to decide machine behavior.

## Server Configuration Schema Diagnostics

`server.json` schema failures continue to use the stable Server configuration
diagnostic descriptors. Nested `settings` errors are distinguished by precise
source ranges and fully qualified detail text rather than by allocating one
diagnostic ID per field.

Examples:

```text
settings requires abi, shm, and files
settings.abi requires target and pack
settings.shm requires mode
settings.shm.fixed_direct requires fixed_base_address
settings.shm.fixed_base_address is valid only for fixed_direct
settings.shm.mode must be fixed_direct or relocatable_transfer
settings.files requires manifest, source_save, database, and compiled
settings.files.manifest must be a single relative file name
unknown property in settings.shm: foo
duplicate property in settings.abi: pack
```

SHM schema failures continue to use `1103 configuration.invalid`; the precise
field/rule is carried by source location plus detail text. A separate diagnostic
ID is not allocated per configuration field.

Unsupported schema versions use `1104 configuration.unsupported_version` and
report the expected current version explicitly.

## Lifecycle diagnostic boundary

LOAD, PUBLISH, BUILD, and REBUILD all start from `UNLOADED`, receive a Project path, and
may use different persisted/construction subsystems.

One external operation still owns exactly:

```text
one operation_id
one diagnostic_collection
```

Nested construction layers append to that same caller-owned collection.

A failed BUILD leaves no resident Project. The previous successful persisted
persisted BUILD state may remain available for future BUILD acceleration, but diagnostics for the
failed operation describe the current attempted source state.

## Current Project Construction Diagnostics

Project construction diagnostics use stable IDs:

```text
2001 project.already_loaded
2002 project.not_loaded
2003 project.load_failed
2004 project.startup_unsupported
2005 project.invalid_json
2006 project.invalid_configuration
2007 project.build_incomplete
2008 project.rebuild_incomplete
2011 project.configuration_read_failed
2012 project.configuration_cycle
2013 project.manifest_invalid
2014 project.manifest_io_failed
2015 project.manifest_missing
2026 project.assign_invalid
2027 project.compiled_invalid
2028 project.compiled_io_failed
2029 project.database_invalid
2030 project.database_io_failed
2031 project.publish_cleanup_failed
```

The retired root-only `project.identity` artifact no longer exists. Diagnostic
IDs 2009 and 2010 are intentionally unassigned; they are not reused for the
manifest layer.


Lifecycle use of the existing state diagnostics changes with the BUILD contract:

```text
project.already_loaded
    LOAD / PUBLISH / BUILD / REBUILD requested while a resident Project is active

project.not_loaded
    operation requiring resident runtime state (for example UNLOAD)
    requested while UNLOADED
```

BUILD no longer reports `project.not_loaded` merely because no resident Project
exists; `UNLOADED` is its required entry state.

The manifest-specific diagnostics separate artifact state from configuration
input state:

```text
project.configuration_read_failed
    a participating project.json cannot be acquired/proven safely

project.configuration_cycle
    recursive type:"project" composition contains a cycle

project.manifest_missing
    BUILD has no persisted project.manifest; REBUILD is required

project.manifest_invalid
    manifest format/checksum/aggregate validation failed

project.manifest_io_failed
    manifest storage I/O failed
```

Assign syntax failures use:

```text
2026 project.assign_invalid
```

The diagnostic points at the offending `.assign` line. Assign syntax validation
does not perform semantic variable lookup or Graph validation.

Human-readable detail text may evolve. Numeric IDs and symbolic names are the
stable machine-facing contracts.

Project schema diagnostics keep one stable class:

```text
project.invalid_configuration
```

Root/nested preprocessing scope errors do not allocate new diagnostic IDs. The
schema validator reports the exact offending source range and a specific detail.
For example, a nested `preprocessor` property points at that property and reports:

```text
preprocessor is allowed only in the root Project configuration
```

A missing root `preprocessor` points at the root object boundary and reports the
required root field order.

A changed or missing configuration input during BUILD is not itself a corrupt
manifest. It triggers recomposition. Manifest diagnostics are reserved for the
persisted manifest artifact itself.

During PUBLISH and REBUILD, `compiled.bin` diagnostics retain error severity
because that artifact is required for resident Project publication. PUBLISH does
not create BUILD-acceleration artifacts. During REBUILD, persistence failures for
`project.manifest`, `source.bin`, or `database.bin` use their existing stable
diagnostic IDs with `warning` severity: those files are BUILD acceleration only,
so the current REBUILD may still succeed while the next BUILD rejects
missing/invalid acceleration state. `project.publish_cleanup_failed` reports a
PUBLISH pre-clean/failure-cleanup error that could leave artifact state ambiguous.
