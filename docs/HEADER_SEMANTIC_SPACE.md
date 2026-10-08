# Header Semantic Space

`HEADER-SEMANTIC-SPACE-01` makes the Header parser's outer control state
explicit.

The input boundary is the existing preprocessed `semantic_token` stream:

```text
bytes
  -> lexer
  -> Header preprocessing/include replay
  -> semantic_token stream
  -> Header semantic control space
  -> existing semantic operations / G
```

The control model is deliberately small:

```text
(state, token-class, registers) -> (state, semantic action)
```

This is not an AST, not a second Semantic DB, and not a replacement for `G`.
Token payload remains in `semantic_token`; semantic identities, types and other
values remain parser registers / semantic storage.

## Slice 01

The first production slice collapses the Header scope token universe into these
semantic classes:

- end of stream
- close scope
- empty declaration
- typedef declaration
- namespace declaration
- record declaration
- object declaration
- other/reject

The transition function decides the semantic action for a Header scope. Source
parsing is intentionally unchanged.

This preserves the current Header language while replacing the repeated
top-level token-dispatch branch chain with one explicit, constexpr semantic
transition.

## Non-goals

This slice does not yet replace:

- declarator parsing;
- record-body parsing;
- constructor parsing;
- Source-domain parsing;
- semantic identity/type resolution;
- Graph or ABI projection.

Those become subordinate semantic machines only after this scope machine is
verified by the existing frontend regression suite.

## Next expansion

The next useful slice is the record-body machine:

```text
record-body token stream
    -> access / member / constructor / method / close
    -> declarator submachine where required
```

The rule remains: do not flatten the full grammar into one giant DFA. Keep a
small finite control state and separate semantic registers.
