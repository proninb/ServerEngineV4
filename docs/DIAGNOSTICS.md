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

`server.json` supports:

```text
// line comments
/* block comments */
```

Comment removal must preserve source byte positions.

The configuration loader therefore replaces comment bytes with spaces while
preserving CR/LF bytes and total input length. Parser offsets remain valid
against the exact original file and can be converted directly to line/column.

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
