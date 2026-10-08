#include "header_parser_v2.hpp"

#include "../graph/construction_semantics.hpp"

#include <limits>
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
#include <chrono>
#endif
#include <algorithm>
#include <string>
#include <utility>

namespace cw::server {
namespace {

// Header intrinsic grammar: the same token family as OLD parse_intrinsic().
[[nodiscard]] constexpr bool header_v2_builtin_start(token_kind kind) noexcept {
    switch (kind) {
    case token_kind::kw_void:
    case token_kind::kw_bool:
    case token_kind::kw_char:
    case token_kind::kw_wchar_t:
    case token_kind::kw_char8_t:
    case token_kind::kw_char16_t:
    case token_kind::kw_char32_t:
    case token_kind::kw_short:
    case token_kind::kw_int:
    case token_kind::kw_long:
    case token_kind::kw_signed:
    case token_kind::kw_unsigned:
    case token_kind::kw_float:
    case token_kind::kw_double:
        return true;
    default:
        return false;
    }
}

#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
struct header_v2_timed_probe final {
    double* elapsed_ms = nullptr;
    std::chrono::steady_clock::time_point started{};

    explicit header_v2_timed_probe(double* value) noexcept
        : elapsed_ms(value),
          started(value ? std::chrono::steady_clock::now()
                        : std::chrono::steady_clock::time_point{}) {}

    ~header_v2_timed_probe() noexcept {
        if (elapsed_ms != nullptr) {
            *elapsed_ms += std::chrono::duration<double, std::milli>{
                std::chrono::steady_clock::now() - started}.count();
        }
    }
};
// PARSER-V2-SEMANTIC-DETAIL-06: sample every 128th operation.
// An independent counter is used per operation; measured regions can be
// nested (in particular advance inside parse_type/parse_member_declarator).
// Sum of these metrics is NOT an exclusive, additive wall-time breakdown.
struct header_v2_detail_probe final {
    using counter_member = std::uint64_t header_parser_v2_profile::*;
    using time_member = double header_parser_v2_profile::*;

    [[nodiscard]] static double* choose(
        header_parser_v2_profile* profile,
        counter_member calls,
        counter_member samples,
        time_member elapsed) noexcept {
        if (profile == nullptr) return nullptr;
        auto& value = profile->*calls;
        ++value;
        if ((value & 127u) != 0) return nullptr;
        ++(profile->*samples);
        return &(profile->*elapsed);
    }

    header_v2_timed_probe timer;

    header_v2_detail_probe(
        header_parser_v2_profile* profile,
        counter_member calls,
        counter_member samples,
        time_member elapsed) noexcept
        : timer(choose(profile, calls, samples, elapsed)) {}
};
#endif

} // namespace

header_parser_v2::header_parser_v2(
    semantic_preprocessor_v2& input_value,
    identity_space& identities_value,
    graph& graph_value,
    parser_v2_failure* failure_value
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
    , header_parser_v2_profile* profile_value
#endif
    ) noexcept
    : input(input_value),
      identities(identities_value),
      G(graph_value),
      failure(failure_value)
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
    , profile(profile_value)
#endif
    {
}

const prepared_token*
header_parser_v2::current() const noexcept {
    return input.current();
}

bool header_parser_v2::at(
    token_kind kind) const noexcept {

    return input.at(kind);
}

server_status header_parser_v2::consume() noexcept {
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
    header_v2_detail_probe perf06_probe(
        profile,
        &header_parser_v2_profile::advance_calls,
        &header_parser_v2_profile::advance_samples,
        &header_parser_v2_profile::advance_sample_ms);
#endif
    return input.advance();
}

server_status header_parser_v2::fail(
    parser_v2_failure_kind kind,
    std::string_view detail) noexcept {

    if (const auto* token = current();
        token != nullptr) {

        return fail_at(
            kind,
            detail,
            token->file,
            {token->source_offset, token->source_length});
    }

    return fail_at(kind, detail, {}, {});
}

server_status header_parser_v2::fail_at(
    parser_v2_failure_kind kind,
    std::string_view detail,
    file_id file,
    source_range source) noexcept {

    if (failure != nullptr) {
        *failure = {};
        failure->kind = kind;
        failure->detail = detail;
        failure->file = file;
        failure->source = source;
    }

    return server_status::
        project_configuration_invalid;
}

server_status header_parser_v2::expect(
    token_kind kind,
    std::string_view detail) noexcept {

    if (!at(kind)) {
        return fail(
            parser_v2_failure_kind::syntax,
            detail);
    }

    return consume();
}

identity_ref header_parser_v2::find_type_identity(
    identity_ref scope,
    string_id name) const noexcept {

    if (!scope || !name) {
        return {};
    }

    auto current_scope =
        scope;

    for (;;) {
        if (const auto identity =
                identities.find(
                    current_scope,
                    name,
                    identity_kind::type);
            identity) {

            return identity;
        }

        if (current_scope ==
            identities.root()) {

            return {};
        }

        identity_record record;

        if (!identities.record(
                current_scope,
                record) ||
            !record.parent) {

            return {};
        }

        current_scope =
            record.parent;
    }
}

graph_record_kind header_parser_v2::record_kind(
    token_kind kind) noexcept {

    switch (kind) {
    case token_kind::kw_class:
        return graph_record_kind::class_type;

    case token_kind::kw_union:
        return graph_record_kind::union_type;

    case token_kind::kw_struct:
    default:
        return graph_record_kind::struct_type;
    }
}

graph_member_access header_parser_v2::default_access(
    graph_record_kind kind) noexcept {

    return kind ==
            graph_record_kind::class_type
        ? graph_member_access::private_access
        : graph_member_access::public_access;
}

server_status header_parser_v2::remember_record_kind(
    identity_ref identity,
    graph_record_kind kind) noexcept {

    if (!identity ||
        identity.kind() !=
            identity_kind::type) {

        return server_status::
            project_configuration_invalid;
    }

    const auto slot =
        static_cast<std::size_t>(
            identity.slot());

    const auto family =
        kind ==
            graph_record_kind::union_type
        ? std::uint8_t{2}
        : std::uint8_t{1};

    try {
        if (slot >=
            record_kind_slots.size()) {

            record_kind_slots.resize(
                slot + 1);
        }
    }
    catch (...) {
        return server_status::io_error;
    }

    auto& value =
        record_kind_slots[
            slot];

    if (value != 0 &&
        value != family) {

        return fail(
            parser_v2_failure_kind::semantic,
            "Header Parser V2 record key conflicts with prior declaration");
    }

    value =
        family;

    return server_status::success;
}

server_status header_parser_v2::resolve_named_type(
    identity_ref identity,
    resolved_type& output) noexcept {

    output = {};

    if (!identity ||
        identity.kind() !=
            identity_kind::type) {

        return fail(
            parser_v2_failure_kind::semantic,
            "Header Parser V2 named type is unknown");
    }

    const auto handle =
        G.find_type(
            identity);

    if (handle) {
        const auto* entry =
            G.find(
                handle);

        // HEADER-V2-TYPEDEF-07: OLD folds intrinsic aliases to the scalar type.
        if (entry != nullptr &&
            entry->kind == graph_type_kind::intrinsic_alias) {

            output.type = G.intrinsic(entry->alias_intrinsic());
            output.complete = true;
            return output.type
                ? server_status::success
                : fail(parser_v2_failure_kind::semantic,
                    "Header Parser V2 intrinsic alias is invalid");
        }

        if (entry == nullptr ||
            entry->kind !=
                graph_type_kind::record) {

            return fail(
                parser_v2_failure_kind::semantic,
                "Header Parser V2 named type is unknown");
        }

        output.type =
            G.named(
                handle);

        output.complete =
            entry->defined();
    }
    else {
        output.type =
            G.named(
                identity);

        output.complete =
            false;
    }

    output.named =
        true;

    return output.type
        ? server_status::success
        : fail(
            parser_v2_failure_kind::semantic,
            "Header Parser V2 named type could not be represented");
}

server_status header_parser_v2::apply_qualifiers(
    bool const_qualified,
    bool volatile_qualified,
    resolved_type& output) noexcept {

    if (output.is_reference &&
        (const_qualified ||
         volatile_qualified)) {

        return fail(
            parser_v2_failure_kind::semantic,
            "Header Parser V2 cannot cv-qualify a reference type");
    }

    const auto apply =
        [&](derived_type_kind kind)
            -> server_status {

            type_ref wrapped;

            const auto status =
                G.derive_resolved(
                    output.type,
                    kind,
                    0,
                    wrapped);

            if (!succeeded(status) ||
                !wrapped) {

                return fail(
                    parser_v2_failure_kind::semantic,
                    "Header Parser V2 cv-qualified type is invalid");
            }

            output.type =
                wrapped;

            return server_status::success;
        };

    if (const_qualified) {
        const auto status =
            apply(
                derived_type_kind::
                    const_qualified);

        if (!succeeded(status)) {
            return status;
        }
    }

    if (volatile_qualified) {
        const auto status =
            apply(
                derived_type_kind::
                    volatile_qualified);

        if (!succeeded(status)) {
            return status;
        }
    }

    return server_status::success;
}

server_status header_parser_v2::parse_type_tail(
    resolved_type& output,
    bool const_qualified,
    bool volatile_qualified) noexcept {

    while (at(token_kind::kw_const) ||
           at(token_kind::kw_volatile)) {

        const_qualified |=
            at(token_kind::kw_const);

        volatile_qualified |=
            at(token_kind::kw_volatile);

        const auto status =
            consume();

        if (!succeeded(status)) {
            return status;
        }
    }

    auto status =
        apply_qualifiers(
            const_qualified,
            volatile_qualified,
            output);

    if (!succeeded(status)) {
        return status;
    }

    while (at(token_kind::star) ||
           at(token_kind::ampersand) ||
           at(token_kind::logical_and)) {

        const auto kind =
            at(token_kind::star)
            ? derived_type_kind::pointer
            : at(token_kind::ampersand)
                ? derived_type_kind::
                    lvalue_reference
                : derived_type_kind::
                    rvalue_reference;

        if (output.is_reference) {
            return fail(
                parser_v2_failure_kind::semantic,
                "Header Parser V2 cannot derive from a reference type");
        }

        type_ref wrapped;

        status =
            G.derive_resolved(
                output.type,
                kind,
                0,
                wrapped);

        if (!succeeded(status) ||
            !wrapped) {

            return fail(
                parser_v2_failure_kind::semantic,
                "Header Parser V2 declarator produces an invalid type");
        }

        output.type =
            wrapped;

        output.indirect =
            true;

        output.is_reference =
            kind ==
                derived_type_kind::
                    lvalue_reference ||
            kind ==
                derived_type_kind::
                    rvalue_reference;

        status =
            consume();

        if (!succeeded(status)) {
            return status;
        }

        if (kind ==
            derived_type_kind::pointer) {

            bool pointer_const = false;
            bool pointer_volatile = false;

            while (at(token_kind::kw_const) ||
                   at(token_kind::kw_volatile)) {

                pointer_const |=
                    at(token_kind::kw_const);

                pointer_volatile |=
                    at(token_kind::kw_volatile);

                status =
                    consume();

                if (!succeeded(status)) {
                    return status;
                }
            }

            status =
                apply_qualifiers(
                    pointer_const,
                    pointer_volatile,
                    output);

            if (!succeeded(status)) {
                return status;
            }
        }
    }

    if (output.named &&
        !output.complete &&
        !output.indirect) {

        return fail(
            parser_v2_failure_kind::semantic,
            "Header Parser V2 incomplete type cannot be stored by value");
    }

    return server_status::success;
}

// HEADER-V2-PARITY-03: match OLD's intrinsic spellings and modifiers.
// This only selects existing intrinsic_type values: no new Graph or Runtime ABI.
server_status header_parser_v2::parse_intrinsic(
    intrinsic_type& output) noexcept {

    output = intrinsic_type::none;
    bool is_signed = false;
    bool is_unsigned = false;
    bool is_short = false;
    std::uint32_t long_count = 0;

    while (at(token_kind::kw_signed) ||
           at(token_kind::kw_unsigned) ||
           at(token_kind::kw_short) ||
           at(token_kind::kw_long)) {
        if (at(token_kind::kw_signed)) {
            if (is_signed || is_unsigned) {
                return fail(parser_v2_failure_kind::syntax,
                    "Invalid signed/unsigned type specifier combination");
            }
            is_signed = true;
        }
        else if (at(token_kind::kw_unsigned)) {
            if (is_signed || is_unsigned) {
                return fail(parser_v2_failure_kind::syntax,
                    "Invalid signed/unsigned type specifier combination");
            }
            is_unsigned = true;
        }
        else if (at(token_kind::kw_short)) {
            if (is_short || long_count != 0) {
                return fail(parser_v2_failure_kind::syntax,
                    "Invalid short/long type specifier combination");
            }
            is_short = true;
        }
        else {
            if (is_short || long_count == 2) {
                return fail(parser_v2_failure_kind::syntax,
                    "Invalid long type specifier combination");
            }
            ++long_count;
        }
        const auto advanced = consume();
        if (!succeeded(advanced)) return advanced;
    }

    // MSVC spelling is an identifier by lexical contract.
    if (input.contextual_identifier("__int64")) {
        if (is_short || long_count != 0) {
            return fail(parser_v2_failure_kind::syntax,
                "__int64 cannot use short/long modifiers");
        }
        output = is_unsigned
            ? intrinsic_type::unsigned_long_long
            : intrinsic_type::signed_long_long;
        return consume();
    }

    if (at(token_kind::kw_char)) {
        if (is_short || long_count != 0) {
            return fail(parser_v2_failure_kind::syntax,
                "char cannot use short/long modifiers");
        }
        output = is_unsigned
            ? intrinsic_type::unsigned_char
            : is_signed ? intrinsic_type::signed_char
                        : intrinsic_type::char_type;
        return consume();
    }

    if (at(token_kind::kw_double)) {
        if (is_signed || is_unsigned || is_short || long_count > 1) {
            return fail(parser_v2_failure_kind::syntax,
                "Invalid double type specifier combination");
        }
        output = long_count == 1
            ? intrinsic_type::long_double_type
            : intrinsic_type::double_type;
        return consume();
    }

    if (at(token_kind::kw_int)) {
        const auto advanced = consume();
        if (!succeeded(advanced)) return advanced;
    }
    else if (!is_signed && !is_unsigned && !is_short && long_count == 0) {
        const auto* token = current();
        switch (token != nullptr ? token->kind : token_kind::invalid) {
        case token_kind::kw_void: output = intrinsic_type::void_type; break;
        case token_kind::kw_bool: output = intrinsic_type::bool_type; break;
        case token_kind::kw_wchar_t: output = intrinsic_type::wchar_type; break;
        case token_kind::kw_char8_t: output = intrinsic_type::char8_type; break;
        case token_kind::kw_char16_t: output = intrinsic_type::char16_type; break;
        case token_kind::kw_char32_t: output = intrinsic_type::char32_type; break;
        case token_kind::kw_float: output = intrinsic_type::float_type; break;
        default:
            return fail(parser_v2_failure_kind::syntax,
                "Expected supported intrinsic type");
        }
        return consume();
    }

    if (long_count == 2) {
        output = is_unsigned
            ? intrinsic_type::unsigned_long_long
            : intrinsic_type::signed_long_long;
    }
    else if (long_count == 1) {
        output = is_unsigned
            ? intrinsic_type::unsigned_long
            : intrinsic_type::signed_long;
    }
    else if (is_short) {
        output = is_unsigned
            ? intrinsic_type::unsigned_short
            : intrinsic_type::signed_short;
    }
    else {
        output = is_unsigned
            ? intrinsic_type::unsigned_int
            : intrinsic_type::signed_int;
    }
    return server_status::success;
}

server_status header_parser_v2::parse_type(
    identity_ref scope,
    resolved_type& output) noexcept {
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
    // Every 128th call only: low-overhead estimate, NOT exact wall-time.
    if (profile != nullptr) ++profile->type_calls;
    const bool sampled = profile != nullptr &&
        (profile->type_calls % 128u) == 0;
    header_v2_timed_probe probe(sampled ? &profile->type_sample_ms : nullptr);
    if (sampled) ++profile->type_samples;
#endif

    output = {};

    bool const_qualified = false;
    bool volatile_qualified = false;

    while (at(token_kind::kw_const) ||
           at(token_kind::kw_volatile)) {

        const_qualified |=
            at(token_kind::kw_const);

        volatile_qualified |=
            at(token_kind::kw_volatile);

        const auto status =
            consume();

        if (!succeeded(status)) {
            return status;
        }
    }

    const auto* first = current();
    if ((first != nullptr && header_v2_builtin_start(first->kind)) ||
        input.contextual_identifier("__int64")) {
        intrinsic_type intrinsic = intrinsic_type::none;
        server_status status;
        {
        #ifdef CW_HEADER_V2_SEMANTIC_PROFILE
            header_v2_detail_probe perf06_probe(
                profile,
                &header_parser_v2_profile::intrinsic_parse_calls,
                &header_parser_v2_profile::intrinsic_parse_samples,
                &header_parser_v2_profile::intrinsic_parse_sample_ms);
        #endif
            status = parse_intrinsic(intrinsic);
        }
        if (!succeeded(status)) return status;
        output.type = G.intrinsic(intrinsic);
        if (!output.type) {
            return fail(parser_v2_failure_kind::semantic,
                "Header Parser V2 could not create intrinsic type");
        }
    }
    else {
        const auto* token =
            current();

        if (token == nullptr ||
            token->kind !=
                token_kind::identifier ||
            !token->identifier) {

            return fail(
                parser_v2_failure_kind::syntax,
                "Header Parser V2 expected a supported type");
        }

        identity_ref identity;
        {
        #ifdef CW_HEADER_V2_SEMANTIC_PROFILE
            header_v2_detail_probe perf06_probe(
                profile,
                &header_parser_v2_profile::identity_lookup_calls,
                &header_parser_v2_profile::identity_lookup_samples,
                &header_parser_v2_profile::identity_lookup_sample_ms);
        #endif
            identity = find_type_identity(scope, token->identifier);
        }

        if (!identity) {
            return fail(
                parser_v2_failure_kind::semantic,
                "Header Parser V2 named type is unknown");
        }

        server_status resolved;
        {
        #ifdef CW_HEADER_V2_SEMANTIC_PROFILE
            header_v2_detail_probe perf06_probe(
                profile,
                &header_parser_v2_profile::named_resolution_calls,
                &header_parser_v2_profile::named_resolution_samples,
                &header_parser_v2_profile::named_resolution_sample_ms);
        #endif
            resolved = resolve_named_type(identity, output);
        }

        if (!succeeded(resolved)) {
            return resolved;
        }

        const auto status =
            consume();

        if (!succeeded(status)) {
            return status;
        }
    }

    server_status tail_status;
    {
    #ifdef CW_HEADER_V2_SEMANTIC_PROFILE
        header_v2_detail_probe perf06_probe(
            profile,
            &header_parser_v2_profile::type_tail_calls,
            &header_parser_v2_profile::type_tail_samples,
            &header_parser_v2_profile::type_tail_sample_ms);
    #endif
        tail_status = parse_type_tail(output, const_qualified, volatile_qualified);
    }
    return tail_status;
}


std::uint32_t
header_parser_v2::record_member_name_set::hash(
    string_id name) noexcept {

    auto value =
        name.value();

    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;

    return value;
}

void
header_parser_v2::record_member_name_set::insert_slot(
    string_id name,
    std::uint32_t index_plus_one) noexcept {

    const auto mask = slots.size() - 1;
    auto position = static_cast<std::size_t>(hash(name)) & mask;

    while (slots[position] != 0) {
        position = (position + 1) & mask;
    }
    slots[position] = index_plus_one;
}

server_status
header_parser_v2::record_member_name_set::rebuild(
    std::span<const member_record> existing,
    std::size_t required) noexcept {

    if (required == 0) return server_status::success;
    if (required > (std::numeric_limits<std::uint32_t>::max)()) {
        return server_status::io_error;
    }
    if (required > (std::numeric_limits<std::size_t>::max)() / 2) {
        return server_status::io_error;
    }

    constexpr std::size_t minimum_capacity = 128;
    const auto minimum = required * 2;
    auto capacity = minimum_capacity;
    while (capacity < minimum) {
        if (capacity > (std::numeric_limits<std::size_t>::max)() / 2) {
            return server_status::io_error;
        }
        capacity *= 2;
    }

    try {
        std::vector<std::uint32_t> candidate(capacity);
        slots.swap(candidate);
        for (std::size_t i = 0; i < existing.size(); ++i) {
            insert_slot(existing[i].name, static_cast<std::uint32_t>(i + 1));
        }
        return server_status::success;
    }
    catch (...) { return server_status::io_error; }
}

std::size_t
header_parser_v2::record_member_name_set::find(
    std::span<const member_record> existing,
    string_id name) const noexcept {

    if (slots.empty()) {
        for (std::size_t i = 0; i < existing.size(); ++i) {
            if (existing[i].name == name) return i;
        }
        return existing.size();
    }

    const auto mask = slots.size() - 1;
    auto position = static_cast<std::size_t>(hash(name)) & mask;
    for (std::size_t probe = 0; probe < slots.size(); ++probe) {
        const auto indexed = slots[position];
        if (indexed == 0) return existing.size();
        const auto index = static_cast<std::size_t>(indexed - 1);
        if (index < existing.size() && existing[index].name == name) {
            return index;
        }
        position = (position + 1) & mask;
    }
    return existing.size();
}

server_status
header_parser_v2::record_member_name_set::insert(
    std::span<const member_record> existing,
    string_id name,
    bool& inserted) noexcept {

    inserted = false;
    if (!name) return server_status::project_configuration_invalid;

    // Common tiny records do not allocate a hash table.
    if (slots.empty() &&
        existing.size() < header_parser_v2::record_member_linear_limit) {
        if (find(existing, name) != existing.size()) return server_status::success;
        inserted = true;
        return server_status::success;
    }

    if (existing.size() >= (std::numeric_limits<std::uint32_t>::max)()) {
        return server_status::io_error;
    }
    const auto required = existing.size() + 1;
    if (slots.empty() || required > slots.size() / 2) {
        const auto rebuilt = rebuild(existing, required);
        if (!succeeded(rebuilt)) return rebuilt;
    }
    if (find(existing, name) != existing.size()) return server_status::success;

    insert_slot(name, static_cast<std::uint32_t>(required));
    inserted = true;
    return server_status::success;
}

server_status header_parser_v2::parse_array_suffix(
    resolved_type& output) noexcept {

    std::vector<std::uint64_t>
        bounds;

    // HEADER-V2-DECLARATOR-09-SPAN-01: preserve the first '[' diagnostic
    // location before consuming array dimensions. OLD reports the array
    // modifier's source location, not the token after the final ']'.
    file_id reference_array_file{};
    source_range reference_array_source{};
    if (output.is_reference && at(token_kind::l_bracket)) {
        const auto* bracket = current();
        reference_array_file = bracket->file;
        reference_array_source = {
            bracket->source_offset,
            bracket->source_length,
        };
    }

    while (at(token_kind::l_bracket)) {
        auto status =
            consume();

        if (!succeeded(status)) {
            return status;
        }

        const auto* bound =
            current();

        if (bound == nullptr ||
            bound->kind !=
                token_kind::pp_number ||
            bound->number.kind !=
                prepared_number_kind_v2::
                    unsigned_integer) {

            return fail(
                parser_v2_failure_kind::unsupported,
                "Header Parser V2 array bound requires a positive decimal integer literal");
        }

        if (bound->number.bits == 0) {
            return fail(
                parser_v2_failure_kind::semantic,
                "Header Parser V2 array bound must be greater than zero");
        }

        try {
            bounds.push_back(
                bound->number.bits);
        }
        catch (...) {
            return server_status::io_error;
        }

        status =
            consume();

        if (!succeeded(status)) {
            return status;
        }

        status =
            expect(
                token_kind::r_bracket,
                "Header Parser V2 expected ']' after array bound");

        if (!succeeded(status)) {
            return status;
        }
    }

    if (bounds.empty()) {
        return server_status::success;
    }

    if (output.is_reference) {
        return fail_at(
            parser_v2_failure_kind::semantic,
            "Header Parser V2 arrays of references are invalid",
            reference_array_file,
            reference_array_source);
    }

    for (auto iterator =
             bounds.rbegin();
         iterator !=
             bounds.rend();
         ++iterator) {

        type_ref wrapped;

        const auto status =
            G.derive_resolved(
                output.type,
                derived_type_kind::
                    bounded_array,
                *iterator,
                wrapped);

        if (!succeeded(status) ||
            !wrapped) {

            return fail(
                parser_v2_failure_kind::semantic,
                "Header Parser V2 array declarator produces an invalid type");
        }

        output.type =
            wrapped;
    }

    output.is_reference =
        false;

    return server_status::success;
}

server_status header_parser_v2::parse_scalar_constant(
    construction_value& output) noexcept {

    output = {};

    bool negative = false;

    if (at(token_kind::minus) || at(token_kind::plus)) {
        negative = at(token_kind::minus);

        const auto status =
            consume();

        if (!succeeded(status)) {
            return status;
        }
    }

    // HEADER-V2-BOOL-ZERO-12: OLD distinguishes the zero-initializer
    // semantics of `false`/`nullptr` from the numeric literal `0`.
    // Keep their canonical construction_kind::zero in persisted G.
    if (at(token_kind::kw_false) || at(token_kind::kw_nullptr)) {
        if (negative) {
            return fail(parser_v2_failure_kind::syntax,
                "Header Parser V2 boolean/nullptr initializer cannot be negated");
        }
        output = {};
        return consume();
    }

    if (at(token_kind::kw_true)) {
        if (negative) {
            return fail(parser_v2_failure_kind::syntax,
                "Header Parser V2 boolean initializer cannot be negated");
        }
        output = construction_value::constant(
            construction_kind::unsigned_integer, 1);
        return consume();
    }

    const auto* token =
        current();

    // HEADER-V2-REAL-LITERAL-11: numbers are already decoded by the
    // physical prepare pass; semantic parsing only consumes their bits.
    if (token == nullptr || token->kind != token_kind::pp_number ||
        (token->number.kind != prepared_number_kind_v2::unsigned_integer &&
         token->number.kind != prepared_number_kind_v2::real)) {
        return fail(parser_v2_failure_kind::unsupported,
            "Header Parser V2 initializer requires a plain decimal numeric or boolean literal");
    }

    if (token->number.kind == prepared_number_kind_v2::real) {
        // IEEE-754 sign flip preserves -0.0 exactly, as OLD does by -value.
        output = construction_value::constant(
            construction_kind::real,
            token->number.bits ^
                (negative ? (std::uint64_t{1} << 63) : std::uint64_t{0}));
        return consume();
    }

    const auto magnitude = token->number.bits;

    output =
        negative
        ? construction_value::constant(
            construction_kind::
                signed_integer,
            std::uint64_t{0} -
                magnitude)
        : construction_value::constant(
            construction_kind::
                unsigned_integer,
            magnitude);

    return consume();
}

// HEADER-V2-REFERENCE-08: Header-local reference operands are resolved as
// soon as their source and destination members exist. No AST, no second pass.
server_status header_parser_v2::parse_reference_name(
    constructor_operation& output) noexcept {

    if (at(token_kind::kw_this)) {
        auto status = consume();
        if (!succeeded(status)) return status;
        status = expect(token_kind::dot,
            "Expected '.' after this in reference binding");
        if (!succeeded(status)) return status;
    }

    const auto* token = current();
    if (token == nullptr || token->kind != token_kind::identifier ||
        !token->identifier) {
        return fail(parser_v2_failure_kind::unsupported,
            "Reference initializer must name another member");
    }
    output.reference_name = token->identifier;
    output.reference_file = token->file;
    output.reference_source = {token->source_offset, token->source_length};
    return consume();
}

server_status header_parser_v2::parse_construction_operand(
    constructor_operation& output, bool allow_reference_name) noexcept {

    if (allow_reference_name &&
        (at(token_kind::identifier) || at(token_kind::kw_this))) {
        return parse_reference_name(output);
    }
    return parse_scalar_constant(output.value);
}

server_status header_parser_v2::parse_member_initializer(
    type_ref target, constructor_operation& output) noexcept {

    output = {};
    if (!at(token_kind::assign) && !at(token_kind::l_brace)) {
        return server_status::success;
    }

    if (const auto* token = current(); token != nullptr) {
        output.file = token->file;
        output.source = {token->source_offset, token->source_length};
    }

    derived_type_record derived;
    const bool reference_target = G.derived(target, derived) &&
        (derived.kind == derived_type_kind::lvalue_reference ||
         derived.kind == derived_type_kind::rvalue_reference);

    if (at(token_kind::assign)) {
        const auto status = consume();
        if (!succeeded(status)) return status;
    }
    if (at(token_kind::l_brace)) {
        auto status = consume();
        if (!succeeded(status)) return status;
        if (!at(token_kind::r_brace)) {
            status = parse_construction_operand(output, reference_target);
            if (!succeeded(status)) return status;
        }
        status = expect(token_kind::r_brace,
            "Header Parser V2 expected '}' after member initializer");
        if (!succeeded(status)) return status;
    }
    else {
        const auto status = parse_construction_operand(output, reference_target);
        if (!succeeded(status)) return status;
    }

    if (!output.reference_name &&
        !construction_compatible(G, target, output.value)) {
        return fail_at(parser_v2_failure_kind::semantic,
            "Header Parser V2 member initializer is incompatible with its type",
            output.file, output.source);
    }
    return server_status::success;
}

server_status header_parser_v2::apply_record_operand(
    std::size_t target,
    const constructor_operation& input_value,
    std::span<const member_record> members,
    std::vector<construction_value>& construction,
    const record_member_name_set& names,
    record_reference_state& references) noexcept {

    if (target >= members.size() || construction.size() != members.size()) {
        return server_status::project_configuration_invalid;
    }

    const auto& target_type = members[target].type;
    auto revision = references.generations.find(target);

    if (!input_value.reference_name) {
        if (!construction_compatible(G, target_type, input_value.value)) {
            return fail_at(parser_v2_failure_kind::semantic,
                "Header Parser V2 construction value is incompatible with its field",
                input_value.file, input_value.source);
        }
        if (revision != references.generations.end()) ++revision->second;
        construction[target] = input_value.value;
        return server_status::success;
    }

    derived_type_record target_ref;
    if (!G.derived(target_type, target_ref) ||
        (target_ref.kind != derived_type_kind::lvalue_reference &&
         target_ref.kind != derived_type_kind::rvalue_reference)) {
        return fail_at(parser_v2_failure_kind::unsupported,
            "Value construction requires a scalar constant, not a member name",
            input_value.reference_file, input_value.reference_source);
    }

    const auto source = names.find(members, input_value.reference_name);
    if (source == members.size()) {
        try {
            auto [it, inserted] = references.generations.try_emplace(target, 0);
            (void)inserted;
            ++it->second;
            references.waiting.emplace(input_value.reference_name.value(),
                waiting_reference{target, it->second,
                    input_value.reference_file, input_value.reference_source});
            construction[target] = {};
            return server_status::success;
        }
        catch (...) { return server_status::io_error; }
    }

    if (source >= static_cast<std::size_t>(
        (std::numeric_limits<std::uint32_t>::max)())) {
        return server_status::io_error;
    }

    auto source_type = members[source].type;
    derived_type_record source_ref;
    if (G.derived(source_type, source_ref) &&
        (source_ref.kind == derived_type_kind::lvalue_reference ||
         source_ref.kind == derived_type_kind::rvalue_reference)) {
        source_type = source_ref.child;
    }
    if (target_ref.child != source_type) {
        return fail_at(parser_v2_failure_kind::semantic,
            "Reference binding type does not match bound member type",
            input_value.reference_file, input_value.reference_source);
    }

    if (revision != references.generations.end()) ++revision->second;
    construction[target] = construction_value::member_binding(
        static_cast<std::uint32_t>(source + 1));
    return server_status::success;
}

server_status header_parser_v2::resolve_waiting_references(
    string_id source_name,
    std::span<const member_record> members,
    std::vector<construction_value>& construction,
    record_reference_state& references) noexcept {

    if (references.waiting.empty()) return server_status::success;
    if (!source_name || members.empty() || construction.size() != members.size() ||
        members.back().name != source_name) {
        return server_status::project_configuration_invalid;
    }
    const auto source_index = members.size() - 1;
    if (source_index >= static_cast<std::size_t>(
        (std::numeric_limits<std::uint32_t>::max)())) {
        return server_status::io_error;
    }
    auto source_type = members.back().type;
    derived_type_record source_ref;
    if (G.derived(source_type, source_ref) &&
        (source_ref.kind == derived_type_kind::lvalue_reference ||
         source_ref.kind == derived_type_kind::rvalue_reference)) {
        source_type = source_ref.child;
    }

    const auto range = references.waiting.equal_range(source_name.value());
    for (auto it = range.first; it != range.second;) {
        const auto waiting = it->second;
        if (waiting.target >= members.size()) {
            return server_status::project_configuration_invalid;
        }
        derived_type_record target_ref;
        if (!G.derived(members[waiting.target].type, target_ref) ||
            (target_ref.kind != derived_type_kind::lvalue_reference &&
             target_ref.kind != derived_type_kind::rvalue_reference) ||
            target_ref.child != source_type) {
            return fail_at(parser_v2_failure_kind::semantic,
                "Reference binding type does not match bound member type",
                waiting.file, waiting.source);
        }
        const auto current = references.generations.find(waiting.target);
        if (current == references.generations.end()) {
            return server_status::project_configuration_invalid;
        }
        if (current->second == waiting.generation) {
            construction[waiting.target] = construction_value::member_binding(
                static_cast<std::uint32_t>(source_index + 1));
        }
        it = references.waiting.erase(it);
    }
    return server_status::success;
}

server_status header_parser_v2::parse_member_declarator(
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
    bool& declares_virtual) noexcept {
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
    if (profile != nullptr) ++profile->member_calls;
    const bool sampled = profile != nullptr &&
        (profile->member_calls % 128u) == 0;
    header_v2_timed_probe probe(sampled ? &profile->member_sample_ms : nullptr);
    if (sampled) ++profile->member_samples;
#endif

    // HEADER-V2-DECLARATOR-09: a parenthesized prefix binds *outside* the
    // following array suffix: short (&A)[6] => reference(array(short, 6)).
    // A plain short &A[6] remains invalid (array of references).
    bool grouped = false;
    derived_type_kind grouped_kind = derived_type_kind::pointer;
    auto status = server_status::success;

    if (at(token_kind::l_paren)) {
        grouped = true;
        status = consume();
        if (!succeeded(status)) return status;

        if (at(token_kind::star)) {
            grouped_kind = derived_type_kind::pointer;
        }
        else if (at(token_kind::ampersand)) {
            grouped_kind = derived_type_kind::lvalue_reference;
        }
        else if (at(token_kind::logical_and)) {
            grouped_kind = derived_type_kind::rvalue_reference;
        }
        else {
            return fail(parser_v2_failure_kind::unsupported,
                "Header Parser V2 grouped data declarator requires *, & or &&");
        }
        status = consume();
        if (!succeeded(status)) return status;
    }

    const auto* name =
        current();

    if (name == nullptr ||
        name->kind !=
            token_kind::identifier ||
        !name->identifier) {

        return fail(
            parser_v2_failure_kind::syntax,
            "Header Parser V2 expected a data-member name");
    }

    const auto member_name =
        name->identifier;

    const auto member_file =
        name->file;

    const source_range member_source{
        name->source_offset,
        name->source_length,
    };

    status = consume();
    if (!succeeded(status)) return status;

    if (grouped) {
        status = expect(token_kind::r_paren,
            "Header Parser V2 expected ')' after grouped declarator name");
        if (!succeeded(status)) return status;
    }

    // HEADER-V2-VIRTUAL-01: methods have no stored Graph member.
    // Consume their declaration before the record-local field name set.
    if (!grouped && at(token_kind::l_paren)) {
        return parse_method_tail(
            virtual_prefix,
            base_polymorphic,
            declares_virtual);
    }

    if (virtual_prefix) {
        return fail(
            parser_v2_failure_kind::syntax,
            "virtual must declare a member function");
    }

    if (type.type == G.intrinsic(intrinsic_type::void_type)) {
        return fail(
            parser_v2_failure_kind::semantic,
            "A data member cannot have void type");
    }

    status =
        parse_array_suffix(
            type);

    if (!succeeded(status)) {
        return status;
    }

    // Apply the grouped prefix AFTER the array suffix. No token replay, AST,
    // second Graph pass or additional persisted representation is needed.
    if (grouped) {
        if (type.is_reference) {
            return fail(parser_v2_failure_kind::semantic,
                "Pointer/reference to reference is not valid C++");
        }
        type_ref wrapped;
        status = G.derive_resolved(type.type, grouped_kind, 0, wrapped);
        if (!succeeded(status) || !wrapped) {
            return fail(parser_v2_failure_kind::semantic,
                "Header Parser V2 grouped declarator produces an invalid type");
        }
        type.type = wrapped;
        type.indirect = true;
        type.is_reference = grouped_kind != derived_type_kind::pointer;
    }

    bool inserted = false;

    {
    #ifdef CW_HEADER_V2_SEMANTIC_PROFILE
        header_v2_detail_probe perf06_probe(
            profile,
            &header_parser_v2_profile::member_name_calls,
            &header_parser_v2_profile::member_name_samples,
            &header_parser_v2_profile::member_name_sample_ms);
    #endif
        status = names.insert(members, member_name, inserted);
    }

    if (!succeeded(status)) {
        return status;
    }

    if (!inserted) {
        return fail_at(
            parser_v2_failure_kind::semantic,
            "Header Parser V2 data-member name is duplicated",
            member_file,
            member_source);
    }

    // Most fields have no initializer: avoid type lookups and hash probes.
    const bool has_initializer =
        at(token_kind::assign) || at(token_kind::l_brace);
    constructor_operation initial;

    {
    #ifdef CW_HEADER_V2_SEMANTIC_PROFILE
        header_v2_detail_probe perf06_probe(
            profile,
            &header_parser_v2_profile::member_initializer_calls,
            &header_parser_v2_profile::member_initializer_samples,
            &header_parser_v2_profile::member_initializer_sample_ms);
    #endif
        status = parse_member_initializer(type.type, initial);
    }

    if (!succeeded(status)) {
        return status;
    }

    if (at(token_kind::comma)) {
        return fail(
            parser_v2_failure_kind::unsupported,
            "Header Parser V2 multiple data declarators are not part of the current Header subset");
    }

    status =
        expect(
            token_kind::semicolon,
            "Header Parser V2 expected ';' after data member");

    if (!succeeded(status)) {
        return status;
    }

    // HEADER-V2-REFERENCE-08: resolve declaration default immediately when
    // possible; defer only a forward referent, keyed by its compact name ID.
    {
    #ifdef CW_HEADER_V2_SEMANTIC_PROFILE
        header_v2_detail_probe perf06_probe(
            profile,
            &header_parser_v2_profile::member_append_calls,
            &header_parser_v2_profile::member_append_samples,
            &header_parser_v2_profile::member_append_sample_ms);
    #endif
        try {
            members.push_back({member_name, type.type, access, {}});
            construction.push_back({});
        }
        catch (...) { return server_status::io_error; }
    }

    const auto index = members.size() - 1;
    if (has_initializer) {
        status = apply_record_operand(index, initial, members, construction,
            names, references);
        if (!succeeded(status)) return status;
    }

    const auto pending_it = pending.find(member_name.value());
    if (pending_it != pending.end()) {
        status = apply_record_operand(index, pending_it->second,
            members, construction, names, references);
        if (!succeeded(status)) return status;
        pending.erase(pending_it);
    }

    // One hashed range is visited only when its source member becomes known.
    status = resolve_waiting_references(member_name, members, construction,
        references);
    if (!succeeded(status)) return status;
    return resolve_waiting_nested(member_name, members, names, nested);
}

server_status header_parser_v2::parse_member(
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
    bool& declares_virtual) noexcept {

    resolved_type type;

    auto status =
        parse_type(
            scope,
            type);

    if (!succeeded(status)) {
        return status;
    }

    if (at(token_kind::kw_operator) ||
        (at(token_kind::identifier) &&
         current()->identifier == enclosing_record)) {
        return parse_named_operator(
            enclosing_record, virtual_prefix,
            base_polymorphic, declares_virtual);
    }

    return parse_member_declarator(
        type,
        access,
        members,
        construction,
        pending,
        references,
        nested,
        names,
        virtual_prefix,
        base_polymorphic,
        declares_virtual);
}

// HEADER-V2-METHOD-02: preserve OLD declaration-only method semantics.
// Ordinary method parameters are opaque balanced tokens (as in OLD); no
// method-body execution, Graph data members or Runtime method ABI is added.
server_status header_parser_v2::parse_method_tail(
    bool virtual_prefix,
    bool base_polymorphic,
    bool& declares_virtual,
    bool conversion,
    bool assignment_operator,
    bool subscript_operator,
    bool binary_operator) noexcept {

    if (!at(token_kind::l_paren)) {
        return fail(parser_v2_failure_kind::syntax,
            "Expected '(' after method declarator");
    }

    // OLD's method parameter contract counts balanced parentheses. Parameter
    // types are deliberately not persisted or resolved by this grammar slice.
    std::size_t depth = 0;
    bool conversion_void = false;
    std::size_t parameter_tokens = 0;
    bool sole_void = false;
    for (;;) {
        if (input.finished() || current() == nullptr) {
            return fail(parser_v2_failure_kind::syntax,
                "Method parameter list is not closed");
        }
        if ((assignment_operator || subscript_operator || binary_operator) &&
            depth == 1 && !at(token_kind::r_paren)) {
            if (at(token_kind::comma) || at(token_kind::ellipsis) ||
                at(token_kind::assign)) {
                return fail(parser_v2_failure_kind::syntax,
                    "Named operator requires one parameter without a default argument");
            }
            sole_void = parameter_tokens == 0 && at(token_kind::kw_void);
            ++parameter_tokens;
        }
        if (conversion && depth != 0 && !at(token_kind::r_paren)) {
            if (!conversion_void && at(token_kind::kw_void)) {
                conversion_void = true;
            }
            else {
                return fail(parser_v2_failure_kind::syntax,
                    "This operator must have an empty parameter list");
            }
        }
        if (at(token_kind::l_paren)) {
            ++depth;
        }
        else if (at(token_kind::r_paren)) {
            if (depth == 0) {
                return fail(parser_v2_failure_kind::syntax,
                    "Unexpected ')' in method declaration");
            }
            --depth;
        }
        const auto consumed = consume();
        if (!succeeded(consumed)) return consumed;
        if (depth == 0) break;
    }

    if ((assignment_operator || subscript_operator || binary_operator) &&
        (parameter_tokens == 0 || sole_void)) {
        return fail(parser_v2_failure_kind::syntax,
            "Named operator requires one parameter");
    }

    bool override_seen = false;
    bool final_seen = false;
    bool ref_seen = false;
    for (;;) {
        // OLD permits ref-qualifiers on conversion and named operators,
        // not on ordinary functions/destructors in its current subset.
        if ((conversion || assignment_operator || subscript_operator ||
             binary_operator) && !ref_seen &&
            (at(token_kind::ampersand) || at(token_kind::logical_and))) {
            ref_seen = true;
            const auto consumed = consume();
            if (!succeeded(consumed)) return consumed;
            continue;
        }
        if (at(token_kind::kw_const) || at(token_kind::kw_volatile)) {
            const auto consumed = consume();
            if (!succeeded(consumed)) return consumed;
            continue;
        }
        if (at(token_kind::kw_noexcept)) {
            auto consumed = consume();
            if (!succeeded(consumed)) return consumed;
            if (at(token_kind::l_paren)) {
                std::size_t noexcept_depth = 0;
                for (;;) {
                    if (input.finished() || current() == nullptr) {
                        return fail(parser_v2_failure_kind::syntax,
                            "noexcept expression is not closed");
                    }
                    if (at(token_kind::l_paren)) {
                        ++noexcept_depth;
                    }
                    else if (at(token_kind::r_paren)) {
                        if (noexcept_depth == 0) {
                            return fail(parser_v2_failure_kind::syntax,
                                "Unexpected ')' in noexcept expression");
                        }
                        --noexcept_depth;
                    }
                    consumed = consume();
                    if (!succeeded(consumed)) return consumed;
                    if (noexcept_depth == 0) break;
                }
            }
            continue;
        }
        if (!override_seen && input.contextual_identifier("override")) {
            override_seen = true;
            const auto consumed = consume();
            if (!succeeded(consumed)) return consumed;
            continue;
        }
        if (!final_seen && input.contextual_identifier("final")) {
            final_seen = true;
            const auto consumed = consume();
            if (!succeeded(consumed)) return consumed;
            continue;
        }
        break;
    }

    bool pure = false;
    if (at(token_kind::assign)) {
        auto consumed = consume();
        if (!succeeded(consumed)) return consumed;
        const auto* token = current();
        if (token != nullptr &&
            token->kind == token_kind::pp_number &&
            token->number.kind == prepared_number_kind_v2::unsigned_integer &&
            token->number.bits == 0) {
            pure = true;
        }
        else if ((conversion || subscript_operator || binary_operator ||
                  !at(token_kind::kw_default)) &&
                 !at(token_kind::kw_delete)) {
            return fail(parser_v2_failure_kind::unsupported,
                "Method declaration supports only '= 0', '= default', or '= delete'");
        }
        consumed = consume();
        if (!succeeded(consumed)) return consumed;
    }

    if ((override_seen || final_seen || pure) &&
        !virtual_prefix && !base_polymorphic) {
        return fail(parser_v2_failure_kind::semantic,
            "override/final/pure method requires a polymorphic base or explicit virtual");
    }
    if (virtual_prefix || override_seen || final_seen || pure) {
        declares_virtual = true;
    }
    if (!at(token_kind::semicolon)) {
        return fail(parser_v2_failure_kind::unsupported,
            "CXX-CLASS-ABI-V1 stores method declarations only; method bodies are not implemented");
    }
    return consume();
}

// HEADER-V2-PARITY-04: declaration-only named operators share the OLD
// method tail and produce no method records in Graph.
server_status header_parser_v2::parse_named_operator(
    string_id enclosing_record,
    bool virtual_prefix,
    bool base_polymorphic,
    bool& declares_virtual) noexcept {

    server_status status = server_status::success;
    if (at(token_kind::identifier)) {
        if (!enclosing_record ||
            current()->identifier != enclosing_record) {
            return fail(parser_v2_failure_kind::semantic,
                "In-class operator qualifier must name the enclosing record");
        }
        status = consume();
        if (!succeeded(status)) return status;
        status = expect(token_kind::scope,
            "In-class operator qualification is supported only for the enclosing record");
        if (!succeeded(status)) return status;
    }

    status = expect(token_kind::kw_operator,
        "In-class qualification is supported only for named operators");
    if (!succeeded(status)) return status;

    bool conversion = false;
    bool assignment_operator = false;
    bool subscript_operator = false;
    bool binary_operator = false;
    if (at(token_kind::assign)) {
        assignment_operator = true;
    }
    else if (at(token_kind::l_bracket)) {
        subscript_operator = true;
        status = consume();
        if (!succeeded(status)) return status;
        status = expect(token_kind::r_bracket,
            "Expected ']' in operator[] declarator");
        if (!succeeded(status)) return status;
        // The operator token has been consumed by expect().
        return parse_method_tail(virtual_prefix, base_polymorphic,
            declares_virtual, false, false, true, false);
    }
    else if (at(token_kind::exclamation)) {
        conversion = true; // OLD treats operator! like an empty conversion tail.
    }
    else {
        switch (current() ? current()->kind : token_kind::invalid) {
        case token_kind::plus_assign:
        case token_kind::minus_assign:
        case token_kind::star_assign:
        case token_kind::slash_assign:
        case token_kind::percent_assign:
        case token_kind::caret_assign:
        case token_kind::ampersand_assign:
        case token_kind::pipe_assign:
        case token_kind::shift_left_assign:
        case token_kind::shift_right_assign:
        case token_kind::equal:
        case token_kind::not_equal:
        case token_kind::less:
        case token_kind::greater:
        case token_kind::less_equal:
        case token_kind::greater_equal:
            binary_operator = true;
            break;
        default:
            return fail(parser_v2_failure_kind::unsupported,
                "Named operator declarator is not supported");
        }
    }

    status = consume();
    if (!succeeded(status)) return status;
    return parse_method_tail(virtual_prefix, base_polymorphic,
        declares_virtual, conversion, assignment_operator,
        subscript_operator, binary_operator);
}

// HEADER-V2-NESTED-CONSTRUCTOR-10: resolve a nested member/array path
// against the already-created Type domain and intern its canonical spelling.
// No AST, Graph projection or source-token replay. OLD's per-record last-wins
// semantics are retained until the one-time constructor-default commit.
server_status header_parser_v2::apply_nested_operation(
    const nested_operation& operation,
    std::span<const member_record> members,
    const record_member_name_set& names,
    record_nested_state& nested) noexcept {

    const auto index = names.find(members, operation.root);
    if (index >= members.size() || operation.path.empty()) {
        return server_status::project_configuration_invalid;
    }

    auto type = members[index].type;
    auto& strings = input.semantic_strings();
    std::string spelling;
    try { spelling.assign(strings.get(operation.root)); }
    catch (...) { return server_status::io_error; }

    for (const auto& step : operation.path) {
        if (step.name) {
            if (type.kind() != type_ref_kind::named) {
                return fail_at(parser_v2_failure_kind::unsupported,
                    "Nested constructor path requires direct record fields",
                    operation.file, operation.source);
            }
            type_handle record;
            if (!G.named(type, record)) {
                return fail_at(parser_v2_failure_kind::semantic,
                    "Nested constructor target record is invalid",
                    operation.file, operation.source);
            }
            const auto member_index = G.find_member(record, step.name);
            const auto* member = G.member(record, member_index);
            if (member == nullptr) {
                return fail_at(parser_v2_failure_kind::semantic,
                    "Nested constructor field does not exist",
                    operation.file, operation.source);
            }
            type = member->type;
            try {
                spelling.push_back('.');
                spelling.append(strings.get(step.name));
            }
            catch (...) { return server_status::io_error; }
        }
        else {
            derived_type_record array;
            if (!G.derived(type, array) ||
                array.kind != derived_type_kind::bounded_array) {
                return fail_at(parser_v2_failure_kind::semantic,
                    "Constructor array index requires a bounded array",
                    operation.file, operation.source);
            }
            if (step.index >= array.payload) {
                return fail_at(parser_v2_failure_kind::semantic,
                    "Constructor array index is outside the declared bound",
                    operation.file, operation.source);
            }
            type = array.child;
            try {
                spelling.push_back('[');
                spelling.append(std::to_string(step.index));
                spelling.push_back(']');
            }
            catch (...) { return server_status::io_error; }
        }
    }

    if (type.kind() != type_ref_kind::intrinsic ||
        !construction_compatible(G, type, operation.value)) {
        return fail_at(parser_v2_failure_kind::unsupported,
            "Nested constructor assignment requires a writable scalar field and scalar constant",
            operation.file, operation.source);
    }

    string_id path;
    const auto interned = strings.intern(spelling, path);
    if (!succeeded(interned)) return interned;
    if (!path) return server_status::project_configuration_invalid;

    // Small nested constructors scan a contiguous vector; large ones reuse
    // one path-ID index, avoiding quadratic override lookup on PLC records.
    if (nested.completed.size() < 32) {
        for (auto& previous : nested.completed) {
            if (previous.path == path) {
                previous.value = operation.value;
                return server_status::success;
            }
        }
    }
    else {
        try {
            if (nested.completed_lookup.empty()) {
                nested.completed_lookup.reserve(nested.completed.size() * 2);
                for (std::size_t i = 0; i < nested.completed.size(); ++i) {
                    nested.completed_lookup.emplace(
                        nested.completed[i].path.value(), i);
                }
            }
            const auto found = nested.completed_lookup.find(path.value());
            if (found != nested.completed_lookup.end()) {
                nested.completed[found->second].value = operation.value;
                return server_status::success;
            }
        }
        catch (...) { return server_status::io_error; }
    }
    try {
        if (!nested.completed_lookup.empty()) {
            // Vector/index insertion either both succeeds, or the operation
            // fails: the construction run will be discarded on error.
            nested.completed_lookup.emplace(path.value(), nested.completed.size());
        }
        nested.completed.push_back({nested.owner, path, operation.value});
    }
    catch (...) { return server_status::io_error; }
    return server_status::success;
}

server_status header_parser_v2::append_nested_operation(
    nested_operation&& operation,
    std::span<const member_record> members,
    const record_member_name_set& names,
    record_nested_state& nested) noexcept {

    if (names.find(members, operation.root) < members.size()) {
        return apply_nested_operation(operation, members, names, nested);
    }
    try {
        nested.waiting[operation.root.value()].push_back(std::move(operation));
    }
    catch (...) { return server_status::io_error; }
    return server_status::success;
}

server_status header_parser_v2::resolve_waiting_nested(
    string_id root,
    std::span<const member_record> members,
    const record_member_name_set& names,
    record_nested_state& nested) noexcept {

    if (nested.waiting.empty()) return server_status::success;
    const auto it = nested.waiting.find(root.value());
    if (it == nested.waiting.end()) return server_status::success;
    // These operations were written in source order, including last-wins.
    for (const auto& operation : it->second) {
        const auto status = apply_nested_operation(operation, members, names, nested);
        if (!succeeded(status)) return status;
    }
    nested.waiting.erase(it);
    return server_status::success;
}

server_status header_parser_v2::parse_constructor(
    string_id record_name,
    std::span<const member_record> members,
    const record_member_name_set& names,
    std::vector<construction_value>& construction,
    pending_constructor_operations& pending,
    record_reference_state& references,
    record_nested_state& nested) noexcept {
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
    if (profile != nullptr) ++profile->constructor_calls;
    header_v2_timed_probe probe(profile != nullptr ? &profile->constructor_ms : nullptr);
#endif

    if (!record_name ||
        !at(token_kind::l_paren)) {

        return fail(
            parser_v2_failure_kind::syntax,
            "Header Parser V2 expected '(' after constructor name");
    }

    auto status =
        consume();

    if (!succeeded(status)) {
        return status;
    }

    if (!at(token_kind::r_paren)) {
        return fail(
            parser_v2_failure_kind::unsupported,
            "Header Parser V2 managed constructors must have no parameters");
    }

    status =
        consume();

    if (!succeeded(status)) {
        return status;
    }

    if (at(token_kind::kw_noexcept)) {
        status =
            consume();

        if (!succeeded(status)) {
            return status;
        }
    }

    if (at(token_kind::semicolon)) {
        return consume();
    }

    if (at(token_kind::assign)) {
        status =
            consume();

        if (!succeeded(status)) {
            return status;
        }

        status =
            expect(
                token_kind::kw_default,
                "Header Parser V2 managed constructor supports '= default' only");

        if (!succeeded(status)) {
            return status;
        }

        return expect(
            token_kind::semicolon,
            "Header Parser V2 expected ';' after '= default'");
    }

    // HEADER-V2-REFERENCE-08: both scalar and local-reference constructor
    // operands normalize when their destination is available. Forward
    // destinations retain only their latest unresolved source operand.
    const auto append = [&](string_id target,
        const constructor_operation& value) -> server_status {
        const auto i = names.find(members, target);
        if (i < members.size()) {
            return apply_record_operand(i, value, members, construction,
                names, references);
        }
        try {
            pending.insert_or_assign(target.value(), value);
            return server_status::success;
        }
        catch (...) { return server_status::io_error; }
    };

    if (at(token_kind::colon)) {
        status =
            consume();

        if (!succeeded(status)) {
            return status;
        }

        for (;;) {
            const auto* target =
                current();

            if (target == nullptr ||
                target->kind !=
                    token_kind::identifier ||
                !target->identifier) {

                return fail(
                    parser_v2_failure_kind::syntax,
                    "Header Parser V2 expected constructor member name");
            }

            const auto target_name =
                target->identifier;
            const auto target_file = target->file;
            const source_range target_source{
                target->source_offset, target->source_length};

            status =
                consume();

            if (!succeeded(status)) {
                return status;
            }

            token_kind close =
                token_kind::invalid;

            if (at(token_kind::l_paren)) {
                close =
                    token_kind::r_paren;
            }
            else if (at(token_kind::l_brace)) {
                close =
                    token_kind::r_brace;
            }
            else {
                return fail(
                    parser_v2_failure_kind::syntax,
                    "Header Parser V2 expected constructor member initializer");
            }

            status =
                consume();

            if (!succeeded(status)) {
                return status;
            }

            constructor_operation value;
            value.file = target_file;
            value.source = target_source;

            if (!at(close)) {
                status = parse_construction_operand(value, true);
                if (!succeeded(status)) return status;
            }

            status =
                expect(
                    close,
                    "Header Parser V2 expected end of constructor member initializer");

            if (!succeeded(status)) {
                return status;
            }

            status = append(target_name, value);

            if (!succeeded(status)) {
                return status;
            }

            if (!at(token_kind::comma)) {
                break;
            }

            status =
                consume();

            if (!succeeded(status)) {
                return status;
            }
        }
    }

    status =
        expect(
            token_kind::l_brace,
            "Header Parser V2 expected managed constructor body");

    if (!succeeded(status)) {
        return status;
    }

    while (!at(token_kind::r_brace)) {
        if (input.finished()) {
            return fail(
                parser_v2_failure_kind::syntax,
                "Header Parser V2 managed constructor body is not closed");
        }

        const auto* target =
            current();

        if (target == nullptr ||
            target->kind !=
                token_kind::identifier ||
            !target->identifier) {

            return fail(
                parser_v2_failure_kind::unsupported,
                "Header Parser V2 constructor body supports direct field assignments only");
        }

        const auto target_name =
            target->identifier;
        const auto target_file = target->file;
        const source_range target_source{
            target->source_offset, target->source_length};

        status =
            consume();

        if (!succeeded(status)) {
            return status;
        }

        // HEADER-V2-NESTED-CONSTRUCTOR-10: OLD's constructor target path
        // grammar is identifier (.identifier | [decimal-index])*, depth <= 256.
        std::vector<nested_path_step> path;
        while (at(token_kind::dot) || at(token_kind::l_bracket)) {
            if (path.size() >= parser_v2_scope_depth_limit) {
                return fail(parser_v2_failure_kind::unsupported,
                    "Constructor path exceeds supported depth");
            }
            if (at(token_kind::dot)) {
                status = consume();
                if (!succeeded(status)) return status;
                const auto* member = current();
                if (member == nullptr || member->kind != token_kind::identifier ||
                    !member->identifier) {
                    return fail(parser_v2_failure_kind::syntax,
                        "Invalid nested constructor field path");
                }
                try { path.push_back({member->identifier, 0}); }
                catch (...) { return server_status::io_error; }
                status = consume();
                if (!succeeded(status)) return status;
            }
            else {
                status = consume();
                if (!succeeded(status)) return status;
                const auto* index = current();
                if (index == nullptr || index->kind != token_kind::pp_number ||
                    index->number.kind != prepared_number_kind_v2::unsigned_integer) {
                    return fail(parser_v2_failure_kind::unsupported,
                        "Constructor array index requires a decimal integer literal");
                }
                try { path.push_back({{}, index->number.bits}); }
                catch (...) { return server_status::io_error; }
                status = consume();
                if (!succeeded(status)) return status;
                status = expect(token_kind::r_bracket,
                    "Expected ']' after constructor array index");
                if (!succeeded(status)) return status;
            }
        }

        status =
            expect(
                token_kind::assign,
                "Header Parser V2 expected '=' in constructor field assignment");

        if (!succeeded(status)) {
            return status;
        }

        constructor_operation value;
        value.file = target_file;
        value.source = target_source;

        status = path.empty()
            ? parse_construction_operand(value, true)
            : parse_scalar_constant(value.value);

        if (!succeeded(status)) {
            return status;
        }

        status =
            expect(
                token_kind::semicolon,
                "Header Parser V2 expected ';' after constructor field assignment");

        if (!succeeded(status)) {
            return status;
        }

        if (path.empty()) {
            status = append(target_name, value);
        }
        else {
            nested_operation operation;
            operation.root = target_name;
            operation.path = std::move(path);
            operation.value = value.value;
            operation.file = target_file;
            operation.source = target_source;
            status = append_nested_operation(std::move(operation), members,
                names, nested);
        }

        if (!succeeded(status)) {
            return status;
        }
    }

    return consume();
}

server_status header_parser_v2::parse_record(
    identity_ref scope) noexcept {

    const auto* key =
        current();

    if (key == nullptr ||
        (key->kind !=
             token_kind::kw_struct &&
         key->kind !=
             token_kind::kw_class &&
         key->kind !=
             token_kind::kw_union)) {

        return fail(
            parser_v2_failure_kind::syntax,
            "Header Parser V2 expected a record declaration");
    }

    const auto kind =
        record_kind(
            key->kind);

    auto status =
        consume();

    if (!succeeded(status)) {
        return status;
    }

    const auto* name =
        current();

    if (name == nullptr ||
        name->kind !=
            token_kind::identifier ||
        !name->identifier) {

        return fail(
            parser_v2_failure_kind::syntax,
            "Header Parser V2 expected a record name");
    }

    const auto record_name =
        name->identifier;

    identity_ref identity;

    status =
        identities.resolve(
            scope,
            record_name,
            identity_kind::type,
            identity);

    if (!succeeded(status) ||
        !identity) {

        return fail(
            parser_v2_failure_kind::semantic,
            "Header Parser V2 record identity could not be resolved");
    }

    status =
        remember_record_kind(
            identity,
            kind);

    if (!succeeded(status)) {
        return status;
    }

    status =
        consume();

    if (!succeeded(status)) {
        return status;
    }

    std::vector<base_record> bases;

    if (at(token_kind::colon)) {
        if (kind == graph_record_kind::union_type) {
            return fail(
                parser_v2_failure_kind::semantic,
                "Union types cannot have base classes");
        }

        status = consume();
        if (!succeeded(status)) {
            return status;
        }

        for (;;) {
            auto access = default_access(kind);
            bool access_seen = false;
            bool virtual_seen = false;

            // Match OLD: access and virtual may appear in either order,
            // but neither specifier may be repeated.
            for (;;) {
                if (!access_seen &&
                    (at(token_kind::kw_public) ||
                     at(token_kind::kw_protected) ||
                     at(token_kind::kw_private))) {

                    access = at(token_kind::kw_public)
                        ? graph_member_access::public_access
                        : at(token_kind::kw_protected)
                            ? graph_member_access::protected_access
                            : graph_member_access::private_access;
                    access_seen = true;
                }
                else if (!virtual_seen && at(token_kind::kw_virtual)) {
                    virtual_seen = true;
                }
                else {
                    break;
                }

                status = consume();
                if (!succeeded(status)) {
                    return status;
                }
            }

            if (virtual_seen) {
                return fail(
                    parser_v2_failure_kind::unsupported,
                    "Virtual base classes are not implemented in CXX-CLASS-ABI-V1");
            }

            const auto* base_name = current();
            if (base_name == nullptr ||
                base_name->kind != token_kind::identifier ||
                !base_name->identifier) {
                return fail(
                    parser_v2_failure_kind::syntax,
                    "Expected base class type");
            }

            const auto base_identity = find_type_identity(
                scope, base_name->identifier);
            const auto base_type = G.find_type(base_identity);
            const auto* base_entry = G.find(base_type);
            if (!base_identity || !base_type ||
                base_entry == nullptr || !base_entry->defined() ||
                base_entry->kind != graph_type_kind::record ||
                base_entry->record_kind == graph_record_kind::union_type) {
                return fail(
                    parser_v2_failure_kind::semantic,
                    "Base class must name a complete non-union record type");
            }

            for (const auto& previous : bases) {
                if (previous.type == base_identity) {
                    return fail(
                        parser_v2_failure_kind::semantic,
                        "Direct base class is duplicated");
                }
            }

            try {
                bases.push_back({base_identity, access, 0, 0});
            }
            catch (...) {
                return server_status::io_error;
            }

            status = consume();
            if (!succeeded(status)) {
                return status;
            }
            if (!at(token_kind::comma)) {
                break;
            }
            status = consume();
            if (!succeeded(status)) {
                return status;
            }
        }
    }

    if (at(token_kind::semicolon) && !bases.empty()) {
        return fail(
            parser_v2_failure_kind::syntax,
            "A base-specifier list requires a record definition");
    }

    if (at(token_kind::semicolon)) {
        // WHO-only declaration. Do not allocate Graph WHERE.
        return consume();
    }

    status =
        expect(
            token_kind::l_brace,
            "Header Parser V2 expected '{' after record name");

    if (!succeeded(status)) {
        return status;
    }

    type_handle handle;

    status =
        G.declare_record(
            identity,
            kind,
            handle);

    if (!succeeded(status) ||
        !handle) {

        return fail(
            parser_v2_failure_kind::semantic,
            "Header Parser V2 record definition conflicts with G");
    }

    std::vector<member_record>
        members;

    std::vector<construction_value>
        construction;

    // Only forward constructor targets live beyond the point of parsing.
    pending_constructor_operations pending;
    record_reference_state references;
    record_nested_state nested;
    nested.owner = identity;

    record_member_name_set
        names;

    auto access =
        default_access(
            kind);

    bool constructor_seen = false;
    bool declares_virtual = false;
    bool base_polymorphic = false;
    for (const auto& base : bases) {
        const auto base_handle = G.find_type(base.type);
        if (base_handle && G.polymorphic(base_handle)) {
            base_polymorphic = true;
            break;
        }
    }

    while (!input.finished() &&
           !at(token_kind::r_brace)) {

        if (at(token_kind::semicolon)) {
            status =
                consume();

            if (!succeeded(status)) {
                return status;
            }

            continue;
        }

        if (at(token_kind::kw_public) ||
            at(token_kind::kw_protected) ||
            at(token_kind::kw_private)) {

            access =
                at(token_kind::kw_public)
                ? graph_member_access::
                    public_access
                : at(token_kind::kw_protected)
                    ? graph_member_access::
                        protected_access
                    : graph_member_access::
                        private_access;

            status =
                consume();

            if (!succeeded(status)) {
                return status;
            }

            status =
                expect(
                    token_kind::colon,
                    "Header Parser V2 expected ':' after access specifier");

            if (!succeeded(status)) {
                return status;
            }

            continue;
        }

        bool virtual_prefix = false;
        if (at(token_kind::kw_virtual)) {
            virtual_prefix = true;
            status = consume();
            if (!succeeded(status)) return status;
        }

        if (at(token_kind::tilde)) {
            // A destructor has no data-member identity; OLD accepts only a
            // declaration with the containing record's exact name.
            status = consume();
            if (!succeeded(status)) return status;
            if (!at(token_kind::identifier) ||
                current()->identifier != record_name) {
                return fail(parser_v2_failure_kind::syntax,
                    "Destructor name must match its record");
            }
            status = consume();
            if (!succeeded(status)) return status;
            status = parse_method_tail(
                virtual_prefix, base_polymorphic, declares_virtual);
            if (!succeeded(status)) return status;
            continue;
        }

        if (at(token_kind::kw_static)) {
            return fail(parser_v2_failure_kind::unsupported,
                "Static data members and static methods are not part of the current instance ABI slice");
        }

        if (at(token_kind::kw_explicit)) {
            status = consume();
            if (!succeeded(status)) return status;
            if (!at(token_kind::kw_operator)) {
                return fail(parser_v2_failure_kind::unsupported,
                    "explicit is supported here only on conversion operator declarations");
            }
        }
        if (at(token_kind::kw_operator)) {
            status = consume();
            if (!succeeded(status)) return status;
            resolved_type conversion_type;
            status = parse_type(scope, conversion_type);
            if (!succeeded(status)) return status;
            status = parse_method_tail(
                virtual_prefix, base_polymorphic, declares_virtual, true);
            if (!succeeded(status)) return status;
            continue;
        }

        if (at(token_kind::identifier) &&
            current()->identifier ==
                record_name) {

            status =
                consume();

            if (!succeeded(status)) {
                return status;
            }

            if (at(token_kind::l_paren)) {
                if (virtual_prefix) {
                    return fail(parser_v2_failure_kind::syntax,
                        "Constructor cannot be virtual");
                }
                if (constructor_seen) {
                    return fail(
                        parser_v2_failure_kind::semantic,
                        "Header Parser V2 supports one managed default constructor per record");
                }

                constructor_seen =
                    true;

                status =
                    parse_constructor(
                        record_name,
                        members,
                        names,
                        construction,
                        pending,
                        references,
                        nested);

                if (!succeeded(status)) {
                    return status;
                }

                continue;
            }

            resolved_type self_type;

            status =
                resolve_named_type(
                    identity,
                    self_type);

            if (!succeeded(status)) {
                return status;
            }

            status =
                parse_type_tail(
                    self_type);

            if (!succeeded(status)) {
                return status;
            }

            if (at(token_kind::kw_operator) ||
                (at(token_kind::identifier) &&
                 current()->identifier == record_name)) {
                status = parse_named_operator(
                    record_name, virtual_prefix,
                    base_polymorphic, declares_virtual);
            }
            else {
                status = parse_member_declarator(
                    self_type,
                    access,
                    members,
                    construction,
                    pending,
                    references,
                    nested,
                    names,
                    virtual_prefix,
                    base_polymorphic,
                    declares_virtual);
            }

            if (!succeeded(status)) {
                return status;
            }

            continue;
        }

        status =
            parse_member(
                scope,
                record_name,
                access,
                members,
                construction,
                pending,
                references,
                nested,
                names,
                virtual_prefix,
                base_polymorphic,
                declares_virtual);

        if (!succeeded(status)) {
            return status;
        }
    }

    status =
        expect(
            token_kind::r_brace,
            "Header Parser V2 expected '}' after record body");

    if (!succeeded(status)) {
        return status;
    }

    // All forward declarations must be satisfied by the end of this record.
    // No second pass over constructor operations or member initializations.
    if (!pending.empty()) {
        auto earliest = pending.begin();
        for (auto it = pending.begin(); it != pending.end(); ++it) {
            if (it->second.source.offset < earliest->second.source.offset) {
                earliest = it;
            }
        }
        return fail_at(parser_v2_failure_kind::semantic,
            "Header Parser V2 constructor target is not a field of this record",
            earliest->second.file, earliest->second.source);
    }

    if (!nested.waiting.empty()) {
        const nested_operation* earliest = nullptr;
        for (const auto& bucket : nested.waiting) {
            for (const auto& operation : bucket.second) {
                if (earliest == nullptr || operation.source.offset < earliest->source.offset)
                    earliest = &operation;
            }
        }
        if (earliest == nullptr) return server_status::project_configuration_invalid;
        return fail_at(parser_v2_failure_kind::semantic,
            "Constructor target is not a field of this record",
            earliest->file, earliest->source);
    }

    if (!references.waiting.empty()) {
        // Only unresolved forward names are checked. No second field or
        // constructor-operation traversal, and no token replay.
        auto earliest = references.waiting.begin();
        for (auto it = references.waiting.begin();
             it != references.waiting.end(); ++it) {
            if (it->second.source.offset < earliest->second.source.offset)
                earliest = it;
        }
        return fail_at(parser_v2_failure_kind::semantic,
            "Reference binding names neither a member nor a visible Header static object",
            earliest->second.file, earliest->second.source);
    }

    status =
        expect(
            token_kind::semicolon,
            "Header Parser V2 expected ';' after record definition");

    if (!succeeded(status)) {
        return status;
    }

#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
    {
        header_v2_timed_probe probe(profile != nullptr ? &profile->graph_define_ms : nullptr);
#endif
        status =
            G.define_resolved_record(
                handle,
                kind,
                members,
                construction,
                bases,
                declares_virtual);
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
    }
    if (succeeded(status) && profile != nullptr) {
        ++profile->graph_define_calls;
        profile->graph_defined_members += members.size();
    }
#endif

    if (!succeeded(status)) {
        return fail(
            parser_v2_failure_kind::semantic,
            "Header Parser V2 record definition conflicts with G");
    }

    // Cold sparse defaults only, emitted with the existing Graph ABI.
    for (const auto& item : nested.completed) {
        const auto added = G.constructor_defaults.add(item);
        if (!succeeded(added)) {
            return fail(parser_v2_failure_kind::semantic,
                "Nested constructor default conflicts with Graph");
        }
    }
    return server_status::success;
}


server_status header_parser_v2::parse_namespace(
    identity_ref scope,
    std::size_t depth) noexcept {

    auto status =
        expect(
            token_kind::kw_namespace,
            "Header Parser V2 expected 'namespace'");

    if (!succeeded(status)) {
        return status;
    }

    const auto* name =
        current();

    if (name == nullptr ||
        name->kind !=
            token_kind::identifier ||
        !name->identifier) {

        return fail(
            parser_v2_failure_kind::syntax,
            "Header Parser V2 expected a namespace name");
    }

    identity_ref nested;

    status =
        identities.resolve(
            scope,
            name->identifier,
            identity_kind::
                namespace_scope,
            nested);

    if (!succeeded(status) ||
        !nested) {

        return fail(
            parser_v2_failure_kind::semantic,
            "Header Parser V2 namespace identity could not be resolved");
    }

    status =
        consume();

    if (!succeeded(status)) {
        return status;
    }

    status =
        expect(
            token_kind::l_brace,
            "Header Parser V2 expected '{' after namespace name");

    if (!succeeded(status)) {
        return status;
    }

    return parse_scope(
        nested,
        true,
        depth + 1);
}

// HEADER-V2-TYPEDEF-07: normalize typedef immediately, one token pass.
// OLD intentionally restricts typedef to intrinsic scalars, not records,
// pointers, arrays, references or function declarators.
server_status header_parser_v2::parse_typedef(
    identity_ref scope) noexcept {

    auto status = expect(
        token_kind::kw_typedef,
        "Expected typedef");
    if (!succeeded(status)) return status;

    resolved_type parsed;
    status = parse_type(scope, parsed);
    if (!succeeded(status)) return status;

    intrinsic_type intrinsic = intrinsic_type::none;
    if (!G.intrinsic(parsed.type, intrinsic) ||
        intrinsic < intrinsic_type::bool_type ||
        intrinsic > intrinsic_type::long_double_type) {
        return fail(parser_v2_failure_kind::unsupported,
            "Typedef currently supports intrinsic scalar aliases");
    }

    const auto* alias = current();
    if (alias == nullptr || alias->kind != token_kind::identifier ||
        !alias->identifier) {
        return fail(parser_v2_failure_kind::syntax,
            "Expected typedef alias identifier");
    }
    const auto alias_name = alias->identifier;
    const auto alias_file = alias->file;
    const source_range alias_source{alias->source_offset, alias->source_length};
    status = consume();
    if (!succeeded(status)) return status;

    status = expect(token_kind::semicolon,
        "Expected ';' after typedef");
    if (!succeeded(status)) return status;

    identity_ref identity;
    status = identities.resolve(
        scope, alias_name, identity_kind::type, identity);
    if (!succeeded(status) || !identity) {
        return fail_at(parser_v2_failure_kind::semantic,
            "Typedef identity conflicts with existing semantic identity",
            alias_file, alias_source);
    }

    type_handle handle;
    status = G.define_intrinsic_alias(identity, intrinsic, handle);
    if (!succeeded(status) || !handle) {
        return fail_at(parser_v2_failure_kind::semantic,
            "Typedef conflicts with existing type in G",
            alias_file, alias_source);
    }
    return server_status::success;
}

server_status header_parser_v2::parse_scope(
    identity_ref scope,
    bool closing_brace,
    std::size_t depth) noexcept {

    if (depth >
        parser_v2_scope_depth_limit) {

        return fail(
            parser_v2_failure_kind::unsupported,
            "Header Parser V2 namespace nesting exceeds supported depth");
    }

    while (!input.finished()) {
        if (closing_brace &&
            at(token_kind::r_brace)) {

            return consume();
        }

        // HEADER-V2-TYPEDEF-07: Header-scope alias declaration.
        if (at(token_kind::kw_typedef)) {
            const auto status = parse_typedef(scope);
            if (!succeeded(status)) return status;
            continue;
        }

        if (at(token_kind::kw_namespace)) {
            const auto status =
                parse_namespace(
                    scope,
                    depth);

            if (!succeeded(status)) {
                return status;
            }

            continue;
        }

        if (at(token_kind::kw_struct) ||
            at(token_kind::kw_class) ||
            at(token_kind::kw_union)) {

            const auto status =
                parse_record(
                    scope);

            if (!succeeded(status)) {
                return status;
            }

            continue;
        }

        return fail(
            parser_v2_failure_kind::unsupported,
            "Header Parser V2 token is outside the initial supported grammar");
    }

    return closing_brace
        ? fail(
            parser_v2_failure_kind::syntax,
            "Header Parser V2 expected closing '}'")
        : server_status::success;
}

server_status header_parser_v2::parse() noexcept {

    if (failure != nullptr) {
        *failure = {};
    }

    if (input.finished()) {
        return server_status::success;
    }

    return parse_scope(
        identities.root(),
        false,
        0);
}

}
