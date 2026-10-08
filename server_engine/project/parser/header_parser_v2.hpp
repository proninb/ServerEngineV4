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
#include <unordered_map>

namespace cw::server {

inline constexpr std::size_t parser_v2_scope_depth_limit = 256;

// Benchmark-only parser counters and sampled timings. Not part of Graph ABI.
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
struct header_parser_v2_profile final {
    // PARSER-V2-SEMANTIC-DETAIL-06: benchmark only; sampled subphase probes.
    std::uint64_t identity_lookup_calls = 0;
    std::uint64_t identity_lookup_samples = 0;
    double identity_lookup_sample_ms = 0;
    std::uint64_t named_resolution_calls = 0;
    std::uint64_t named_resolution_samples = 0;
    double named_resolution_sample_ms = 0;
    std::uint64_t intrinsic_parse_calls = 0;
    std::uint64_t intrinsic_parse_samples = 0;
    double intrinsic_parse_sample_ms = 0;
    std::uint64_t type_tail_calls = 0;
    std::uint64_t type_tail_samples = 0;
    double type_tail_sample_ms = 0;
    std::uint64_t member_name_calls = 0;
    std::uint64_t member_name_samples = 0;
    double member_name_sample_ms = 0;
    std::uint64_t member_initializer_calls = 0;
    std::uint64_t member_initializer_samples = 0;
    double member_initializer_sample_ms = 0;
    std::uint64_t member_append_calls = 0;
    std::uint64_t member_append_samples = 0;
    double member_append_sample_ms = 0;
    std::uint64_t advance_calls = 0;
    std::uint64_t advance_samples = 0;
    double advance_sample_ms = 0;
    std::uint64_t type_calls = 0;
    std::uint64_t type_samples = 0;
    double type_sample_ms = 0;
    std::uint64_t member_calls = 0;
    std::uint64_t member_samples = 0;
    double member_sample_ms = 0;
    std::uint64_t constructor_calls = 0;
    double constructor_ms = 0;
    std::uint64_t graph_define_calls = 0;
    std::uint64_t graph_defined_members = 0;
    double graph_define_ms = 0;
};
#endif

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
        parser_v2_failure* failure = nullptr
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
        , header_parser_v2_profile* profile = nullptr
#endif
        ) noexcept;

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
        // Returns existing.size() when absent. Reuses the duplicate detector:
        // tiny records scan contiguous members, larger records use one index.
        [[nodiscard]] std::size_t find(
            std::span<const member_record> existing,
            string_id name) const noexcept;

        [[nodiscard]] server_status insert(
            std::span<const member_record> existing,
            string_id name,
            bool& inserted) noexcept;

    private:
        [[nodiscard]] static std::uint32_t hash(
            string_id name) noexcept;

        void insert_slot(
            string_id name,
            std::uint32_t index_plus_one) noexcept;

        [[nodiscard]] server_status rebuild(
            std::span<const member_record> existing,
            std::size_t required) noexcept;

        // Zero is empty; nonzero is one-based index in member_record[].
        // Storage remains 4 bytes per slot; no duplicate name table.
        std::vector<std::uint32_t> slots;
    };

    static constexpr std::size_t
        record_member_linear_limit = 32;

    // HEADER-V2-REFERENCE-08: a name is stored only until its member is
    // available; G always receives the resolved one-based member operand.
    struct constructor_operation final {
        construction_value value{};
        file_id file{};
        source_range source{};
        string_id reference_name{};
        file_id reference_file{};
        source_range reference_source{};
    };

    struct waiting_reference final {
        std::size_t target = 0;
        std::uint64_t generation = 0;
        file_id file{};
        source_range source{};
    };

    struct record_reference_state final {
        // Only forward references allocate these sparse indices. No second
        // whole-record traversal is performed at the closing brace.
        std::unordered_multimap<std::uint32_t, waiting_reference> waiting;
        std::unordered_map<std::size_t, std::uint64_t> generations;
    };

    using pending_constructor_operations =
        std::unordered_map<std::uint32_t, constructor_operation>;

    // HEADER-V2-NESTED-CONSTRUCTOR-10: sparse, record-local paths only.
    // The common case (no nested constructor body assignments) allocates none.
    struct nested_path_step final {
        string_id name{};
        std::uint64_t index = 0;
    };
    struct nested_operation final {
        string_id root{};
        std::vector<nested_path_step> path;
        construction_value value{};
        file_id file{};
        source_range source{};
    };
    struct record_nested_state final {
        identity_ref owner{};
        std::unordered_map<std::uint32_t, std::vector<nested_operation>> waiting;
        std::vector<constructor_default> completed;
        // Allocate the extra index only for unusually large nested bodies.
        std::unordered_map<std::uint32_t, std::size_t> completed_lookup;
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

    // HEADER-V2-TYPEDEF-07: restricted intrinsic aliases.
    [[nodiscard]] server_status parse_typedef(
        identity_ref scope) noexcept;

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

    [[nodiscard]] server_status parse_intrinsic(
        intrinsic_type& output) noexcept;

    [[nodiscard]] server_status parse_type(
        identity_ref scope,
        resolved_type& output) noexcept;

    [[nodiscard]] server_status parse_array_suffix(
        resolved_type& output) noexcept;

    [[nodiscard]] server_status parse_scalar_constant(
        construction_value& output) noexcept;

    [[nodiscard]] server_status parse_reference_name(
        constructor_operation& output) noexcept;

    [[nodiscard]] server_status parse_construction_operand(
        constructor_operation& output,
        bool allow_reference_name) noexcept;

    [[nodiscard]] server_status parse_member_initializer(
        type_ref target,
        constructor_operation& output) noexcept;

    [[nodiscard]] server_status apply_record_operand(
        std::size_t target,
        const constructor_operation& input_value,
        std::span<const member_record> members,
        std::vector<construction_value>& construction,
        const record_member_name_set& names,
        record_reference_state& references) noexcept;

    [[nodiscard]] server_status resolve_waiting_references(
        string_id source_name,
        std::span<const member_record> members,
        std::vector<construction_value>& construction,
        record_reference_state& references) noexcept;

    [[nodiscard]] server_status apply_nested_operation(
        const nested_operation& operation,
        std::span<const member_record> members,
        const record_member_name_set& names,
        record_nested_state& nested) noexcept;

    [[nodiscard]] server_status append_nested_operation(
        nested_operation&& operation,
        std::span<const member_record> members,
        const record_member_name_set& names,
        record_nested_state& nested) noexcept;

    [[nodiscard]] server_status resolve_waiting_nested(
        string_id root,
        std::span<const member_record> members,
        const record_member_name_set& names,
        record_nested_state& nested) noexcept;

    [[nodiscard]] server_status parse_member_declarator(
        resolved_type type,
        graph_member_access access,
        std::vector<member_record>& members,
        std::vector<construction_value>& construction,
        pending_constructor_operations& pending,
        record_reference_state& references,
        record_nested_state& nested,
        record_member_name_set& names,
        bool virtual_prefix,
        bool base_polymorphic,
        bool& declares_virtual) noexcept;

    [[nodiscard]] server_status parse_method_tail(
        bool virtual_prefix,
        bool base_polymorphic,
        bool& declares_virtual,
        bool conversion = false,
        bool assignment_operator = false,
        bool subscript_operator = false,
        bool binary_operator = false) noexcept;

    // OLD-compatible declaration-only named operators. No Graph methods.
    [[nodiscard]] server_status parse_named_operator(
        string_id enclosing_record,
        bool virtual_prefix,
        bool base_polymorphic,
        bool& declares_virtual) noexcept;

    [[nodiscard]] server_status parse_member(
        identity_ref scope,
        string_id enclosing_record,
        graph_member_access access,
        std::vector<member_record>& members,
        std::vector<construction_value>& construction,
        pending_constructor_operations& pending,
        record_reference_state& references,
        record_nested_state& nested,
        record_member_name_set& names,
        bool virtual_prefix,
        bool base_polymorphic,
        bool& declares_virtual) noexcept;

    [[nodiscard]] server_status parse_constructor(
        string_id record_name,
        std::span<const member_record> members,
        const record_member_name_set& names,
        std::vector<construction_value>& construction,
        pending_constructor_operations& pending,
        record_reference_state& references,
        record_nested_state& nested) noexcept;

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
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
    header_parser_v2_profile* profile = nullptr;
#endif
};

}
