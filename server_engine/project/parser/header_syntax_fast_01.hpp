#pragma once
// HEADER-SYNTAX-FAST-01: independent syntax-only exploratory scanner.
// No string interning, semantic resolution, G construction or persistence.
// Input is an already-decoded effective token-kind sequence. It is NOT yet a
// complete Header V2 grammar and MUST NOT replace production parsing.
#include "../frontend/lexical_token.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace cw::server {

struct header_syntax_record_01 final {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;       // one past closing brace (or semicolon)
    std::uint32_t member_begin = 0;
    std::uint32_t member_count = 0;
    token_kind kind = token_kind::invalid;
};

struct header_syntax_member_01 final {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;       // one past terminating semicolon or method body
    std::uint32_t owner = 0;     // record index
    bool method_like = false;
};

struct header_syntax_result_01 final {
    std::vector<header_syntax_record_01> records;
    std::vector<header_syntax_member_01> members;
    std::uint64_t tokens = 0;
    std::uint64_t namespaces = 0;
    std::uint64_t skipped_declarations = 0;
    std::uint64_t malformed = 0;
    void clear() noexcept {
        records.clear(); members.clear();
        tokens = namespaces = skipped_declarations = malformed = 0;
    }
};

// Deliberately recognizes record boundaries and balanced member spans, not
// semantic member declarations. Unknown forms are counted, never accepted as
// proof of Header V2 syntax coverage.
class header_syntax_fast_01 final {
public:
    static bool scan(std::span<const token_kind> tokens,
                     header_syntax_result_01& out) {
        out.clear();
        out.tokens = tokens.size();
        if (tokens.size() > std::numeric_limits<std::uint32_t>::max()) return false;
        return scope(tokens, 0, tokens.size(), out);
    }
private:
    static bool record_start(token_kind k) noexcept {
        return k == token_kind::kw_struct || k == token_kind::kw_class ||
               k == token_kind::kw_union;
    }
    static std::size_t matching(std::span<const token_kind> t, std::size_t start,
                                std::size_t limit, token_kind open, token_kind close) noexcept {
        std::size_t depth = 0;
        for (std::size_t i = start; i < limit; ++i) {
            if (t[i] == open) ++depth;
            else if (t[i] == close) { if (depth == 0) return limit; if (--depth == 0) return i; }
        }
        return limit;
    }
    static std::size_t skip_statement(std::span<const token_kind> t,
                                      std::size_t i, std::size_t end) noexcept {
        for (; i < end; ++i) {
            if (t[i] == token_kind::semicolon) return i + 1;
            if (t[i] == token_kind::l_brace) {
                const auto x = matching(t, i, end, token_kind::l_brace, token_kind::r_brace);
                return x < end ? x + 1 : end;
            }
        }
        return end;
    }
    static bool scan_record(std::span<const token_kind> t, std::size_t& pos,
                            std::size_t end, header_syntax_result_01& out) {
        const auto begin = pos;
        std::size_t open = pos + 1;
        while (open < end && t[open] != token_kind::l_brace &&
               t[open] != token_kind::semicolon) ++open;
        if (open == end) { ++out.malformed; return false; }
        if (t[open] == token_kind::semicolon) {
            // forward declaration, no members
            out.records.push_back({static_cast<std::uint32_t>(begin),
                static_cast<std::uint32_t>(open + 1),
                static_cast<std::uint32_t>(out.members.size()), 0, t[begin]});
            pos = open + 1;
            return true;
        }
        const auto close = matching(t, open, end, token_kind::l_brace, token_kind::r_brace);
        if (close == end) { ++out.malformed; return false; }
        const auto owner = static_cast<std::uint32_t>(out.records.size());
        const auto first_member = static_cast<std::uint32_t>(out.members.size());
        out.records.push_back({static_cast<std::uint32_t>(begin),
            static_cast<std::uint32_t>(close + 1), first_member, 0, t[begin]});
        // Keep nested records in the record table; member descriptor spans
        // are stable contiguous only for non-nested records in this slice.
        std::size_t i = open + 1;
        while (i < close) {
            if (t[i] == token_kind::semicolon) { ++i; continue; }
            if ((t[i] == token_kind::kw_public || t[i] == token_kind::kw_private ||
                 t[i] == token_kind::kw_protected) && i + 1 < close &&
                t[i + 1] == token_kind::colon) { i += 2; continue; }
            if (record_start(t[i])) {
                if (!scan_record(t, i, close, out)) return false;
                continue;
            }
            const auto start = i;
            bool paren = false;
            std::size_t paren_depth = 0, bracket_depth = 0, brace_depth = 0;
            for (; i < close; ++i) {
                const auto k = t[i];
                if (k == token_kind::l_paren) { ++paren_depth; paren = true; }
                else if (k == token_kind::r_paren) {
                    if (!paren_depth) { ++out.malformed; return false; }
                    --paren_depth;
                } else if (k == token_kind::l_bracket) ++bracket_depth;
                else if (k == token_kind::r_bracket) {
                    if (!bracket_depth) { ++out.malformed; return false; }
                    --bracket_depth;
                } else if (k == token_kind::l_brace) ++brace_depth;
                else if (k == token_kind::r_brace) {
                    if (!brace_depth) { ++out.malformed; return false; }
                    --brace_depth;
                }
                if (!paren_depth && !bracket_depth && !brace_depth &&
                    k == token_kind::semicolon) { ++i; break; }
            }
            if (i == start || i > close || paren_depth || bracket_depth || brace_depth) {
                ++out.malformed; return false;
            }
            out.members.push_back({static_cast<std::uint32_t>(start),
                static_cast<std::uint32_t>(i), owner, paren});
        }
        out.records[owner].member_count = static_cast<std::uint32_t>(
            out.members.size() - first_member); // includes nested descendants
        pos = close + 1;
        if (pos < end && t[pos] == token_kind::semicolon) ++pos;
        return true;
    }
    static bool scope(std::span<const token_kind> t, std::size_t begin,
                      std::size_t end, header_syntax_result_01& out) {
        for (std::size_t i = begin; i < end;) {
            if (record_start(t[i])) {
                if (!scan_record(t, i, end, out)) return false;
            } else if (t[i] == token_kind::kw_namespace) {
                ++out.namespaces;
                std::size_t j = i + 1;
                while (j < end && t[j] != token_kind::l_brace &&
                       t[j] != token_kind::semicolon) ++j;
                if (j == end) { ++out.malformed; return false; }
                if (t[j] == token_kind::l_brace) {
                    auto close = matching(t, j, end, token_kind::l_brace, token_kind::r_brace);
                    if (close == end) { ++out.malformed; return false; }
                    if (!scope(t, j + 1, close, out)) return false;
                    i = close + 1;
                } else i = j + 1;
            } else {
                ++out.skipped_declarations;
                const auto next = skip_statement(t, i, end);
                if (next <= i) return false;
                i = next;
            }
        }
        return true;
    }
};
}
