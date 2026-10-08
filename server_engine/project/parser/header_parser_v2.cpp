#include "header_parser_v2.hpp"

#include "../graph/construction_semantics.hpp"

#include <limits>

namespace cw::server {

header_parser_v2::header_parser_v2(
    semantic_preprocessor_v2& input_value,
    identity_space& identities_value,
    graph& graph_value,
    parser_v2_failure* failure_value) noexcept
    : input(input_value),
      identities(identities_value),
      G(graph_value),
      failure(failure_value) {
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

server_status header_parser_v2::parse_type(
    identity_ref scope,
    resolved_type& output) noexcept {

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

    if (at(token_kind::kw_int) ||
        at(token_kind::kw_void)) {
        const auto intrinsic =
            at(token_kind::kw_void)
            ? intrinsic_type::void_type
            : intrinsic_type::signed_int;

        output.type =
            G.intrinsic(intrinsic);

        if (!output.type) {
            return fail(
                parser_v2_failure_kind::semantic,
                "Header Parser V2 could not create intrinsic int type");
        }

        const auto status =
            consume();

        if (!succeeded(status)) {
            return status;
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

        const auto identity =
            find_type_identity(
                scope,
                token->identifier);

        if (!identity) {
            return fail(
                parser_v2_failure_kind::semantic,
                "Header Parser V2 named type is unknown");
        }

        const auto resolved =
            resolve_named_type(
                identity,
                output);

        if (!succeeded(resolved)) {
            return resolved;
        }

        const auto status =
            consume();

        if (!succeeded(status)) {
            return status;
        }
    }

    return parse_type_tail(
        output,
        const_qualified,
        volatile_qualified);
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
    string_id name) noexcept {

    const auto mask =
        slots.size() - 1;

    auto position =
        static_cast<std::size_t>(
            hash(name)) &
        mask;

    while (slots[position]) {
        position =
            (position + 1) &
            mask;
    }

    slots[position] =
        name;
}

server_status
header_parser_v2::record_member_name_set::rebuild(
    std::span<const member_record> existing,
    std::size_t required) noexcept {

    if (required == 0) {
        return server_status::success;
    }

    constexpr std::size_t
        minimum_capacity = 128;

    if (required >
        (std::numeric_limits<std::size_t>::max)() / 2) {

        return server_status::io_error;
    }

    const auto minimum =
        required * 2;

    auto capacity =
        minimum_capacity;

    while (capacity < minimum) {
        if (capacity >
            (std::numeric_limits<std::size_t>::max)() / 2) {

            return server_status::io_error;
        }

        capacity *= 2;
    }

    try {
        std::vector<string_id>
            candidate(capacity);

        slots.swap(
            candidate);

        for (const auto& member :
             existing) {

            insert_slot(
                member.name);
        }

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status
header_parser_v2::record_member_name_set::insert(
    std::span<const member_record> existing,
    string_id name,
    bool& inserted) noexcept {

    inserted = false;

    if (!name) {
        return server_status::
            project_configuration_invalid;
    }

    if (slots.empty() &&
        existing.size() <
            header_parser_v2::
                record_member_linear_limit) {

        for (const auto& member :
             existing) {

            if (member.name == name) {
                return server_status::success;
            }
        }

        inserted = true;
        return server_status::success;
    }

    const auto required =
        existing.size() + 1;

    if (slots.empty() ||
        required >
            slots.size() / 2) {

        const auto rebuilt =
            rebuild(
                existing,
                required);

        if (!succeeded(rebuilt)) {
            return rebuilt;
        }
    }

    const auto mask =
        slots.size() - 1;

    auto position =
        static_cast<std::size_t>(
            hash(name)) &
        mask;

    for (std::size_t probe = 0;
         probe < slots.size();
         ++probe) {

        const auto existing_name =
            slots[position];

        if (!existing_name) {
            slots[position] =
                name;

            inserted = true;
            return server_status::success;
        }

        if (existing_name ==
            name) {

            return server_status::success;
        }

        position =
            (position + 1) &
            mask;
    }

    return server_status::io_error;
}

server_status header_parser_v2::parse_array_suffix(
    resolved_type& output) noexcept {

    std::vector<std::uint64_t>
        bounds;

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
        return fail(
            parser_v2_failure_kind::semantic,
            "Header Parser V2 arrays of references are invalid");
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

    if (at(token_kind::minus)) {
        negative = true;

        const auto status =
            consume();

        if (!succeeded(status)) {
            return status;
        }
    }

    if (at(token_kind::kw_true) ||
        at(token_kind::kw_false)) {

        if (negative) {
            return fail(
                parser_v2_failure_kind::syntax,
                "Header Parser V2 boolean initializer cannot be negated");
        }

        output =
            construction_value::constant(
                construction_kind::
                    unsigned_integer,
                at(token_kind::kw_true)
                    ? 1u
                    : 0u);

        return consume();
    }

    const auto* token =
        current();

    if (token == nullptr ||
        token->kind !=
            token_kind::pp_number ||
        token->number.kind !=
            prepared_number_kind_v2::
                unsigned_integer) {

        return fail(
            parser_v2_failure_kind::unsupported,
            "Header Parser V2 initializer requires a plain decimal integer or boolean literal");
    }

    const auto magnitude =
        token->number.bits;

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

server_status header_parser_v2::parse_member_initializer(
    type_ref target,
    construction_value& output) noexcept {

    output = {};

    if (!at(token_kind::assign) &&
        !at(token_kind::l_brace)) {

        return server_status::success;
    }

    auto status =
        server_status::success;

    if (at(token_kind::assign)) {
        status =
            consume();

        if (!succeeded(status)) {
            return status;
        }
    }

    if (at(token_kind::l_brace)) {
        status =
            consume();

        if (!succeeded(status)) {
            return status;
        }

        if (!at(token_kind::r_brace)) {
            status =
                parse_scalar_constant(
                    output);

            if (!succeeded(status)) {
                return status;
            }
        }

        status =
            expect(
                token_kind::r_brace,
                "Header Parser V2 expected '}' after member initializer");

        if (!succeeded(status)) {
            return status;
        }
    }
    else {
        status =
            parse_scalar_constant(
                output);

        if (!succeeded(status)) {
            return status;
        }
    }

    if (!construction_compatible(
            G,
            target,
            output)) {

        return fail(
            parser_v2_failure_kind::semantic,
            "Header Parser V2 member initializer is incompatible with its type");
    }

    return server_status::success;
}

server_status header_parser_v2::parse_member_declarator(
    resolved_type type,
    graph_member_access access,
    std::vector<member_record>& members,
    std::vector<construction_value>& construction,
    record_member_name_set& names,
    bool virtual_prefix,
    bool base_polymorphic,
    bool& declares_virtual) noexcept {

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

    auto status =
        consume();

    if (!succeeded(status)) {
        return status;
    }

    // HEADER-V2-VIRTUAL-01: methods have no stored Graph member.
    // Consume their declaration before the record-local field name set.
    if (at(token_kind::l_paren)) {
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

    bool inserted = false;

    status =
        names.insert(
            members,
            member_name,
            inserted);

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

    construction_value initial;

    status =
        parse_member_initializer(
            type.type,
            initial);

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

    try {
        members.push_back({
            member_name,
            type.type,
            access,
            {},
        });

        construction.push_back(
            initial);
    }
    catch (...) {
        return server_status::io_error;
    }

    return server_status::success;
}

server_status header_parser_v2::parse_member(
    identity_ref scope,
    graph_member_access access,
    std::vector<member_record>& members,
    std::vector<construction_value>& construction,
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

    return parse_member_declarator(
        type,
        access,
        members,
        construction,
        names,
        virtual_prefix,
        base_polymorphic,
        declares_virtual);
}

// HEADER-V2-VIRTUAL-01: declaration-only methods, zero argument subset.
// Method declarations affect polymorphic ABI but do not create Graph members.
server_status header_parser_v2::parse_method_tail(
    bool virtual_prefix,
    bool base_polymorphic,
    bool& declares_virtual) noexcept {

    auto status = expect(
        token_kind::l_paren,
        "Header Parser V2 expected '(' after method name");
    if (!succeeded(status)) return status;

    // A sole void is equivalent to an empty C++ parameter list.
    if (at(token_kind::kw_void)) {
        status = consume();
        if (!succeeded(status)) return status;
    }
    if (!at(token_kind::r_paren)) {
        return fail(
            parser_v2_failure_kind::unsupported,
            "Header Parser V2 method parameters are not yet supported");
    }
    status = consume();
    if (!succeeded(status)) return status;

    while (at(token_kind::kw_const) ||
           at(token_kind::kw_volatile) ||
           at(token_kind::kw_noexcept)) {
        status = consume();
        if (!succeeded(status)) return status;
    }

    bool pure = false;
    if (at(token_kind::assign)) {
        status = consume();
        if (!succeeded(status)) return status;
        const auto* token = current();
        if (token != nullptr &&
            token->kind == token_kind::pp_number &&
            token->number.kind == prepared_number_kind_v2::unsigned_integer &&
            token->number.bits == 0) {
            pure = true;
        }
        else if (!at(token_kind::kw_default) &&
                 !at(token_kind::kw_delete)) {
            return fail(
                parser_v2_failure_kind::unsupported,
                "Method declaration supports only '= 0', '= default', or '= delete'");
        }
        status = consume();
        if (!succeeded(status)) return status;
    }

    if (pure && !virtual_prefix && !base_polymorphic) {
        return fail(
            parser_v2_failure_kind::semantic,
            "Pure method requires a polymorphic base or explicit virtual");
    }
    if (pure || virtual_prefix) declares_virtual = true;

    if (!at(token_kind::semicolon)) {
        return fail(
            parser_v2_failure_kind::unsupported,
            "CXX-CLASS-ABI-V1 stores method declarations only; method bodies are not implemented");
    }
    return consume();
}

server_status header_parser_v2::parse_constructor(
    string_id record_name,
    std::vector<constructor_operation>& operations) noexcept {

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

    const auto append =
        [&](string_id target,
            construction_value value)
            -> server_status {

            try {
                operations.push_back({
                    target,
                    value,
                });

                return server_status::success;
            }
            catch (...) {
                return server_status::io_error;
            }
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

            construction_value value;

            if (!at(close)) {
                status =
                    parse_scalar_constant(
                        value);

                if (!succeeded(status)) {
                    return status;
                }
            }

            status =
                expect(
                    close,
                    "Header Parser V2 expected end of constructor member initializer");

            if (!succeeded(status)) {
                return status;
            }

            status =
                append(
                    target_name,
                    value);

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

        status =
            consume();

        if (!succeeded(status)) {
            return status;
        }

        status =
            expect(
                token_kind::assign,
                "Header Parser V2 expected '=' in constructor field assignment");

        if (!succeeded(status)) {
            return status;
        }

        construction_value value;

        status =
            parse_scalar_constant(
                value);

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

        status =
            append(
                target_name,
                value);

        if (!succeeded(status)) {
            return status;
        }
    }

    return consume();
}

server_status header_parser_v2::apply_constructor_operations(
    std::span<const member_record> members,
    std::vector<construction_value>& construction,
    std::span<const constructor_operation> operations) noexcept {

    if (construction.size() !=
        members.size()) {

        return server_status::
            project_configuration_invalid;
    }

    for (const auto& operation :
         operations) {

        std::size_t index = 0;

        for (;
             index < members.size();
             ++index) {

            if (members[index].name ==
                operation.target) {

                break;
            }
        }

        if (index ==
            members.size()) {

            return fail(
                parser_v2_failure_kind::semantic,
                "Header Parser V2 constructor target is not a field of this record");
        }

        if (!construction_compatible(
                G,
                members[index].type,
                operation.value)) {

            return fail(
                parser_v2_failure_kind::semantic,
                "Header Parser V2 constructor initializer is incompatible with target field");
        }

        construction[index] =
            operation.value;
    }

    return server_status::success;
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

    std::vector<constructor_operation>
        constructor_operations;

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
                        constructor_operations);

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

            status =
                parse_member_declarator(
                    self_type,
                    access,
                    members,
                    construction,
                    names,
                    virtual_prefix,
                    base_polymorphic,
                    declares_virtual);

            if (!succeeded(status)) {
                return status;
            }

            continue;
        }

        status =
            parse_member(
                scope,
                access,
                members,
                construction,
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

    status =
        apply_constructor_operations(
            members,
            construction,
            constructor_operations);

    if (!succeeded(status)) {
        return status;
    }

    status =
        expect(
            token_kind::semicolon,
            "Header Parser V2 expected ';' after record definition");

    if (!succeeded(status)) {
        return status;
    }

    status =
        G.define_resolved_record(
            handle,
            kind,
            members,
            construction,
            bases,
            declares_virtual);

    if (!succeeded(status)) {
        return fail(
            parser_v2_failure_kind::semantic,
            "Header Parser V2 record definition conflicts with G");
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
