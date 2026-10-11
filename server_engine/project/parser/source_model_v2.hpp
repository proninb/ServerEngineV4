/*
 * SOURCE-V2-MODEL-01: transient, syntactic, per-file Source operation stream.
 * This is NOT a C++ AST, a second Graph, or a semantic identity table.
 * A worker owns the token and operation vectors of one physical file.
 * No Graph/string_table/identity_space/Source Map access occurs here.
 */
#pragma once

#include "../frontend/semantic_input.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cw::server {

enum class source_model_opcode : std::uint8_t {
    statement,            // conservatively retained if grammar shape is ambiguous
    assignment,           // identifier followed by '.', '[', or '='
    declaration,          // all other ordinary statements; validation still uses G
    extern_statement,
    namespace_open,
    namespace_close,
    empty_statement,
};

struct source_model_operation final {
    std::uint32_t token_begin = 0;
    std::uint32_t token_count = 0;
    // Relative index into this operation, or UINT32_MAX if no assignment.
    std::uint32_t assignment_token = 0xffffffffu;
    std::uint16_t namespace_depth = 0;
    source_model_opcode opcode = source_model_opcode::statement;
    std::uint8_t reserved = 0;
};
static_assert(sizeof(source_model_operation) == 16);

// A model is made from the very same retained lexical words consumed by the
// semantic validator; byte ranges and token order are never rewritten.
struct source_model_file final {
    std::vector<source_predecoded_token> tokens;
    std::vector<source_model_operation> operations;
};

class source_model_builder final {
public:
    explicit source_model_builder(source_model_file& destination) noexcept
        : out(destination) {}

    [[nodiscard]] bool accept(const source_predecoded_token& tok) noexcept {
        const auto index = out.tokens.size() - 1;
        const auto kind = tok.kind;
        if (kind == token_kind::l_brace) {
            // Only namespace braces delimit scopes. Object initializers retain
            // their braces as part of a single statement.
            if (initializer_depth == 0 && first_is_namespace()) {
                if (!emit(index + 1, source_model_opcode::namespace_open))
                    return false;
                if (namespace_depth == 0xffffu) return false;
                ++namespace_depth;
                begin = index + 1;
                equal = 0xffffffffu;
                return true;
            }
            ++initializer_depth;
        }
        else if (kind == token_kind::r_brace) {
            if (initializer_depth != 0) {
                --initializer_depth;
            }
            else {
                if (!emit(index, source_model_opcode::statement)) return false;
                begin = index;
                equal = 0xffffffffu;
                if (!emit(index + 1, source_model_opcode::namespace_close))
                    return false;
                if (namespace_depth != 0) --namespace_depth;
                begin = index + 1;
                return true;
            }
        }
        else if (kind == token_kind::assign && initializer_depth == 0 &&
                 equal == 0xffffffffu && index >= begin) {
            equal = static_cast<std::uint32_t>(index - begin);
        }
        else if (kind == token_kind::semicolon && initializer_depth == 0) {
            const auto opcode = (index == begin)
                ? source_model_opcode::empty_statement
                : (out.tokens[begin].kind == token_kind::kw_extern
                    ? source_model_opcode::extern_statement
                    : source_model_opcode::statement);
            if (!emit(index + 1, opcode)) return false;
            begin = index + 1;
            equal = 0xffffffffu;
        }
        return true;
    }

    // Incomplete or invalid syntax is retained for the canonical validator to
    // produce its existing, precisely located diagnostics.
    [[nodiscard]] bool finish() noexcept {
        if (begin == out.tokens.size()) return true;
        return emit(out.tokens.size(), source_model_opcode::statement);
    }

private:
    [[nodiscard]] bool first_is_namespace() const noexcept {
        return begin < out.tokens.size() &&
            out.tokens[begin].kind == token_kind::kw_namespace;
    }
    [[nodiscard]] bool emit(std::size_t end,
                            source_model_opcode code) noexcept {
        if (end <= begin) return true;
        if (end > 0xffffffffu || begin > 0xffffffffu) return false;
        if (code == source_model_opcode::statement && begin + 1 < end) {
            const auto first = out.tokens[begin].kind;
            const auto second = out.tokens[begin + 1].kind;
            code = first == token_kind::identifier &&
                (second == token_kind::dot || second == token_kind::l_bracket ||
                 second == token_kind::assign)
                ? source_model_opcode::assignment
                : source_model_opcode::declaration;
        }
        try {
            out.operations.push_back({
                static_cast<std::uint32_t>(begin),
                static_cast<std::uint32_t>(end - begin),
                equal, namespace_depth, code, 0});
            return true;
        }
        catch (...) { return false; }
    }

    source_model_file& out;
    std::size_t begin = 0;
    std::size_t initializer_depth = 0;
    std::uint32_t equal = 0xffffffffu;
    std::uint16_t namespace_depth = 0;
};

} // namespace cw::server
