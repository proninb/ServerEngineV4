/*
 * Construction-local object-like preprocessing state.
 *
 * preprocessor owns only active #define bindings. Identifier spelling belongs
 * to string_table, file traversal belongs to the streaming frontend, and
 * semantic meaning belongs to Semantic.
 */
#pragma once

#include "../../server_status.hpp"
#include "../../string_id.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cw::server {

class string_table;

enum class preprocessor_expansion_kind : std::uint8_t {
    identifier,
    empty,
};

struct preprocessor_expansion final {
    preprocessor_expansion_kind kind =
        preprocessor_expansion_kind::identifier;
    string_id identifier{};
};

// Sparse construction-local table for object-like macro bindings. The table is
// single-owner: no mutex/atomics and no Project-resident or persistence state.
class preprocessor final {
public:
    explicit preprocessor(
        const string_table& strings) noexcept
        : strings(strings) {
    }

    preprocessor(const preprocessor&) = delete;
    preprocessor& operator=(const preprocessor&) = delete;

    // Equivalent to "#define NAME": NAME is defined with an empty replacement.
    [[nodiscard]] server_status define(
        string_id name) noexcept;

    // Equivalent to "#define NAME REPLACEMENT" for one identifier replacement.
    [[nodiscard]] server_status define(
        string_id name,
        string_id replacement) noexcept;

    // Undefining a valid but currently undefined name is a successful no-op.
    [[nodiscard]] server_status undefine(
        string_id name) noexcept;

    [[nodiscard]] bool defined(
        string_id name) const noexcept;

    // Recursively expands identifier-only object macros using current define
    // state. A replacement cycle terminates at the cycle entry, matching the
    // disabled-macro behavior needed to avoid recursive expansion.
    [[nodiscard]] server_status expand(
        string_id name,
        preprocessor_expansion& output) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return definition_count;
    }

private:
    struct define_slot final {
        string_id name{};

        // Invalid string_id means an empty replacement. Slot occupancy is
        // carried independently by name.
        string_id replacement{};
    };

    static_assert(sizeof(define_slot) == 8);

    enum class transition_kind : std::uint8_t {
        next,
        identifier,
        empty,
    };

    [[nodiscard]] static std::uint32_t hash_name(
        string_id name) noexcept;

    [[nodiscard]] std::size_t position(
        string_id name) const noexcept;

    [[nodiscard]] server_status ensure_capacity(
        std::size_t additional) noexcept;

    static void insert_slot(
        std::vector<define_slot>& target,
        define_slot slot) noexcept;

    [[nodiscard]] transition_kind transition(
        string_id name,
        string_id& next) const noexcept;

    [[nodiscard]] server_status define_impl(
        string_id name,
        string_id replacement) noexcept;

    const string_table& strings;
    std::vector<define_slot> definitions;
    std::size_t definition_count = 0;
};

}
