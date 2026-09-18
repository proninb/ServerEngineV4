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

## Lifecycle diagnostic boundary

LOAD, BUILD, and REBUILD may use different Project subsystems, but one
external operation still owns one `operation_id` and one
`diagnostic_collection`. Nested construction layers append to that same
caller-owned collection.

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
2009 project.identity_invalid
2010 project.identity_io_failed
2011 project.configuration_read_failed
2012 project.configuration_cycle
2013 project.manifest_invalid
2014 project.manifest_io_failed
2015 project.manifest_missing
```

The manifest-specific diagnostics separate artifact state from configuration
input state:

```text
project.configuration_read_failed
    a participating project.json cannot be acquired/proven safely

project.configuration_cycle
    recursive type:"project" composition contains a cycle

project.manifest_missing
    BUILD has resident Gn but no committed configuration manifest

project.manifest_invalid
    manifest format/checksum/aggregate validation failed

project.manifest_io_failed
    manifest storage I/O failed
```

Human-readable detail text may evolve. Numeric IDs and symbolic names are the
stable machine-facing contracts.

A changed or missing configuration input during BUILD is not itself a corrupt
manifest. It triggers recomposition. Manifest diagnostics are reserved for the
persisted manifest artifact itself.
