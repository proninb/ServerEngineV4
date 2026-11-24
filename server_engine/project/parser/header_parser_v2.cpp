#include "parser.hpp"
#include "enum_constants.hpp"
#include "header_parser_v2.hpp"
#include "scalar_type_names.hpp"

#include "../graph/construction_semantics.hpp"
#include "../graph/constructor_member_lookup.hpp"

#include <limits>
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
#include <chrono>
#endif
#include <algorithm>
#include <charconv>
#include <string>
#include <utility>

namespace cw::server {
namespace {

construction_value narrow_character_default(std::uint8_t byte) noexcept {
    if (byte < 128) return construction_value::constant(construction_kind::unsigned_integer, byte);
    return construction_value::constant(construction_kind::signed_integer,
        static_cast<std::uint64_t>(static_cast<std::int64_t>(byte)-256));
}

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

server_status header_parser_v2::record_dependency(identity_ref identity) noexcept {
    if (templates->array_aliases.contains(identity.value())) return server_status::success;
    auto status = input.record_reuse(semantic_preprocessor_v2::reuse_event::dependency,
        current() != nullptr ? current()->file : file_id{}, identity.value());
    return succeeded(status) ? provenance->add_dependency(identity) : status;
}

server_status header_parser_v2::record_contribution(file_id file, source_data_ref data) noexcept {
    auto status = input.record_reuse(semantic_preprocessor_v2::reuse_event::contribution,
        file, data.raw());
    return succeeded(status) ? provenance->add(file, data) : status;
}

const prepared_token*
header_parser_v2::current() const noexcept {
    if (has_split_greater) return &split_greater;
    if (!template_replays.empty()) {
        const auto& replay = template_replays.back();
        return replay.cursor < replay.tokens.size() ? &replay.tokens[replay.cursor] : nullptr;
    }
    return input.current();
}

bool header_parser_v2::at(
    token_kind kind) const noexcept {

    const auto* token = current();
    return token && token->kind == kind;
}

server_status header_parser_v2::consume() noexcept {
    if (has_split_greater) {
        has_split_greater = false;
        return server_status::success;
    }
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
    header_v2_detail_probe perf06_probe(
        profile,
        &header_parser_v2_profile::advance_calls,
        &header_parser_v2_profile::advance_samples,
        &header_parser_v2_profile::advance_sample_ms);
#endif
    if (!template_replays.empty()) {
        ++template_replays.back().cursor;
        return server_status::success;
    }
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

    if (const auto alias = templates->array_aliases.find(identity.value());
        alias != templates->array_aliases.end()) {
        output.type = alias->second;
        output.complete = true;
        return server_status::success;
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
            entry->kind != graph_type_kind::record &&
            entry->kind != graph_type_kind::enumeration && entry->kind != graph_type_kind::nominal_scalar) {

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
    if (current() && current()->kind == token_kind::identifier &&
        input.semantic_strings().get(current()->identifier) == "__int64") {
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
    if (first && first->kind == token_kind::identifier && !template_replays.empty()) {
        for (const auto& parameter : template_replays.back().types) {
            if (first->identifier != parameter.first) continue;
            output = parameter.second;
            const auto advanced = consume();
            if (!succeeded(advanced)) return advanced;
            return parse_type_tail(output, const_qualified, volatile_qualified);
        }
    }
    if (first && first->kind == token_kind::identifier &&
        (scalar_type_name(input.semantic_strings().get(first->identifier)) != intrinsic_type::none ||
         input.semantic_strings().get(first->identifier)=="std") && !find_type_identity(scope, first->identifier)) {
        auto intrinsic = scalar_type_name(input.semantic_strings().get(first->identifier));
        if (intrinsic != intrinsic_type::none) {
            output.type = G.intrinsic(intrinsic);
            const auto status = consume();
            return succeeded(status) ? parse_type_tail(output, const_qualified, volatile_qualified) : status;
        }
        identity_ref visible_namespace;
        if (input.semantic_strings().get(first->identifier) == "std") {
            for (auto search=scope; search;) {
                visible_namespace=identities.find(search,first->identifier,identity_kind::namespace_scope);
                if (visible_namespace || search==identities.root()) break;
                identity_record parent;
                if (!identities.record(search,parent)) break;
                search=parent.parent;
            }
        }
        if (input.semantic_strings().get(first->identifier) == "std" && !visible_namespace) {
            auto status = consume();
            if (!succeeded(status)) return status;
            status = expect(token_kind::scope,"Expected '::' after std");
            if (!succeeded(status)) return status;
            intrinsic = at(token_kind::identifier) ? scalar_type_name(input.semantic_strings().get(current()->identifier),true) : intrinsic_type::none;
            if (intrinsic == intrinsic_type::none)
                return fail(parser_v2_failure_kind::unsupported,"Unsupported std scalar type name");
            output.type = G.intrinsic(intrinsic);
            status = consume();
            return succeeded(status) ? parse_type_tail(output, const_qualified, volatile_qualified) : status;
        }
    }
    if ((first != nullptr && header_v2_builtin_start(first->kind)) ||
        (first && first->kind == token_kind::identifier &&
         input.semantic_strings().get(first->identifier) == "__int64")) {
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

        identity_ref namespace_scope;
        for (auto search = scope; search;) {
            namespace_scope = identities.find(search, token->identifier, identity_kind::namespace_scope);
            if (namespace_scope || search == identities.root()) break;
            identity_record parent;
            if (!identities.record(search, parent)) break;
            search = parent.parent;
        }
        if (!identity && !namespace_scope)
            return fail(parser_v2_failure_kind::semantic, "Header Parser V2 named type is unknown");
        auto advanced = consume();
        if (!succeeded(advanced)) return advanced;
        while (at(token_kind::scope)) {
            const auto parent = namespace_scope ? namespace_scope : identity;
            if (!parent) return fail(parser_v2_failure_kind::semantic, "Unknown qualified type scope");
            advanced = consume();
            if (!succeeded(advanced)) return advanced;
            if (!at(token_kind::identifier))
                return fail(parser_v2_failure_kind::syntax, "Expected qualified type name");
            identity = identities.find(parent, current()->identifier, identity_kind::type);
            namespace_scope = identities.find(parent, current()->identifier, identity_kind::namespace_scope);
            advanced = consume();
            if (!succeeded(advanced)) return advanced;
        }
        if (!identity) {
            return fail(parser_v2_failure_kind::semantic, "Header Parser V2 named type is unknown");
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
            resolved = at(token_kind::less)
                ? instantiate_template(identity, output, scope)
                : resolve_named_type(identity, output);
        }

        if (!succeeded(resolved)) {
            return resolved;
        }

        if (provenance != nullptr) {
            const auto dependency = record_dependency(identity);
            if (!succeeded(dependency)) return dependency;
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

        std::int64_t bound = 0;
        status = parse_integer_expression(bound);
        if (!succeeded(status)) return status;
        if (bound <= 0) {
            return fail(
                parser_v2_failure_kind::semantic,
                "Header Parser V2 array bound must be greater than zero");
        }

        try {
            bounds.push_back(
                static_cast<std::uint64_t>(bound));
        }
        catch (...) {
            return server_status::io_error;
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
    if (at(token_kind::identifier)) return parse_enum_constant(output);
    if (at(token_kind::l_paren)) {
        std::int64_t value = 0;
        const auto status = parse_integer_expression(value);
        if (!succeeded(status)) return status;
        output = construction_value::constant(value < 0 ? construction_kind::signed_integer :
            construction_kind::unsigned_integer, static_cast<std::uint64_t>(value));
        return status;
    }

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

    auto status = consume();
    if (!succeeded(status)) return status;
    if (at(token_kind::plus) || at(token_kind::minus) || at(token_kind::star) ||
        at(token_kind::slash) || at(token_kind::percent)) {
        const auto signed_limit = static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
        if (magnitude > signed_limit + (negative ? 1u : 0u))
            return fail(parser_v2_failure_kind::semantic, "Integer initializer exceeds signed expression range");
        auto value = static_cast<std::int64_t>(output.bits());
        status = parse_integer_expression_tail(value, 0, 0);
        if (!succeeded(status)) return status;
        output = construction_value::constant(value < 0 ? construction_kind::signed_integer :
            construction_kind::unsigned_integer, static_cast<std::uint64_t>(value));
    }
    return status;
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

    if (at(token_kind::identifier) && enum_lookup_members && enum_lookup_names &&
        enum_lookup_names->find(*enum_lookup_members, current()->identifier) < enum_lookup_members->size())
        return parse_reference_name(output);
    construction_value constant;
    identity_ref visible_namespace;
    if (at(token_kind::identifier)) {
        for (auto search = constant_scope; search;) {
            visible_namespace = identities.find(search, current()->identifier, identity_kind::namespace_scope);
            if (visible_namespace || search == identities.root()) break;
            identity_record record;
            if (!identities.record(search, record)) break;
            search = record.parent;
        }
    }
    if (at(token_kind::identifier) &&
        (enum_constant(G, identities, constant_scope, current()->identifier, constant) ||
         find_type_identity(constant_scope, current()->identifier) ||
         visible_namespace)) {
        return parse_enum_constant(output.value);
    }
    if (allow_reference_name &&
        (at(token_kind::identifier) || at(token_kind::kw_this))) {
        return parse_reference_name(output);
    }
    return parse_scalar_constant(output.value);
}

server_status header_parser_v2::internal_static_scope(identity_ref scope, identity_ref& output) noexcept {
    try {
        auto& strings=input.semantic_strings();
        if (!internal_static_scope_name) {
            const auto text = "<static-root:" + std::to_string(input.semantic_root().value()) + ">";
            const auto status = strings.intern(text, internal_static_scope_name);
            if (!succeeded(status)) return status;
        }
        return identities.resolve(scope, internal_static_scope_name, identity_kind::namespace_scope, output);
    } catch (...) { return server_status::io_error; }
}

identity_ref header_parser_v2::find_static_object(identity_ref scope, string_id name) noexcept {
    if (!internal_static_scope_name) return {};
    for (auto search=scope; search;) {
        const auto internal = identities.find(search, internal_static_scope_name, identity_kind::namespace_scope);
        if (internal) {
            const auto identity=identities.find(internal, name, identity_kind::object);
            const auto object = G.find_object(identity);
            const auto* entry = G.find(object);
            if (entry && entry->internal_static()) return identity;
        }
        identity_record parent;
        if (!identities.record(search, parent)) break;
        search = parent.parent;
    }
    return {};
}

server_status header_parser_v2::initialize_char_array(identity_ref object, type_ref array,
    std::span<const std::uint8_t> bytes) noexcept {
    derived_type_record shape;
    if (!G.derived(array, shape) || shape.kind != derived_type_kind::bounded_array || bytes.size()>shape.payload)
        return server_status::project_configuration_invalid;
    for (std::size_t i=0; i<bytes.size(); ++i) {
        if (!bytes[i]) continue; // backing is zero-initialized, including NUL and padding
        const endpoint_path_step step{static_cast<std::uint64_t>(i), endpoint_path_step_kind::array_index, {}};
        endpoint_path_handle path;
        auto status = G.intern_resolved_endpoint_path(array, std::span{&step,1}, shape.child, path);
        if (!succeeded(status)) return status;
        bool replaced=false;
        status = G.add_resolved_initialization({object, endpoint_ref::from_path(path)}, shape.child,
            narrow_character_default(bytes[i]), replaced);
        if (!succeeded(status)) return status;
    }
    return server_status::success;
}

server_status header_parser_v2::parse_string_initializer(type_ref target,
    constructor_operation& output, std::vector<std::uint8_t>& bytes) noexcept {
    try {
        auto& strings=input.semantic_strings();
        derived_type_record array;
        intrinsic_type intrinsic;
        const bool is_array = G.derived(target, array) && array.kind==derived_type_kind::bounded_array &&
            G.intrinsic(array.child, intrinsic) && intrinsic==intrinsic_type::char_type;
        const bool is_pointer = const_char_pointer(G, target);
        if (!is_array && !is_pointer) return fail(parser_v2_failure_kind::semantic,
            "String initializer requires a bounded char array or pointer to const char");
        bytes.clear();
        while (at(token_kind::string_literal)) {
            const auto text = input.spelling(*current());
            if (text.size()<2 || text.front()!='"' || text.back()!='"')
                return fail(parser_v2_failure_kind::unsupported, "Only ordinary narrow string literals are supported");
            for (std::size_t i=1; i+1<text.size(); ++i) {
                unsigned value=static_cast<unsigned char>(text[i]);
                if (text[i]=='\\') {
                    if (++i+1>=text.size()) return fail(parser_v2_failure_kind::syntax,"Incomplete string escape");
                    switch (text[i]) {
                    case 'n': value=10; break; case 'r': value=13; break; case 't': value=9; break;
                    case 'a': value=7; break; case 'b': value=8; break; case 'f': value=12; break; case 'v': value=11; break;
                    case '\\': value='\\'; break; case '"': value='"'; break; case '\'': value='\''; break; case '?': value='?'; break;
                    default: {
                        const bool hex=text[i]=='x';
                        const auto start=hex?i+1:i;
                        auto end=start;
                        while (end+1<text.size() && (hex ?
                            ((text[end]>='0'&&text[end]<='9')||(text[end]>='a'&&text[end]<='f')||(text[end]>='A'&&text[end]<='F')) :
                            (end<start+3&&text[end]>='0'&&text[end]<='7'))) ++end;
                        if (start==end) return fail(parser_v2_failure_kind::unsupported,"Unsupported string escape");
                        unsigned code=0;
                        const auto parsed=std::from_chars(text.data()+start,text.data()+end,code,hex?16:8);
                        if (parsed.ec!=std::errc{} || code>255) return fail(parser_v2_failure_kind::semantic,"String escape exceeds narrow character range");
                        value=code; i=end-1; break;
                    }
                    }
                }
                bytes.push_back(static_cast<std::uint8_t>(value));
                if (is_array && bytes.size()>=array.payload)
                    return fail(parser_v2_failure_kind::semantic,"String initializer including NUL exceeds char array bound");
            }
            const auto status=consume(); if (!succeeded(status)) return status;
        }
        bytes.push_back(0);
        if (is_array) { output.value={}; return server_status::success; }
        // Canonical project-owned literal backing: no parser/process pointer in G.
        string_id content, scope_name, object_name;
        auto status=strings.intern(std::string_view{reinterpret_cast<const char*>(bytes.data()),bytes.size()},content);
        if (!succeeded(status)) return status;
        status=strings.intern("<string-literals>",scope_name); if (!succeeded(status)) return status;
        status=strings.intern("<literal:"+std::to_string(content.value())+">",object_name); if (!succeeded(status)) return status;
        identity_ref scope, object_id;
        status=identities.resolve(identities.root(),scope_name,identity_kind::namespace_scope,scope); if (!succeeded(status)) return status;
        status=identities.resolve(scope,object_name,identity_kind::object,object_id); if (!succeeded(status)) return status;
        if (!G.find_object(object_id)) {
            type_ref storage;
            status=G.derive_resolved(G.intrinsic(intrinsic_type::char_type),derived_type_kind::bounded_array,bytes.size(),storage);
            if (!succeeded(status)) return status;
            object_handle object;
            status=G.add_resolved_object(object_id,storage,object,graph_object_internal_static); if (!succeeded(status)) return status;
            status=initialize_char_array(object_id,storage,bytes); if (!succeeded(status)) return status;
        }
        output.value=construction_value::object_binding(object_id.value());
        if (provenance) {
            status=record_contribution(output.file,source_data_ref::object(object_id)); if (!succeeded(status)) return status;
            status=record_dependency(object_id); if (!succeeded(status)) return status;
        }
        return server_status::success;
    } catch (...) { return server_status::io_error; }
}

server_status header_parser_v2::parse_static_object(identity_ref scope) noexcept {
    auto status=consume(); if (!succeeded(status)) return status;
    resolved_type type;
    status=parse_type(scope,type); if (!succeeded(status)) return status;
    const auto* token=current();
    if (!token || token->kind!=token_kind::identifier || !token->identifier)
        return fail(parser_v2_failure_kind::syntax,"Expected Header static object name");
    const auto name=token->identifier; const auto file=token->file;
    status=consume(); if (!succeeded(status)) return status;
    status=parse_array_suffix(type); if (!succeeded(status)) return status;
    if (!type.complete && !type.indirect) return fail(parser_v2_failure_kind::semantic,"Header static object requires a complete type");
    constructor_operation initial; std::vector<std::uint8_t> bytes; bool explicit_value=false;
    status=parse_member_initializer(type.type,initial,&bytes,&explicit_value); if (!succeeded(status)) return status;
    if (initial.reference_name || initial.value.kind==construction_kind::object_binding)
        return fail(parser_v2_failure_kind::unsupported,"Header static object initializer requires a scalar constant or zero initialization");
    if (!at(token_kind::semicolon)) return fail(parser_v2_failure_kind::syntax,"Expected ';' after Header static object");
    identity_ref internal, identity;
    status=internal_static_scope(scope,internal); if (!succeeded(status)) return status;
    status=identities.resolve(internal,name,identity_kind::object,identity); if (!succeeded(status)) return status;
    object_handle object;
    status=G.add_resolved_object(identity,type.type,object,graph_object_internal_static |
        (explicit_value?graph_object_non_default_initializer:0),initial.value);
    if (!succeeded(status)) return fail(parser_v2_failure_kind::semantic,"Conflicting Header static object declaration");
    if (!bytes.empty()) { status=initialize_char_array(identity,type.type,bytes); if (!succeeded(status)) return status; }
    if (provenance) { status=record_contribution(file,source_data_ref::object(identity)); if (!succeeded(status)) return status; }
    return consume();
}

server_status header_parser_v2::parse_relative_aggregate_referent(string_id& output, string_id first) noexcept {
    try {
        std::string path;
        if (!first && at(token_kind::kw_this)) {
            auto status = consume();
            if (!succeeded(status)) return status;
            status = expect(token_kind::arrow, "Expected '->' after 'this' in aggregate reference");
            if (!succeeded(status)) return status;
        }
        for (;;) {
            if (first) {
                path.append(input.semantic_strings().get(first));
                first = {};
            } else {
                const auto* member = current();
                if (!member || member->kind != token_kind::identifier || !member->identifier)
                    return fail(parser_v2_failure_kind::syntax, "Expected relative member path in Header reference");
                if (!path.empty()) path.push_back('.');
                path.append(input.semantic_strings().get(member->identifier));
                const auto status = consume();
                if (!succeeded(status)) return status;
            }
            while (at(token_kind::l_bracket)) {
                auto status = consume();
                if (!succeeded(status)) return status;
                const bool negative = at(token_kind::minus);
                if (negative) {
                    status = consume();
                    if (!succeeded(status)) return status;
                }
                const auto* index = current();
                if (!index || index->kind != token_kind::pp_number ||
                    index->number.kind != prepared_number_kind_v2::unsigned_integer)
                    return fail(parser_v2_failure_kind::syntax, "Expected constant array index in Header reference");
                path.push_back('[');
                if (negative) path.push_back('-');
                path.append(std::to_string(index->number.bits));
                path.push_back(']');
                status = consume();
                if (!succeeded(status)) return status;
                status = expect(token_kind::r_bracket, "Expected ']' in Header reference");
                if (!succeeded(status)) return status;
            }
            if (!at(token_kind::dot)) break;
            const auto status = consume();
            if (!succeeded(status)) return status;
        }
        return input.semantic_strings().intern(path, output);
    }
    catch (...) { return server_status::io_error; }
}

server_status header_parser_v2::parse_aggregate_elements(
    type_ref target, nested_operation& operation,
    std::vector<nested_operation>& output) noexcept {

    type_handle record;
    const auto* entry = G.named(target, record) ? G.find(record) : nullptr;
    const bool is_record = entry && entry->kind == graph_type_kind::record;
    derived_type_record array;
    const bool is_array = G.derived(target, array) && array.kind == derived_type_kind::bounded_array;
    const bool designated = at(token_kind::dot);
    if ((!is_record && !is_array) || (designated && !is_record) ||
        (is_record && !designated && entry->bases.count != 0 &&
         !(entry->index_member_slot() == 1 && entry->members.count == 1))) {
        return fail(parser_v2_failure_kind::unsupported,
            "Unsupported target for aggregate member initializer");
    }

    std::uint64_t next_index = 0;
    while (!at(token_kind::r_brace)) {
        type_ref child;
        nested_path_step step;
        if (is_record) {
            if (designated) {
                auto status = expect(token_kind::dot, "Expected a field designator");
                if (!succeeded(status)) return status;
                const auto* name = current();
                if (name == nullptr || name->kind != token_kind::identifier || !name->identifier)
                    return fail(parser_v2_failure_kind::syntax, "Expected a field name after '.'");
                const auto index = G.find_member(record, name->identifier);
                const auto* member = G.member(record, index);
                if (member == nullptr)
                    return fail(parser_v2_failure_kind::semantic, "Unknown aggregate initializer field");
                if (index.value() < next_index)
                    return fail(parser_v2_failure_kind::semantic,
                        "Aggregate designators must follow field declaration order");
                next_index = static_cast<std::uint64_t>(index.value()) + 1;
                step.name = member->name;
                child = member->type;
                status = consume();
                if (!succeeded(status)) return status;
                status = expect(token_kind::assign, "Expected '=' after field designator");
                if (!succeeded(status)) return status;
            }
            else {
                const auto fields = G.members(record);
                if (next_index >= fields.size())
                    return fail(parser_v2_failure_kind::semantic, "Too many record initializers");
                const auto* member = &fields[static_cast<std::size_t>(next_index++)];
                step.name = member->name;
                child = member->type;
            }
        }
        else {
            if (next_index >= array.payload)
                return fail(parser_v2_failure_kind::semantic, "Too many array initializers");
            step.index = next_index++;
            child = array.child;
        }

        if (operation.path.size() >= parser_v2_scope_depth_limit)
            return fail(parser_v2_failure_kind::unsupported, "Aggregate initializer path exceeds supported depth");
        try { operation.path.push_back(step); }
        catch (...) { return server_status::io_error; }
        auto status = server_status::success;
        if (at(token_kind::l_brace)) {
            status = consume();
            if (!succeeded(status)) return status;
            // An empty clause keeps the element's default construction.
            status = at(token_kind::r_brace) ? consume()
                : parse_aggregate_elements(child, operation, output);
        }
        else {
            derived_type_record reference;
            if (G.derived(child, reference) &&
                (reference.kind == derived_type_kind::lvalue_reference ||
                 reference.kind == derived_type_kind::rvalue_reference)) {
                status = parse_relative_aggregate_referent(operation.referent);
            }
            else {
                status = parse_scalar_constant(operation.value);
            }
            if (succeeded(status)) {
                try { output.push_back(operation); }
                catch (...) { return server_status::io_error; }
            }
        }
        operation.referent = {};
        operation.path.pop_back();
        if (!succeeded(status)) return status;
        if (!at(token_kind::comma)) break;
        status = consume();
        if (!succeeded(status)) return status;
    }
    return expect(token_kind::r_brace, "Expected '}' after aggregate member initializer");
}

server_status header_parser_v2::parse_member_initializer(
    type_ref target, constructor_operation& output,
    std::vector<std::uint8_t>* string_bytes, bool* explicit_value,
    std::vector<nested_operation>* aggregate) noexcept {

    output = {};
    if (explicit_value) *explicit_value = false;
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

    const auto complete_operand = [&]() {
        if (aggregate && reference_target && output.reference_name &&
            (at(token_kind::dot) || at(token_kind::l_bracket)))
            return parse_relative_aggregate_referent(output.relative_referent, output.reference_name);
        return server_status::success;
    };

    if (at(token_kind::assign)) {
        const auto status = consume();
        if (!succeeded(status)) return status;
    }
    if (aggregate && reference_target && at(token_kind::identifier) &&
        input.semantic_strings().get(current()->identifier) == "unconnected")
        return consume();

    if (at(token_kind::l_brace)) {
        auto status = consume();
        if (!succeeded(status)) return status;
        if (!at(token_kind::r_brace)) {
            if (aggregate != nullptr && !at(token_kind::string_literal)) {
                type_handle record;
                derived_type_record array;
                const auto* entry = G.named(target, record) ? G.find(record) : nullptr;
                if ((entry && entry->kind == graph_type_kind::record) ||
                    (G.derived(target, array) && array.kind == derived_type_kind::bounded_array)) {
                    nested_operation operation;
                    operation.file = output.file;
                    operation.source = output.source;
                    if (explicit_value) *explicit_value = true;
                    return parse_aggregate_elements(target, operation, *aggregate);
                }
            }
            if (explicit_value) *explicit_value = true;
            status = at(token_kind::string_literal) && string_bytes
                ? parse_string_initializer(target, output, *string_bytes)
                : parse_construction_operand(output, reference_target);
            if (succeeded(status)) status = complete_operand();
            if (!succeeded(status)) return status;
        }
        status = expect(token_kind::r_brace,
            "Header Parser V2 expected '}' after member initializer");
        if (!succeeded(status)) return status;
    }
    else {
        if (explicit_value) *explicit_value = true;
        auto status = at(token_kind::string_literal) && string_bytes
            ? parse_string_initializer(target, output, *string_bytes)
            : parse_construction_operand(output, reference_target);
        if (succeeded(status)) status = complete_operand();
        if (!succeeded(status)) return status;
    }

    auto normalized = output.value;
    if (!output.reference_name &&
        (!normalize_enum_constant(G, target, normalized) ||
         !construction_compatible(G, target, normalized))) {
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
    auto normalized = input_value.value;
    auto revision = references.generations.find(target);

    if (!input_value.reference_name) {
        if ((!normalize_enum_constant(G, target_type, normalized) ||
            !construction_compatible(G, target_type, normalized))) {
            return fail_at(parser_v2_failure_kind::semantic,
                "Header Parser V2 construction value is incompatible with its field",
                input_value.file, input_value.source);
        }
        if (revision != references.generations.end()) ++revision->second;
        construction[target] = normalized;
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
                    input_value.reference_file, input_value.reference_source, input_value.reference_name});
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

    if (!template_replays.empty())
        for (const auto& parameter : template_replays.back().types)
            if (parameter.first == member_name)
                return fail(parser_v2_failure_kind::semantic, "Member declaration shadows a template parameter");

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
    std::vector<std::uint8_t> string_bytes;
    std::vector<nested_operation> aggregate;

    {
    #ifdef CW_HEADER_V2_SEMANTIC_PROFILE
        header_v2_detail_probe perf06_probe(
            profile,
            &header_parser_v2_profile::member_initializer_calls,
            &header_parser_v2_profile::member_initializer_samples,
            &header_parser_v2_profile::member_initializer_sample_ms);
    #endif
        status = parse_member_initializer(type.type, initial, &string_bytes, nullptr, &aggregate);
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
            // HEADER-V2-MEMBER-RESERVE-PROMOTE-26: reserve lazily on the
            // first data member; empty and method-only records allocate nothing.
            if (members.empty()) {
                members.reserve(16);
                construction.reserve(16);
            }
            members.push_back({member_name, type.type, access, {}});
            construction.push_back({});
        }
        catch (...) { return server_status::io_error; }
    }

    const auto index = members.size() - 1;
    if (initial.relative_referent) {
        try {
            templates->relative_bindings.push_back({
                nested.owner, member_name, initial.relative_referent, {}, initial.file, initial.source});
        }
        catch (...) { return server_status::io_error; }
    } else if (has_initializer) {
        status = apply_record_operand(index, initial, members, construction,
            names, references);
        if (!succeeded(status)) return status;
    }

    for (auto& operation : aggregate) {
        operation.root = member_name;
        status = append_nested_operation(std::move(operation), members, names, nested);
        if (!succeeded(status)) return status;
    }
    const auto pending_it = pending.find(member_name.value());
    derived_type_record string_array;
    if (!string_bytes.empty() && G.derived(type.type, string_array) &&
        string_array.kind == derived_type_kind::bounded_array) {
        for (std::size_t i=0; i<string_bytes.size(); ++i) {
            nested_operation operation;
            operation.root = member_name; operation.file = initial.file; operation.source = initial.source;
            operation.value = narrow_character_default(string_bytes[i]);
            try { operation.path.push_back({{}, i}); } catch (...) { return server_status::io_error; }
            status = append_nested_operation(std::move(operation), members, names, nested);
            if (!succeeded(status)) return status;
        }
    }
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

#ifdef CW_HEADER_MODEL_EFFECTIVE_V2
server_status header_parser_v2::try_effective_field(
    identity_ref scope,
    graph_member_access access,
    std::vector<member_record>& members,
    std::vector<construction_value>& construction,
    pending_constructor_operations& pending,
    record_reference_state& references,
    record_nested_state& nested,
    record_member_name_set& names,
    bool& handled) noexcept {

    handled = false;
    if (effective_model == nullptr) return server_status::success;
    const auto* token = current();
    if (token == nullptr) return server_status::success;
    // A physical offset is stable across preprocessing. Token ordinals are not.
    const auto* field = effective_model->find(token->file, token->source_offset);
    if (field == nullptr || field->builtin != token->kind)
        return server_status::success;

    // At this point the current physical file has already been interned by the
    // *original streaming* preprocessor. Reuse its canonical string_id, do not
    // create another string or depend on the future include visitation order.
    auto& strings = input.semantic_strings();
    const auto raw_name = effective_model->spelling(
        field->file, field->name_offset, field->name_length);
    if (raw_name.empty()) return server_status::success;
    const auto member_name = strings.find(raw_name);
    if (!member_name || input.model_macro_defined(member_name))
        return server_status::success;

    resolved_type type;
    if (field->builtin == token_kind::identifier) {
        const auto raw_type = effective_model->spelling(
            field->file, field->type_offset, field->type_length);
        if (raw_type.empty()) return server_status::success;
        const auto type_name = strings.find(raw_type);
        // Even a macro expanding to itself must follow the old parser.
        if (!type_name || input.model_macro_defined(type_name) ||
            token->identifier != type_name) return server_status::success;
        const auto identity = find_type_identity(scope, type_name);
        if (!identity) return server_status::success;
        const auto status = resolve_named_type(identity, type);
        if (!succeeded(status)) return status;
        if (provenance != nullptr) {
            const auto dependency = record_dependency(identity);
            if (!succeeded(dependency)) return dependency;
        }
    } else {
        intrinsic_type intrinsic = intrinsic_type::none;
        switch (field->builtin) {
        case token_kind::kw_bool: intrinsic = intrinsic_type::bool_type; break;
        case token_kind::kw_int: intrinsic = intrinsic_type::signed_int; break;
        case token_kind::kw_short: intrinsic = intrinsic_type::signed_short; break;
        case token_kind::kw_long: intrinsic = intrinsic_type::signed_long; break;
        case token_kind::kw_float: intrinsic = intrinsic_type::float_type; break;
        case token_kind::kw_double: intrinsic = intrinsic_type::double_type; break;
        case token_kind::kw_char: intrinsic = intrinsic_type::char_type; break;
        default: return server_status::success;
        }
        type.type = G.intrinsic(intrinsic);
    }
    if (!type.type || type.type == G.intrinsic(intrinsic_type::void_type))
        return server_status::success;

    // No directive lies between the three *consecutive physical lexical*
    // tokens. Both potential macro identifiers are currently undefined.
    // Consume the existing effective stream once, and verify it has exactly
    // the physical spelling/positions required by the candidate.
    const auto first = *token;
    auto status = consume();
    if (!succeeded(status)) return status;
    const auto* name = current();
    if (name == nullptr || name->kind != token_kind::identifier ||
        name->file != field->file ||
        name->source_offset != field->name_offset ||
        name->source_length != field->name_length ||
        name->identifier != member_name)
        return fail(parser_v2_failure_kind::syntax,
            "Physical field model encountered a changed member token");
    status = consume();
    if (!succeeded(status)) return status;
    const auto* ending = current();
    if (ending == nullptr || ending->kind != token_kind::semicolon ||
        ending->file != field->file ||
        ending->source_offset != field->semicolon_offset)
        return fail(parser_v2_failure_kind::syntax,
            "Physical field model encountered a changed field terminator");
    status = consume();
    if (!succeeded(status)) return status;

    bool inserted = false;
    status = names.insert(members, member_name, inserted);
    if (!succeeded(status)) return status;
    if (!inserted)
        return fail_at(parser_v2_failure_kind::semantic,
            "Header Parser V2 data-member name is duplicated",
            field->file, {field->name_offset, field->name_length});
    try {
        members.push_back({member_name, type.type, access, {}});
        construction.push_back({});
    } catch (...) { return server_status::io_error; }
    const auto pending_it = pending.find(member_name.value());
    if (pending_it != pending.end()) {
        status = apply_record_operand(members.size() - 1, pending_it->second,
            members, construction, names, references);
        if (!succeeded(status)) return status;
        pending.erase(pending_it);
    }
    status = resolve_waiting_references(member_name, members, construction,
        references);
    if (!succeeded(status)) return status;
    status = resolve_waiting_nested(member_name, members, names, nested);
    if (!succeeded(status)) return status;
    handled = true;
    ++effective_direct_fields;
    return server_status::success;
}
#endif

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

// Opaque function definitions contribute no executable code or instance data.
// Validate delimiters while preserving comments/literals as indivisible tokens.
server_status header_parser_v2::parse_balanced_tokens() noexcept {
    std::vector<token_kind> closing;
    do {
        const auto kind = current() ? current()->kind : token_kind::invalid;
        if (kind == token_kind::l_paren || kind == token_kind::l_bracket || kind == token_kind::l_brace) {
            if (closing.size() >= parser_v2_scope_depth_limit)
                return fail(parser_v2_failure_kind::unsupported, "Function delimiter nesting exceeds supported depth");
            try {
                closing.push_back(kind == token_kind::l_paren ? token_kind::r_paren :
                    kind == token_kind::l_bracket ? token_kind::r_bracket : token_kind::r_brace);
            } catch (...) { return server_status::io_error; }
        } else if (kind == token_kind::r_paren || kind == token_kind::r_bracket || kind == token_kind::r_brace) {
            if (closing.empty() || closing.back() != kind)
                return fail(parser_v2_failure_kind::syntax, "Mismatched function delimiter");
            closing.pop_back();
        } else if (input.finished() || kind == token_kind::invalid)
            return fail(parser_v2_failure_kind::syntax, "Function declaration or body is not closed");
        const auto status = consume();
        if (!succeeded(status)) return status;
    } while (!closing.empty());
    return server_status::success;
}

// Ordinary method parameters and bodies are opaque; no Runtime method ABI.
server_status header_parser_v2::parse_method_tail(
    bool virtual_prefix,
    bool base_polymorphic,
    bool& declares_virtual,
    bool conversion,
    bool assignment_operator,
    bool subscript_operator,
    bool binary_operator,
    bool increment_operator) noexcept {

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
    bool postfix_parameter = false;
    bool postfix_name = false;
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
        if (increment_operator && depth == 1 && !at(token_kind::r_paren)) {
            if (!postfix_parameter && at(token_kind::kw_int)) postfix_parameter = true;
            else if (postfix_parameter && !postfix_name && at(token_kind::identifier)) postfix_name = true;
            else
                return fail(parser_v2_failure_kind::syntax,
                    "Increment/decrement operator requires no parameter or a single int parameter");
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
    bool special_declaration = false;
    if (at(token_kind::assign)) {
        special_declaration = true;
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
    if (at(token_kind::l_brace)) {
        if (special_declaration)
            return fail(parser_v2_failure_kind::syntax, "Special method declaration must end with ';'");
        return parse_balanced_tokens();
    }
    if (!at(token_kind::semicolon)) {
        return fail(parser_v2_failure_kind::unsupported, "Expected method body or ';'");
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
    if (at(token_kind::plus_plus) || at(token_kind::minus_minus)) {
        status = consume();
        if (!succeeded(status)) return status;
        return parse_method_tail(virtual_prefix, base_polymorphic,
            declares_virtual, false, false, false, false, true);
    }
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
            if (!G.named(type, record) || !G.find(record) ||
                G.find(record)->kind != graph_type_kind::record) {
                return fail_at(parser_v2_failure_kind::semantic,
                    "Nested constructor target record is invalid",
                    operation.file, operation.source);
            }
            const auto member_index = G.find_member(record, step.name);
            const auto* member = G.member(record, member_index);
            constructor_member_lookup inherited;
            if (member == nullptr) {
                if (!find_constructor_member(G, record, step.name, inherited) || !inherited.accessible)
                    return fail_at(parser_v2_failure_kind::semantic,
                        "Nested constructor field does not exist or is inaccessible",
                        operation.file, operation.source);
                member = &inherited.member;
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

    if (operation.referent) {
        string_id target;
        const auto status = strings.intern(spelling, target);
        if (!succeeded(status)) return status;
        try {
            templates->relative_bindings.push_back({
                nested.owner, target, operation.referent,
                find_static_object(constant_scope, operation.referent), operation.file, operation.source});
        }
        catch (...) { return server_status::io_error; }
        return server_status::success;
    }

    auto normalized = operation.value;
    type_handle enum_handle;
    const auto* enum_entry = G.named(type, enum_handle) ? G.find(enum_handle) : nullptr;
    if ((type.kind() != type_ref_kind::intrinsic &&
         !(enum_entry && (enum_entry->kind == graph_type_kind::enumeration || enum_entry->kind == graph_type_kind::nominal_scalar))) ||
        !normalize_enum_constant(G, type, normalized) ||
        !construction_compatible(G, type, normalized)) {
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
                previous.value = normalized;
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
                nested.completed[found->second].value = normalized;
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
        nested.completed.push_back({nested.owner, path, normalized});
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
    bool& default_constructor_seen,
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

    // Parameterized overloads are parsed as opaque declarations/definitions,
    // just like methods. They never supply automatic object defaults.
    const auto balanced = [&]() { return parse_balanced_tokens(); };
    bool parameterized = !at(token_kind::r_paren);
    bool parameter_token = false;
    if (at(token_kind::kw_void)) {
        status = consume();
        if (!succeeded(status)) return status;
        if (at(token_kind::comma))
            return fail(parser_v2_failure_kind::syntax, "void must be the only constructor parameter");
        parameterized = !at(token_kind::r_paren);
        parameter_token = parameterized;
    }
    while (!at(token_kind::r_paren)) {
        if (input.finished() || at(token_kind::semicolon) || at(token_kind::r_brace) || at(token_kind::r_bracket))
            return fail(parser_v2_failure_kind::syntax, "Constructor parameter list is not closed");
        if (at(token_kind::comma)) {
            if (!parameter_token) return fail(parser_v2_failure_kind::syntax, "Expected constructor parameter");
            parameter_token = false;
            status = consume();
        } else {
            parameter_token = true;
            status = (at(token_kind::l_paren) || at(token_kind::l_bracket) || at(token_kind::l_brace))
                ? balanced() : consume();
        }
        if (!succeeded(status)) return status;
    }
    if (parameterized && !parameter_token)
        return fail(parser_v2_failure_kind::syntax, "Expected constructor parameter after comma");
    if (!parameterized) {
        if (default_constructor_seen)
            return fail(parser_v2_failure_kind::semantic, "Header Parser V2 supports one managed default constructor per record");
        default_constructor_seen = true;
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
        if (at(token_kind::l_paren)) {
            status = balanced();
            if (!succeeded(status)) return status;
        }
    }

    if (parameterized) {
        if (at(token_kind::semicolon)) return consume();
        if (at(token_kind::assign)) {
            status = consume();
            if (!succeeded(status)) return status;
            if (!at(token_kind::kw_default) && !at(token_kind::kw_delete))
                return fail(parser_v2_failure_kind::syntax, "Constructor declaration requires '= default' or '= delete'");
            status = consume();
            if (!succeeded(status)) return status;
            return expect(token_kind::semicolon, "Expected ';' after constructor declaration");
        }
        if (at(token_kind::colon)) {
            status = consume();
            if (!succeeded(status)) return status;
            for (;;) {
                if (!at(token_kind::identifier))
                    return fail(parser_v2_failure_kind::syntax, "Expected constructor initializer name");
                status = consume();
                if (!succeeded(status)) return status;
                while (at(token_kind::scope)) {
                    status = consume();
                    if (!succeeded(status)) return status;
                    status = expect(token_kind::identifier, "Expected qualified constructor initializer name");
                    if (!succeeded(status)) return status;
                }
                if (!at(token_kind::l_paren) && !at(token_kind::l_brace))
                    return fail(parser_v2_failure_kind::syntax, "Expected constructor initializer arguments");
                status = balanced();
                if (!succeeded(status)) return status;
                if (!at(token_kind::comma)) break;
                status = consume();
                if (!succeeded(status)) return status;
            }
        }
        if (!at(token_kind::l_brace))
            return fail(parser_v2_failure_kind::syntax, "Expected constructor body or declaration terminator");
        return balanced();
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
            auto deferred = value;
            deferred.target_name = target;
            const auto previous = pending.find(target.value());
            if (previous != pending.end()) deferred.member_initializer |= previous->second.member_initializer;
            pending.insert_or_assign(target.value(), deferred);
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
            value.member_initializer = true;

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

        value.body_assignment = true;

        if (path.empty() && at(token_kind::ampersand)) {
            status = consume();
            if (!succeeded(status)) return status;
            status = parse_relative_aggregate_referent(value.relative_referent);
            if (!succeeded(status)) return status;
            status = expect(token_kind::semicolon, "Expected ';' after constructor address binding");
            if (!succeeded(status)) return status;
            try {
                templates->relative_bindings.push_back({
                    nested.owner, target_name, value.relative_referent, {}, target_file, target_source, true});
            } catch (...) { return server_status::io_error; }
            continue;
        }

        status = !path.empty() && at(token_kind::kw_this)
            ? parse_relative_aggregate_referent(value.relative_referent)
            : parse_construction_operand(value, true);
        if (!succeeded(status)) return status;
        if (!path.empty() && value.reference_name) {
            status = parse_relative_aggregate_referent(value.relative_referent, value.reference_name);
            if (!succeeded(status)) return status;
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
            operation.referent = value.relative_referent;
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

server_status header_parser_v2::parse_record_attributes(string_id& index_member,
    std::int64_t& index_origin, string_id* index_parameter) noexcept {
    while (at(token_kind::l_bracket)) {
        auto status = consume();
        if (!succeeded(status)) return status;
        status = expect(token_kind::l_bracket, "Expected '[[' for record attribute");
        if (!succeeded(status)) return status;
        const auto spelling = [&]() {
            const auto* token = current();
            return token && token->kind == token_kind::identifier
                ? input.semantic_strings().get(token->identifier) : std::string_view{};
        };
        if (spelling() != "cw")
            return fail(parser_v2_failure_kind::unsupported, "Unsupported record attribute namespace");
        status = consume();
        if (!succeeded(status)) return status;
        status = expect(token_kind::scope, "Expected '::' in record attribute");
        if (!succeeded(status)) return status;
        if (spelling() != "index")
            return fail(parser_v2_failure_kind::unsupported, "Unsupported cw record attribute");
        if (index_member)
            return fail(parser_v2_failure_kind::semantic, "Duplicate cw::index attribute");
        status = consume();
        if (!succeeded(status)) return status;
        status = expect(token_kind::l_paren, "Expected '(' after cw::index");
        if (!succeeded(status)) return status;
        if (at(token_kind::identifier)) {
            const auto first = current()->identifier;
            status = consume();
            if (!succeeded(status)) return status;
            if (at(token_kind::r_paren)) {
                index_member = first;
            } else if (index_parameter != nullptr) {
                *index_parameter = first;
            } else {
                return fail(parser_v2_failure_kind::semantic,
                    "cw::index lower bound must be an integer or an integer template parameter");
            }
        } else {
            status = parse_integer_expression(index_origin);
            if (!succeeded(status)) return status;
        }
        if (!index_member) {
            status = expect(token_kind::comma, "Expected ',' before cw::index backing member");
            if (!succeeded(status)) return status;
            if (!at(token_kind::identifier))
                return fail(parser_v2_failure_kind::syntax, "Expected backing member name in cw::index");
            index_member = current()->identifier;
            status = consume();
            if (!succeeded(status)) return status;
        }
        status = expect(token_kind::r_paren, "Expected ')' after cw::index backing member");
        if (!succeeded(status)) return status;
        status = expect(token_kind::r_bracket, "Expected ']]' after cw::index");
        if (!succeeded(status)) return status;
        status = expect(token_kind::r_bracket, "Expected ']]' after cw::index");
        if (!succeeded(status)) return status;
    }
    return server_status::success;
}

server_status header_parser_v2::parse_record(
    identity_ref scope, string_id index_member, std::int64_t index_origin) noexcept {

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

    const auto record_file = key->file;
    const auto kind =
        record_kind(
            key->kind);

    auto status =
        consume();

    if (!succeeded(status)) {
        return status;
    }

    status = parse_record_attributes(index_member, index_origin);
    if (!succeeded(status)) return status;

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

    if (templates->array_aliases.contains(identity.value()))
        return fail(parser_v2_failure_kind::semantic, "Record conflicts with an array alias");
    for (const auto& definition : templates->records)
        if (definition.identity == identity)
            return fail(parser_v2_failure_kind::semantic, "Concrete record conflicts with a template name");
    const auto previous_scope = constant_scope;
    struct restore { identity_ref& slot; identity_ref value; ~restore() { slot = value; } } restore_scope{constant_scope, previous_scope};
    constant_scope = identity;

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

            resolved_type base;
            status = parse_type(scope, base);
            if (!succeeded(status)) return status;
            type_handle base_type;
            const auto* base_entry = G.named(base.type, base_type) ? G.find(base_type) : nullptr;
            if (!base_entry || !base_entry->defined() || base_entry->kind != graph_type_kind::record ||
                base_entry->record_kind == graph_record_kind::union_type) {
                return fail(parser_v2_failure_kind::semantic,
                    "Base class must name a complete non-union record type");
            }
            const auto base_identity = G.identity(base_type);

            if (provenance != nullptr) {
                const auto dependency = record_dependency(base_identity);
                if (!succeeded(dependency)) return dependency;
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
        if (provenance != nullptr) {
            const auto recorded = record_contribution(
                record_file, source_data_ref::type(identity, false));
            if (!succeeded(recorded)) return recorded;
        }
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
    if (publication_stats != nullptr)
        ++publication_stats->record_declarations;

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
    struct restore_enum_lookup {
        const std::vector<member_record>*& members_slot;
        const record_member_name_set*& names_slot;
        const std::vector<member_record>* previous_members;
        const record_member_name_set* previous_names;
        ~restore_enum_lookup() { members_slot = previous_members; names_slot = previous_names; }
    } restore_lookup{enum_lookup_members, enum_lookup_names, enum_lookup_members, enum_lookup_names};
    enum_lookup_members = &members;
    enum_lookup_names = &names;

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

        if (at(token_kind::kw_enum)) {
            status = parse_enum(identity);
            if (!succeeded(status)) return status;
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
                status =
                    parse_constructor(
                        record_name,
                        constructor_seen,
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

            // OLD parse_type_specifier records this dependency even when
            // the named type is the enclosing record itself.
            if (provenance != nullptr) {
                const auto dependency =
                    record_dependency(identity);
                if (!succeeded(dependency)) {
                    return dependency;
                }
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

#ifdef CW_HEADER_MODEL_EFFECTIVE_V2
        if (!virtual_prefix && effective_model != nullptr) {
            bool handled = false;
            const auto fast = try_effective_field(
                scope, access, members, construction, pending,
                references, nested, names, handled);
            if (!succeeded(fast)) return fast;
            if (handled) continue;
        }
#endif
        status =
            parse_member(
                identity,
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

    // Forward direct-field declarations have now consumed their operands.
    // Records with bases resolve remaining inherited targets after definition.
    if (!pending.empty() && bases.empty()) {
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

    // Class member lookup wins even for fields declared later. Resolve only
    // remaining names against this semantic root's Header static storage.
    for (auto it=references.waiting.begin(); it!=references.waiting.end();) {
        const auto waiting=it->second;
        const auto revision=references.generations.find(waiting.target);
        if (revision!=references.generations.end() && revision->second!=waiting.generation) {
            it=references.waiting.erase(it); continue;
        }
        const auto object=find_static_object(identity,waiting.name);
        const auto* entry=G.find(G.find_object(object));
        if (!entry) { ++it; continue; }
        derived_type_record target_reference;
        if (waiting.target>=members.size() || !G.derived(members[waiting.target].type,target_reference) ||
            (target_reference.kind!=derived_type_kind::lvalue_reference && target_reference.kind!=derived_type_kind::rvalue_reference) ||
            target_reference.child!=entry->type)
            return fail_at(parser_v2_failure_kind::semantic,"Header static reference binding type mismatch",waiting.file,waiting.source);
        construction[waiting.target]=construction_value::object_binding(object.value());
        if (provenance) { status=record_dependency(object); if (!succeeded(status)) return status; }
        it=references.waiting.erase(it);
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

    // Commit this declaration before advancing through following includes or
    // EOF: the preprocessor's file-completion event closes its replay tape.
    if (!at(token_kind::semicolon)) {
        return fail(parser_v2_failure_kind::syntax,
            "Header Parser V2 expected ';' after record definition");
    }

    std::uint16_t index_slot = 0;
    if (index_member) {
        const auto index = names.find(members, index_member);
        derived_type_record array;
        if (index >= members.size() || index >= 0x3fffu ||
            !G.derived(members[index].type, array) || array.kind != derived_type_kind::bounded_array)
            return fail(parser_v2_failure_kind::semantic,
                "cw::index must name a bounded array member of the record");
        if (index_origin < array_origin_min || index_origin > array_origin_max)
            return fail(parser_v2_failure_kind::semantic, "cw::index lower bound exceeds the supported range");
        set_array_origin(members[index].reserved, static_cast<std::int32_t>(index_origin));
        index_slot = static_cast<std::uint16_t>(index + 1);
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
                declares_virtual, index_slot);
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

    // Direct fields (including declarations after the constructor) have already
    // consumed their pending operands. Remaining body targets may be inherited.
    if (!pending.empty()) for (const auto& item : pending) {
        const auto& operation = item.second;
        constructor_member_lookup target;
        if (!operation.body_assignment || operation.member_initializer || operation.reference_name ||
            !find_constructor_member(G, handle, operation.target_name, target) ||
            !target.accessible) {
            return fail_at(parser_v2_failure_kind::semantic,
                "Header Parser V2 constructor target is not an accessible unambiguous inherited field",
                operation.file, operation.source);
        }
        auto value = operation.value;
        type_handle scalar;
        const auto* entry = G.named(target.member.type, scalar) ? G.find(scalar) : nullptr;
        if ((target.member.type.kind() != type_ref_kind::intrinsic &&
             !(entry && (entry->kind == graph_type_kind::enumeration || entry->kind == graph_type_kind::nominal_scalar))) ||
            !normalize_enum_constant(G, target.member.type, value) ||
            !construction_compatible(G, target.member.type, value) ||
            value.kind > construction_kind::real) {
            return fail_at(parser_v2_failure_kind::unsupported,
                "Inherited constructor assignment requires a scalar constant",
                operation.file, operation.source);
        }
        status = G.constructor_defaults.add({identity, operation.target_name, value});
        if (!succeeded(status)) return status;
    }

    if (provenance != nullptr) {
        const auto recorded = record_contribution(
            record_file, source_data_ref::type(identity, true));
        if (!succeeded(recorded)) return recorded;
    }

    if (publication_stats != nullptr) {
        ++publication_stats->record_definitions;
        publication_stats->data_members += members.size();
    }

    // Cold sparse defaults only, emitted with the existing Graph ABI.
    for (const auto& item : nested.completed) {
        const auto added = G.constructor_defaults.add(item);
        if (!succeeded(added)) {
            return fail(parser_v2_failure_kind::semantic,
                "Nested constructor default conflicts with Graph");
        }
    }
    return consume();
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

    if (!at(token_kind::semicolon))
        return fail(parser_v2_failure_kind::syntax, "Expected ';' after typedef");

    identity_ref identity;
    status = identities.resolve(
        scope, alias_name, identity_kind::type, identity);
    if (!succeeded(status) || !identity) {
        return fail_at(parser_v2_failure_kind::semantic,
            "Typedef identity conflicts with existing semantic identity",
            alias_file, alias_source);
    }

    type_handle handle;
    if (templates->array_aliases.contains(identity.value()))
        return fail(parser_v2_failure_kind::semantic, "Typedef conflicts with an array alias");
    status = G.define_intrinsic_alias(identity, intrinsic, handle);
    if (!succeeded(status) || !handle) {
        return fail_at(parser_v2_failure_kind::semantic,
            "Typedef conflicts with existing type in G",
            alias_file, alias_source);
    }
    if (provenance != nullptr) {
        const auto recorded = record_contribution(
            alias_file, source_data_ref::type(identity, true));
        if (!succeeded(recorded)) return recorded;
    }
    if (publication_stats != nullptr)
        ++publication_stats->intrinsic_aliases;
    return consume();
}

server_status header_parser_v2::parse_enum_constant(construction_value& output) noexcept {
    if (!at(token_kind::identifier))
        return fail(parser_v2_failure_kind::syntax, "Expected enum constant");
    const auto name = current()->identifier;
    auto owner = find_type_identity(constant_scope, name);
    auto namespace_scope = constant_scope;
    identity_ref ns;
    while (namespace_scope) {
        ns = identities.find(namespace_scope, name, identity_kind::namespace_scope);
        if (ns || namespace_scope == identities.root()) break;
        identity_record record;
        if (!identities.record(namespace_scope, record)) break;
        namespace_scope = record.parent;
    }
    const bool unqualified = enum_constant(G, identities, constant_scope, name, output);
    auto status = consume();
    if (!succeeded(status)) return status;
    if (!at(token_kind::scope)) {
        if (!unqualified) return fail(parser_v2_failure_kind::semantic, "Unknown enum constant");
        return record_dependency(identity_ref::from_raw(output.operand));
    }
    for (;;) {
        status = consume();
        if (!succeeded(status)) return status;
        if (!at(token_kind::identifier))
            return fail(parser_v2_failure_kind::syntax, "Expected qualified enum name");
        const auto component = current()->identifier;
        if (owner && enum_constant(G, identities, owner, component, output, true)) {
            status = consume();
            if (!succeeded(status)) return status;
            return record_dependency(owner);
        }
        const auto parent = ns ? ns : owner;
        if (!parent) return fail(parser_v2_failure_kind::semantic, "Unknown qualified enum constant");
        owner = identities.find(parent, component, identity_kind::type);
        ns = identities.find(parent, component, identity_kind::namespace_scope);
        status = consume();
        if (!succeeded(status)) return status;
        if (!at(token_kind::scope))
            return fail(parser_v2_failure_kind::semantic, "Expected enum value after qualified type");
    }
}

server_status header_parser_v2::parse_enum(identity_ref scope) noexcept {
    const auto file = current()->file;
    auto status = consume();
    if (!succeeded(status)) return status;
    bool scoped = false;
    if (at(token_kind::kw_class) || at(token_kind::kw_struct)) {
        scoped = true;
        status = consume();
        if (!succeeded(status)) return status;
    }
    if (!at(token_kind::identifier))
        return fail(parser_v2_failure_kind::unsupported, "Named enum required; anonymous enums are unsupported");
    identity_ref identity;
    status = identities.resolve(scope, current()->identifier, identity_kind::type, identity);
    if (!succeeded(status)) return fail(parser_v2_failure_kind::semantic, "Enum identity conflicts with an existing declaration");
    status = consume();
    if (!succeeded(status)) return status;
    intrinsic_type underlying = intrinsic_type::signed_int;
    bool fixed = scoped;
    if (at(token_kind::colon)) {
        fixed = true;
        status = consume();
        if (!succeeded(status)) return status;
        resolved_type parsed;
        status = parse_type(scope, parsed);
        if (!succeeded(status)) return status;
        if (!G.intrinsic(parsed.type, underlying) ||
            !construction_semantics_detail::integral_intrinsic(underlying))
            return fail(parser_v2_failure_kind::semantic, "Enum underlying type must be integral");
    }
    const bool body = at(token_kind::l_brace);
    if (!body && !fixed)
        return fail(parser_v2_failure_kind::semantic, "Unscoped enum forward declaration requires an underlying type");
    std::vector<member_record> names;
    std::vector<construction_value> values;
    construction_value next = construction_value::constant(construction_kind::unsigned_integer, 0);
    bool next_overflow = false;
    if (body) {
        status = consume();
        if (!succeeded(status)) return status;
        while (!at(token_kind::r_brace) && !input.finished()) {
            if (!at(token_kind::identifier))
                return fail(parser_v2_failure_kind::syntax, "Expected enumerator name");
            const auto name = current()->identifier;
            for (const auto& entry : names)
                if (entry.name == name)
                    return fail(parser_v2_failure_kind::semantic, "Duplicate enumerator name");
            if (!scoped) {
                for (const auto handle : G.enum_types()) {
                    identity_record record;
                    const auto* type = G.find(handle);
                    if (G.identity(handle) != identity && type && !(type->flags & graph_enum_scoped) &&
                        identities.record(G.identity(handle), record) && record.parent == scope &&
                        G.member(handle, G.find_member(handle, name)))
                        return fail(parser_v2_failure_kind::semantic, "Unscoped enumerator conflicts with an existing name");
                }
                if (identities.find(scope, name, identity_kind::type) ||
                    identities.find(scope, name, identity_kind::object) ||
                    identities.find(scope, name, identity_kind::namespace_scope))
                    return fail(parser_v2_failure_kind::semantic, "Unscoped enumerator conflicts with an existing name");
            }
            status = consume();
            if (!succeeded(status)) return status;
            auto value = next;
            if (at(token_kind::assign)) {
                status = consume();
                if (!succeeded(status)) return status;
                // Same literal subset as V3. General constexpr expressions are
                // deliberately rejected rather than partially evaluated.
                if (!at(token_kind::plus) && !at(token_kind::minus) && !at(token_kind::pp_number))
                    return fail(parser_v2_failure_kind::unsupported, "Enumerator requires a decimal integer literal");
                const bool negative = at(token_kind::minus);
                if (at(token_kind::plus) || negative) {
                    status = consume();
                    if (!succeeded(status)) return status;
                }
                const auto* token = current();
                if (!token || token->kind != token_kind::pp_number ||
                    token->number.kind != prepared_number_kind_v2::unsigned_integer)
                    return fail(parser_v2_failure_kind::unsupported, "Enumerator requires a decimal integer literal");
                const auto magnitude = token->number.bits;
                if (negative && magnitude > (std::uint64_t{1} << 63))
                    return fail(parser_v2_failure_kind::semantic, "Enumerator overflows signed 64-bit range");
                value = construction_value::constant(negative ? construction_kind::signed_integer :
                    construction_kind::unsigned_integer, negative ? std::uint64_t{0} - magnitude : magnitude);
                status = consume();
                if (!succeeded(status)) return status;
            } else if (next_overflow) {
                return fail(parser_v2_failure_kind::semantic, "Implicit enumerator overflows 64-bit range");
            }
            if (fixed && !enum_value_fits(underlying, value))
                return fail(parser_v2_failure_kind::semantic, "Enumerator is outside its underlying type range");
            try {
                names.push_back({name, G.intrinsic(underlying)});
                values.push_back(value);
            } catch (...) { return server_status::io_error; }
            next_overflow = value.kind == construction_kind::unsigned_integer &&
                value.bits() == (std::numeric_limits<std::uint64_t>::max)();
            next = construction_value::constant(
                value.kind == construction_kind::signed_integer && value.bits() != (std::numeric_limits<std::uint64_t>::max)()
                    ? construction_kind::signed_integer : construction_kind::unsigned_integer,
                value.bits() + 1);
            if (at(token_kind::comma)) {
                status = consume();
                if (!succeeded(status)) return status;
                continue;
            }
            if (!at(token_kind::r_brace))
                return fail(parser_v2_failure_kind::syntax, "Expected ',' or '}' after enumerator");
        }
        status = expect(token_kind::r_brace, "Expected '}' after enum");
        if (!succeeded(status)) return status;
    }
    if (!at(token_kind::semicolon))
        return fail(parser_v2_failure_kind::syntax, "Expected ';' after enum declaration");
    if (!fixed) {
        for (const auto candidate : {intrinsic_type::signed_int, intrinsic_type::unsigned_int,
            intrinsic_type::signed_long_long, intrinsic_type::unsigned_long_long}) {
            bool fits = true;
            for (auto value : values) fits &= enum_value_fits(candidate, value);
            if (fits) { underlying = candidate; break; }
            if (candidate == intrinsic_type::unsigned_long_long)
                return fail(parser_v2_failure_kind::semantic, "No integral type can represent all enumerators");
        }
        for (auto& name : names) name.type = G.intrinsic(underlying);
    }
    type_handle handle;
    if (templates->array_aliases.contains(identity.value()))
        return fail(parser_v2_failure_kind::semantic, "Enum conflicts with an array alias");
    status = G.define_enum(identity, underlying, scoped, fixed, body, names, values, handle);
    if (!succeeded(status)) return fail(parser_v2_failure_kind::semantic, "Enum declaration conflicts with an existing type");
    if (provenance) {
        status = record_contribution(file, source_data_ref::type(identity, body));
        if (!succeeded(status)) return status;
    }
    return consume();
}

server_status header_parser_v2::parse_scope(
    identity_ref scope,
    bool closing_brace,
    std::size_t depth) noexcept {
    const auto previous_scope = constant_scope;
    struct restore { identity_ref& slot; identity_ref value; ~restore() { slot = value; } } restore_scope{constant_scope, previous_scope};
    constant_scope = scope;

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

#ifdef CW_HEADER_MODEL_V2
        if (model != nullptr) {
            const auto* op = model->at(model_cursor);
            const auto* token = current();
            if (op != nullptr && token != nullptr &&
                op->file == token->file &&
                op->offset == token->source_offset &&
                op->kind == token->kind) {
                ++model_cursor;
                ++model_dispatches;
                server_status model_result = server_status::project_configuration_invalid;
                if (op->kind == token_kind::kw_namespace)
                    model_result = parse_namespace(scope, depth);
                else if (op->kind == token_kind::kw_typedef)
                    model_result = parse_typedef(scope);
                else
                    model_result = parse_record(scope);
                if (!succeeded(model_result)) return model_result;
                continue;
            }
            // For any syntactic mismatch, the ORIGINAL semantic parser retains
            // complete responsibility for diagnostics and Graph publication.
            model = nullptr;
        }
#endif
        if (at(token_kind::kw_enum)) {
            const auto status = parse_enum(scope);
            if (!succeeded(status)) return status;
            continue;
        }
        if (at(token_kind::kw_template)) {
            const auto status = parse_template(scope);
            if (!succeeded(status)) return status;
            continue;
        }
        if (at(token_kind::kw_using)) {
            const auto status = parse_using(scope);
            if (!succeeded(status)) return status;
            continue;
        }
        if (at(token_kind::semicolon)) {
            const auto status = consume();
            if (!succeeded(status)) return status;
            continue;
        }
        if (at(token_kind::kw_static)) {
            const auto status = parse_static_object(scope);
            if (!succeeded(status)) return status;
            continue;
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

#include "header_templates_v2.inc"

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
