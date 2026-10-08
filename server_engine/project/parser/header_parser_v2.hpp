/*
 * Clean Header Parser V2.
 *
 * V2 consumes prepared semantic tokens only. Grammar and source-language
 * semantic decisions live here; physical input preparation does not.
 */
#pragma once

#include "../frontend/preprocessor_v2.hpp"
#include "../frontend/source_range.hpp"
#include "../graph/graph.hpp"
#include "../semantic/identity.hpp"
#include "../../server_status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cw::server {

inline constexpr std::size_t parser_v2_scope_depth_limit = 256;

enum class parser_v2_failure_kind : std::uint8_t {
    none = 0,
    syntax,
    semantic,
    unsupported,
};

struct parser_v2_failure final {
    parser_v2_failure_kind kind =
        parser_v2_failure_kind::none;
    file_id file{};
    source_range source;
    std::string_view detail;
};

class header_parser_v2 final {
public:
    header_parser_v2(
        semantic_preprocessor_v2& input,
        identity_space& identities,
        graph& G,
        parser_v2_failure* failure = nullptr) noexcept;

    header_parser_v2(
        const header_parser_v2&) = delete;

    header_parser_v2& operator=(
        const header_parser_v2&) = delete;

    [[nodiscard]] server_status parse() noexcept;

private:
    struct resolved_type final {
        type_ref type{};
        bool named = false;
        bool complete = true;
        bool indirect = false;
        bool is_reference = false;
    };

    // Record-local duplicate detector. Tiny records stay as a contiguous scan;
    // larger records promote once to an ephemeral open-addressed name set.
    struct record_member_name_set final {
        [[nodiscard]] server_status insert(
            std::span<const member_record> existing,
            string_id name,
            bool& inserted) noexcept;

    private:
        [[nodiscard]] static std::uint32_t hash(
            string_id name) noexcept;

        void insert_slot(
            string_id name) noexcept;

        [[nodiscard]] server_status rebuild(
            std::span<const member_record> existing,
            std::size_t required) noexcept;

        std::vector<string_id> slots;
    };

    static constexpr std::size_t
        record_member_linear_limit = 32;

    struct constructor_operation final {
        string_id target{};
        construction_value value{};
    };

    [[nodiscard]] const prepared_token* current() const noexcept;
    [[nodiscard]] bool at(token_kind kind) const noexcept;
    [[nodiscard]] server_status consume() noexcept;

    [[nodiscard]] server_status expect(
        token_kind kind,
        std::string_view detail) noexcept;

    [[nodiscard]] server_status fail(
        parser_v2_failure_kind kind,
        std::string_view detail) noexcept;

    [[nodiscard]] server_status fail_at(
        parser_v2_failure_kind kind,
        std::string_view detail,
        file_id file,
        source_range source) noexcept;

    [[nodiscard]] server_status parse_scope(
        identity_ref scope,
        bool closing_brace,
        std::size_t depth) noexcept;

    [[nodiscard]] server_status parse_namespace(
        identity_ref scope,
        std::size_t depth) noexcept;

    [[nodiscard]] server_status parse_record(
        identity_ref scope) noexcept;

    [[nodiscard]] static graph_record_kind record_kind(
        token_kind kind) noexcept;

    [[nodiscard]] static graph_member_access default_access(
        graph_record_kind kind) noexcept;

    [[nodiscard]] server_status remember_record_kind(
        identity_ref identity,
        graph_record_kind kind) noexcept;

    [[nodiscard]] server_status resolve_named_type(
        identity_ref identity,
        resolved_type& output) noexcept;

    [[nodiscard]] server_status apply_qualifiers(
        bool const_qualified,
        bool volatile_qualified,
        resolved_type& output) noexcept;

    [[nodiscard]] server_status parse_type_tail(
        resolved_type& output,
        bool const_qualified = false,
        bool volatile_qualified = false) noexcept;

    [[nodiscard]] server_status parse_type(
        identity_ref scope,
        resolved_type& output) noexcept;

    [[nodiscard]] server_status parse_array_suffix(
        resolved_type& output) noexcept;

    [[nodiscard]] server_status parse_scalar_constant(
        construction_value& output) noexcept;

    [[nodiscard]] server_status parse_member_initializer(
        type_ref target,
        construction_value& output) noexcept;

    [[nodiscard]] server_status parse_member_declarator(
        resolved_type type,
        graph_member_access access,
        std::vector<member_record>& members,
        std::vector<construction_value>& construction,
        record_member_name_set& names,
        bool virtual_prefix,
        bool base_polymorphic,
        bool& declares_virtual) noexcept;

    [[nodiscard]] server_status parse_method_tail(
        bool virtual_prefix,
        bool base_polymorphic,
        bool& declares_virtual,
        bool conversion = false) noexcept;

    [[nodiscard]] server_status parse_member(
        identity_ref scope,
        graph_member_access access,
        std::vector<member_record>& members,
        std::vector<construction_value>& construction,
        record_member_name_set& names,
        bool virtual_prefix,
        bool base_polymorphic,
        bool& declares_virtual) noexcept;

    [[nodiscard]] server_status parse_constructor(
        string_id record_name,
        std::vector<constructor_operation>& operations) noexcept;

    [[nodiscard]] server_status apply_constructor_operations(
        std::span<const member_record> members,
        std::vector<construction_value>& construction,
        std::span<const constructor_operation> operations) noexcept;

    [[nodiscard]] identity_ref find_type_identity(
        identity_ref scope,
        string_id name) const noexcept;

    semantic_preprocessor_v2& input;
    identity_space& identities;
    graph& G;

    // One byte per semantic WHO touched by this Header Parser V2 instance.
    // 0 = unseen, 1 = struct/class family, 2 = union.
    std::vector<std::uint8_t>
        record_kind_slots;

    parser_v2_failure* failure = nullptr;
};

}
