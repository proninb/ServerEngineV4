/*
 * HEADER-MODEL-01: physical syntax skeleton, not a second semantic graph.
 * Strictly read-only for Header G, identity_space, string_table, and Source Map.
 * Includes/macros and unrecognized syntax retain the unmodified Header V2 path.
 */
#pragma once

#include "../frontend/prepared_include_v2.hpp"
#include "../frontend/frontend_input.hpp"
#include "../construction/execution_lanes.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace cw::server {

struct header_model_declaration final {
    file_id file{};
    std::uint32_t offset = 0;
    token_kind kind = token_kind::invalid;
};

// HEADER-MODEL-V2-02A: one exclusion reason per physical root.
// Ineligible files always fall back to the original semantic Header V2.
enum class header_model_exclusion_v2 : std::uint8_t {
    eligible = 0,
    missing_prepared_file,
    lexical_decode_failed,
    preprocessing_directive,
    unsupported_syntax,
    configured_predefines,
    allocation_failed,
};

struct header_model_exclusion_counts_v2 final {
    std::uint64_t roots = 0;
    std::uint64_t eligible = 0;
    std::uint64_t missing = 0;
    std::uint64_t decode_failed = 0;
    std::uint64_t directives = 0;
    std::uint64_t syntax = 0;
    std::uint64_t predefines = 0;
    std::uint64_t allocations = 0;
    std::uint64_t empty = 0;
};

struct header_model_file final {
    file_id root{};
    std::vector<header_model_declaration> declarations;
    bool eligible = false;
    header_model_exclusion_v2 exclusion =
        header_model_exclusion_v2::missing_prepared_file;

    [[nodiscard]] const header_model_declaration* at(std::size_t position) const noexcept {
        return eligible && position < declarations.size()
            ? &declarations[position] : nullptr;
    }
};

class header_model_batch final {
public:
    [[nodiscard]] server_status build(
        prepared_include_view_v2 prepared,
        std::span<const file_id> roots,
        bool no_predefines) noexcept {
        data.clear();
        try {
            data.resize(roots.size());
        } catch (...) { return server_status::io_error; }
        // Always classify roots: a configured predefine must not mask missing
        // physical files, directives or unsupported syntax in telemetry.
        predefines_clear = no_predefines;
        if (roots.empty()) return server_status::success;
        view = prepared;
        root_ids = roots;
        const auto count = (std::min)(roots.size(), execution_lane_capacity());
        if (count == 0) return server_status::success;
        lane_count = count;
        if (count == 1) {
            lane(this, 0);
            return server_status::success;
        }
        execution_lanes workers;
        const auto started = workers.start(count);
        if (!succeeded(started)) return started;
        return workers.run(count, lane, this);
    }

    [[nodiscard]] const header_model_file* root(std::size_t ordinal) const noexcept {
        if (ordinal >= data.size()) return nullptr;
        return data[ordinal].eligible ? &data[ordinal] : nullptr;
    }

    [[nodiscard]] std::size_t eligible_files() const noexcept {
        std::size_t n = 0;
        for (const auto& file : data) n += file.eligible ? 1 : 0;
        return n;
    }

    [[nodiscard]] std::size_t declarations() const noexcept {
        std::size_t n = 0;
        for (const auto& file : data) if (file.eligible) n += file.declarations.size();
        return n;
    }

    [[nodiscard]] header_model_exclusion_counts_v2 exclusions() const noexcept {
        header_model_exclusion_counts_v2 result;
        result.roots = data.size();
        for (const auto& file : data) {
            switch (file.exclusion) {
            case header_model_exclusion_v2::eligible:
                ++result.eligible;
                if (file.declarations.empty()) ++result.empty;
                break;
            case header_model_exclusion_v2::missing_prepared_file: ++result.missing; break;
            case header_model_exclusion_v2::lexical_decode_failed: ++result.decode_failed; break;
            case header_model_exclusion_v2::preprocessing_directive: ++result.directives; break;
            case header_model_exclusion_v2::unsupported_syntax: ++result.syntax; break;
            case header_model_exclusion_v2::configured_predefines: ++result.predefines; break;
            case header_model_exclusion_v2::allocation_failed: ++result.allocations; break;
            }
        }
        return result;
    }

private:
    // Pure syntax. Record bodies (members, constructors, nested records) stay
    // opaque at this stage; Header V2 validates them against G unchanged.
    static bool scope(std::span<const frontend_token> tokens,
                      std::size_t& at,
                      bool nested,
                      header_model_file& out,
                      unsigned depth) noexcept {
        if (depth > 256) return false;
        while (at < tokens.size()) {
            const auto kind = tokens[at].kind;
            if (kind == token_kind::r_brace) {
                if (!nested) return false;
                ++at;
                return true;
            }
            if (kind != token_kind::kw_namespace &&
                kind != token_kind::kw_struct &&
                kind != token_kind::kw_class &&
                kind != token_kind::kw_union &&
                kind != token_kind::kw_typedef) return false;
            out.declarations.push_back({tokens[at].file, tokens[at].source_offset, kind});
            ++at;
            if (kind == token_kind::kw_namespace) {
                // Only the normal single-name namespace grammar is fast-pathed.
                if (at >= tokens.size() || tokens[at].kind != token_kind::identifier) return false;
                ++at;
                if (at >= tokens.size() || tokens[at].kind != token_kind::l_brace) return false;
                ++at;
                if (!scope(tokens, at, true, out, depth + 1)) return false;
                continue;
            }
            if (kind == token_kind::kw_typedef) {
                // A typedef is a semicolon-terminated declaration; parentheses
                // and array brackets are permitted, but braces are not fast-pathed.
                std::size_t parens = 0, brackets = 0;
                bool completed = false;
                for (; at < tokens.size(); ++at) {
                    const auto k = tokens[at].kind;
                    if (k == token_kind::l_brace || k == token_kind::r_brace) return false;
                    if (k == token_kind::l_paren) ++parens;
                    else if (k == token_kind::r_paren) {
                        if (parens == 0) return false;
                        --parens;
                    } else if (k == token_kind::l_bracket) ++brackets;
                    else if (k == token_kind::r_bracket) {
                        if (brackets == 0) return false;
                        --brackets;
                    } else if (k == token_kind::semicolon && parens == 0 && brackets == 0) {
                        ++at;
                        completed = true;
                        break;
                    }
                }
                if (!completed) return false;
                continue;
            }
            // Record declaration or definition. Consume declarator and base
            // specifiers until the first ';' or '{'. Bodies are balanced,
            // including nested constructor braces, without interpreting types.
            bool found = false;
            for (; at < tokens.size(); ++at) {
                if (tokens[at].kind == token_kind::semicolon) {
                    ++at; found = true; break;
                }
                if (tokens[at].kind == token_kind::l_brace) {
                    unsigned braces = 1;
                    ++at;
                    while (at < tokens.size() && braces != 0) {
                        if (tokens[at].kind == token_kind::l_brace) ++braces;
                        else if (tokens[at].kind == token_kind::r_brace) --braces;
                        ++at;
                    }
                    if (braces != 0 || at >= tokens.size() ||
                        tokens[at].kind != token_kind::semicolon) return false;
                    ++at; found = true; break;
                }
            }
            if (!found) return false;
        }
        return !nested;
    }

    void scan(std::size_t index) noexcept {
        auto& output = data[index];
        output.root = root_ids[index];
        const auto* physical = view.file(output.root);
        if (physical == nullptr || !physical->valid()) {
            output.exclusion = header_model_exclusion_v2::missing_prepared_file;
            return;
        }
        // Always inspect each physical root, even if configuration contains
        // predefines. No potentially different effective tokens are published.
        try {
            std::vector<frontend_token> tokens;
            tokens.reserve(physical->words.size() / 2);
            std::uint32_t word = 0, source = 0;
            while (word < physical->words.size()) {
                frontend_token token;
                const auto step = decode_frontend_token(
                    physical->words, output.root, word, source, token);
                if (!succeeded(step)) {
                    output.exclusion = header_model_exclusion_v2::lexical_decode_failed;
                    return;
                }
                // Physical preprocessor tokens always start a pp_* directive.
                // Checking exact kinds avoids interpreting an inactive branch.
                switch (token.kind) {
                case token_kind::pp_include:
                case token_kind::pp_define:
                case token_kind::pp_undef:
                case token_kind::pp_if:
                case token_kind::pp_ifdef:
                case token_kind::pp_ifndef:
                case token_kind::pp_elif:
                case token_kind::pp_else:
                case token_kind::pp_endif:
                case token_kind::pp_line:
                case token_kind::pp_error:
                case token_kind::pp_pragma:
                case token_kind::pp_unknown:
                    output.exclusion = header_model_exclusion_v2::preprocessing_directive;
                    return;
                default: break;
                }
                tokens.push_back(token);
            }
            std::size_t position = 0;
            if (!scope(tokens, position, false, output, 0) ||
                position != tokens.size()) {
                output.declarations.clear();
                output.exclusion = header_model_exclusion_v2::unsupported_syntax;
                return;
            }
            if (!predefines_clear) {
                // Physical tokens may differ from the effective macro stream.
                // Keep the exact original Header V2 for validation.
                output.declarations.clear();
                output.exclusion = header_model_exclusion_v2::configured_predefines;
                return;
            }
            output.eligible = true;
            output.exclusion = header_model_exclusion_v2::eligible;
        } catch (...) {
            output.declarations.clear();
            output.eligible = false;
            output.exclusion = header_model_exclusion_v2::allocation_failed;
        }
    }

    static void lane(void* opaque, std::size_t lane_id) noexcept {
        auto& self = *static_cast<header_model_batch*>(opaque);
        for (std::size_t i = lane_id; i < self.data.size(); i += self.lane_count)
            self.scan(i);
    }

    prepared_include_view_v2 view;
    bool predefines_clear = true;
    std::span<const file_id> root_ids;
    std::size_t lane_count = 1;
    std::vector<header_model_file> data;
};

} // namespace cw::server
