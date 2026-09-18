# JSON Architecture

## Purpose

`server_engine/json` is a generic JSON-with-comments utility subsystem.

It does not know about:

- Server lifecycle;
- `operation_id`;
- `diagnostic_collection`;
- server configuration schema;
- Project configuration schema.

The configuration layer converts JSON syntax/schema failures into Server
diagnostics.

## Physical structure

```text
server_engine/json/
├── json.hpp
├── json_ascii.hpp
├── json_unicode.hpp
├── json_value.hpp
├── json_parser.hpp
├── json_parser.cpp
├── json_buffer.hpp
├── json_escape.hpp
└── json_writer.hpp
```

## Parser architecture

```text
UTF-8 JSON text
    ↓
streaming parser
    ↓
json_event_handler
    ↓
configuration schema handler
```

No DOM is constructed.

The parser accepts standard JSON plus:

```text
// line comments
/* block comments */
```

Comments are consumed directly as lexical trivia, so byte offsets always refer
to the original source file.

## Error model

The public parser boundary is exception-free:

```cpp
json_parse_result parse_json(
    std::string_view text,
    json_event_handler& handler) noexcept;
```

A syntax failure returns:

```text
json_error_code
offset
length
```

The JSON subsystem does not format line/column diagnostics.

The diagnostics layer maps `(offset,length)` through
`diagnostic_source_cache` to:

```text
file:line:column
source line
^~~~~
```

## Scalar values

All scalar JSON values use one callback:

```cpp
virtual void value(json_value_view value) = 0;
```

Typed conversion is requested by the schema layer:

```cpp
std::uint16_t port = 0;

if (!value.get(port)) {
    // wrong JSON type or numeric range
}
```

`get<T>()` is range-safe. Numeric conversion is performed directly from the
original JSON number token.

String keys and values are decoded UTF-8. Escaped object keys are never exposed
as raw `\uXXXX` text.

## Unicode

The parser validates:

- UTF-8 byte sequences;
- JSON escape sequences;
- `\uXXXX`;
- UTF-16 surrogate pairs;
- Unicode scalar value validity.

Decoded string callbacks receive UTF-8.

## Configuration boundary

`server_configuration_loader` owns the server.json schema:

```text
JSON syntax
    ↓
json_parse_result

server.json schema
    ↓
schema_error

both
    ↓
diagnostic_collection
```

Unknown and duplicate properties fail closed.

## Writer

`json_writer` is a compact state-checked writer.

Invalid object/array/key/value call ordering returns `false` and marks the
writer invalid instead of silently producing structurally invalid JSON.
