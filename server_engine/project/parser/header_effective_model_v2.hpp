/*
 * HEADER-MODEL-V2-02B2 (experimental physical-field syntax model).
 * Scans already prepared PHYSICAL lexical words on independent execution
 * lanes. This code never interns strings, expands macros, mutates G, or
 * consumes the active semantic_preprocessor_v2 stream.
 *
 * A candidate is trusted only if the real streaming preprocessor reaches its
 * exact physical location and all identifier spellings in that candidate
 * are currently free of active macro definitions. The normal Header Parser
 * handles everything else.
 */
#pragma once

#include "../construction/execution_lanes.hpp"
#include "../file/file_context.hpp"
#include "../frontend/prepared_include_v2.hpp"
#include "../frontend/lexical_token.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace cw::server {

struct header_effective_field_v2 final {
    file_id file{};
    std::uint32_t type_offset = 0;
    std::uint32_t type_length = 0;
    std::uint32_t name_offset = 0;
    std::uint32_t name_length = 0;
    std::uint32_t semicolon_offset = 0;
    token_kind builtin = token_kind::invalid; // identifier means named type
};

class header_effective_model_v2 final {
public:
    [[nodiscard]] server_status build(
        const file_context& files,
        prepared_include_view_v2 prepared) noexcept {

        inputs = &files;
        view = prepared;
        data.clear();
        candidate_records = 0;
        candidate_fields = 0;
        if (files.size() > static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)()))
            return server_status::io_error;
        try {
            data.resize(files.size());
        } catch (...) { return server_status::io_error; }
        if (data.empty()) return server_status::success;

        lanes = (std::min)(data.size(), execution_lane_capacity());
        if (lanes == 0) lanes = 1;
        if (lanes == 1) {
            work(this, 0);
        } else {
            execution_lanes workers;
            const auto started = workers.start(lanes);
            if (!succeeded(started)) return started;
            const auto ran = workers.run(lanes, work, this);
            if (!succeeded(ran)) return ran;
        }
        for (const auto& file : data) {
            candidate_records += file.records;
            candidate_fields += file.fields.size();
        }
        return server_status::success;
    }

    [[nodiscard]] const header_effective_field_v2* find(
        file_id file, std::uint32_t source_offset) const noexcept {
        if (!file || file.value() > data.size()) return nullptr;
        const auto& candidates = data[file.value() - 1].fields;
        const auto it = std::lower_bound(
            candidates.begin(), candidates.end(), source_offset,
            [](const header_effective_field_v2& left,
               std::uint32_t right) noexcept {
                return left.type_offset < right;
            });
        return it != candidates.end() && it->type_offset == source_offset
            ? &*it : nullptr;
    }

    [[nodiscard]] std::string_view spelling(
        file_id file, std::uint32_t offset,
        std::uint32_t length) const noexcept {
        if (inputs == nullptr || !file ||
            file.value() > inputs->size()) return {};
        const auto text = inputs->content(file);
        if (offset > text.size() || length > text.size() - offset)
            return {};
        return text.substr(offset, length);
    }

    [[nodiscard]] std::size_t record_count() const noexcept {
        return candidate_records;
    }
    [[nodiscard]] std::size_t field_count() const noexcept {
        return candidate_fields;
    }
    // Constructors still use the original streaming Header Parser V2.
    [[nodiscard]] std::size_t constructor_count() const noexcept {
        return 0;
    }

private:
    struct scanned_token final {
        std::uint32_t offset = 0;
        std::uint32_t length = 0;
        token_kind kind = token_kind::invalid;
        std::uint32_t brace_depth = 0;
        std::uint32_t paren_depth = 0;
        std::uint32_t bracket_depth = 0;
    };
    struct file_model final {
        std::vector<header_effective_field_v2> fields;
        std::size_t records = 0;
    };

    [[nodiscard]] static bool builtin(token_kind kind) noexcept {
        switch (kind) {
        case token_kind::kw_bool:
        case token_kind::kw_int:
        case token_kind::kw_short:
        case token_kind::kw_long:
        case token_kind::kw_float:
        case token_kind::kw_double:
        case token_kind::kw_char: return true;
        default: return false;
        }
    }
    [[nodiscard]] static bool record_start(token_kind kind) noexcept {
        return kind == token_kind::kw_struct ||
               kind == token_kind::kw_class ||
               kind == token_kind::kw_union;
    }
    [[nodiscard]] static bool field_boundary(token_kind kind) noexcept {
        return kind == token_kind::l_brace ||
               kind == token_kind::semicolon ||
               kind == token_kind::colon;
    }

    // No semantic cursor and no global string interning. The compact lexical
    // encoding uses first absolute start, then relative start deltas; escaped
    // source offsets and lengths occupy extra words in that order.
    void scan(std::size_t index) noexcept {
        auto& output = data[index];
        const auto file = file_id{static_cast<std::uint32_t>(index + 1)};
        const auto* physical = view.file(file);
        if (physical == nullptr || !physical->valid() ||
            !inputs->content_available(file)) return;
        const auto source = inputs->content(file);
        if (source.size() != physical->symbols->source_size()) return;
        const auto words = physical->words;
        std::size_t position = 0;
        std::uint32_t previous_offset = 0;
        bool first = true;
        std::uint32_t braces = 0, parens = 0, brackets = 0;
        bool pending_record = false;
        std::vector<std::uint32_t> record_depths;
        std::array<scanned_token, 3> prior{};
        try {
            while (position < words.size()) {
                const auto raw = lexical_token::from_value(words[position++]);
                if (!raw) { output.fields.clear(); output.records = 0; return; }
                auto delta = raw.delta();
                auto length = raw.length();
                if (delta == lexical_token::extended_delta) {
                    if (position == words.size()) return;
                    delta = words[position++];
                }
                if (length == lexical_token::extended_length) {
                    if (position == words.size()) return;
                    length = words[position++];
                }
                if (!first && delta > UINT32_MAX - previous_offset) return;
                const auto offset = first ? delta : previous_offset + delta;
                first = false;
                previous_offset = offset;
                if (offset > source.size() || length > source.size() - offset) return;
                const auto kind = raw.kind();
                const scanned_token current{offset, length, kind,
                                             braces, parens, brackets};

                // Only an unqualified `T name;` or `builtin name;` between
                // statement boundaries, directly in a record body. Directives,
                // arrays, references, constructor bodies and methods never
                // enter the fast path.
                if (kind == token_kind::semicolon &&
                    !record_depths.empty() &&
                    braces == record_depths.back() &&
                    parens == 0 && brackets == 0 &&
                    field_boundary(prior[0].kind) &&
                    (builtin(prior[1].kind) ||
                     prior[1].kind == token_kind::identifier) &&
                    prior[2].kind == token_kind::identifier &&
                    (prior[0].brace_depth == braces ||
                     (prior[0].kind == token_kind::l_brace &&
                      prior[0].brace_depth + 1 == braces)) &&
                    prior[1].brace_depth == braces &&
                    prior[2].brace_depth == braces &&
                    prior[1].paren_depth == 0 &&
                    prior[2].paren_depth == 0 &&
                    prior[1].bracket_depth == 0 &&
                    prior[2].bracket_depth == 0 &&
                    prior[1].length != 0 && prior[2].length != 0) {
                    output.fields.push_back({
                        file, prior[1].offset, prior[1].length,
                        prior[2].offset, prior[2].length,
                        offset,
                        prior[1].kind,
                    });
                }

                if (record_start(kind)) pending_record = true;
                if (kind == token_kind::semicolon) pending_record = false;
                if (kind == token_kind::l_brace) {
                    if (pending_record) {
                        record_depths.push_back(braces + 1);
                        ++output.records;
                    }
                    pending_record = false;
                    ++braces;
                } else if (kind == token_kind::r_brace) {
                    if (braces == 0) return;
                    --braces;
                    while (!record_depths.empty() &&
                           record_depths.back() > braces)
                        record_depths.pop_back();
                } else if (kind == token_kind::l_paren) {
                    ++parens;
                } else if (kind == token_kind::r_paren) {
                    if (parens > 0) --parens;
                } else if (kind == token_kind::l_bracket) {
                    ++brackets;
                } else if (kind == token_kind::r_bracket) {
                    if (brackets > 0) --brackets;
                }
                prior[0] = prior[1];
                prior[1] = prior[2];
                prior[2] = current;
            }
        } catch (...) {
            // Speculative preparation is optional; corrupt/incomplete models
            // must fall back to the original parser, not publish partial data.
            output.fields.clear();
            output.records = 0;
        }
    }

    static void work(void* context, std::size_t lane) noexcept {
        auto& self = *static_cast<header_effective_model_v2*>(context);
        for (std::size_t i = lane; i < self.data.size(); i += self.lanes)
            self.scan(i);
    }

    const file_context* inputs = nullptr;
    prepared_include_view_v2 view;
    std::vector<file_model> data;
    std::size_t lanes = 1;
    std::size_t candidate_records = 0;
    std::size_t candidate_fields = 0;
};

} // namespace cw::server
