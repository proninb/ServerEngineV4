#include "parser.hpp"

#include "../frontend/semantic_input.hpp"
#include "../graph/construction_semantics.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>
#include <vector>

namespace cw::server {
namespace {

[[nodiscard]] constexpr bool builtin_start(
    token_kind kind) noexcept {

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

[[nodiscard]] graph_record_kind record_kind(
    token_kind kind) noexcept {

    if (kind == token_kind::kw_class) {
        return graph_record_kind::class_type;
    }

    if (kind == token_kind::kw_union) {
        return graph_record_kind::union_type;
    }

    return graph_record_kind::struct_type;
}

[[nodiscard]] graph_member_access default_access(
    graph_record_kind kind) noexcept {

    return kind == graph_record_kind::class_type
        ? graph_member_access::private_access
        : graph_member_access::public_access;
}

enum class semantic_domain : std::uint8_t {
    header,
    source,
};

constexpr std::size_t declarator_depth_limit = 64;

enum class pending_construction_kind : std::uint8_t {
    value,
    member_name,
};

struct semantic_source_location final {
    file_id file{};
    source_range source;
};

struct pending_construction final {
    construction_value value{};
    string_id member_name{};
    semantic_source_location location;
    pending_construction_kind kind =
        pending_construction_kind::value;
};

struct constructor_operation final {
    string_id target{};
    semantic_source_location target_location;
    pending_construction expression;
};

struct resolved_link_endpoint final {
    object_endpoint endpoint{};
    type_ref type{};
    semantic_source_location location;
};

struct declarator_modifier final {
    std::uint64_t payload = 0;
    derived_type_kind kind =
        derived_type_kind::pointer;
    semantic_source_location location;
};

struct parsed_declarator final {
    string_id name{};
    semantic_source_location location;
};

template <typename Graph, typename Sources>
class semantic_parser final {
public:
    semantic_parser(
        file_context& files_value,
        lexical_generation& lexical,
        const preprocessor_configuration& configuration,
        string_table& strings,
        identity_space& identities,
        Graph& G,
        Sources& sources,
        parser_failure* failure,
        std::vector<parser_warning>* warnings) noexcept
        : files(files_value),
          input(
              files_value,
              lexical,
              configuration,
              strings),
          strings(strings),
          identities(identities),
          G(G),
          sources(sources),
          failure(failure),
          warnings(warnings) {
    }

    [[nodiscard]] server_status parse(
        file_id root,
        semantic_domain domain_value) noexcept {

        if (failure != nullptr) {
            *failure = {};
        }

        domain = domain_value;
        semantic_root = root;
        internal_static_scope_name = {};
        current = {};
        buffered = {};
        has_buffered = false;

        const auto provenance_started =
            sources.begin_root(root);

        if (!succeeded(provenance_started)) {
            return provenance_started;
        }

        const auto started =
            input.start(
                root,
                domain == semantic_domain::header
                    ? semantic_input_mode::header
                    : semantic_input_mode::source);

        if (!succeeded(started)) {
            return input_failure(started);
        }

        const auto advanced =
            advance();

        if (!succeeded(advanced)) {
            return advanced;
        }

        const auto parsed =
            parse_scope(
                identities.root(),
                false,
                0);

        if (!succeeded(parsed)) {
            return parsed;
        }

        return sources.end_root();
    }

private:
    [[nodiscard]] bool read_type(
        type_handle type,
        type_entry& output) const noexcept {

        output = {};

        if constexpr (
            std::is_same_v<Graph, graph>) {

            const auto* value =
                G.find(type);

            if (value == nullptr) {
                return false;
            }

            output = *value;
            return true;
        }
        else {
            return G.type(
                type,
                output);
        }
    }

    [[nodiscard]] bool read_object(
        object_handle object,
        object_entry& output) const noexcept {

        output = {};

        if constexpr (
            std::is_same_v<Graph, graph>) {

            const auto* value =
                G.find(object);

            if (value == nullptr) {
                return false;
            }

            output = *value;
            return true;
        }
        else {
            return G.object(
                object,
                output);
        }
    }

    [[nodiscard]] bool read_member(
        type_handle type,
        member_index member,
        member_record& output) const noexcept {

        output = {};

        if constexpr (
            std::is_same_v<Graph, graph>) {

            const auto* value =
                G.member(
                    type,
                    member);

            if (value == nullptr) {
                return false;
            }

            output = *value;
            return true;
        }
        else {
            return G.member(
                type,
                member,
                output);
        }
    }

    [[nodiscard]] bool record_polymorphic(
        type_handle type) const noexcept {

        if constexpr (
            std::is_same_v<Graph, graph>) {

            return G.polymorphic(
                type);
        }
        else {
            type_entry value;

            return read_type(
                       type,
                       value) &&
                value.defined() &&
                value.polymorphic();
        }
    }

    [[nodiscard]] server_status ensure_internal_static_scope_name() noexcept {

        if (internal_static_scope_name) {
            return server_status::success;
        }

        constexpr std::string_view prefix{
            "<static-root:"};

        std::array<char, 32> buffer{};
        std::size_t cursor = 0;

        for (const auto value : prefix) {
            buffer[cursor++] = value;
        }

        const auto converted =
            std::to_chars(
                buffer.data() + cursor,
                buffer.data() + buffer.size() - 1,
                semantic_root.value());

        if (converted.ec != std::errc{}) {
            return server_status::io_error;
        }

        *converted.ptr = '>';

        return strings.intern(
            std::string_view{
                buffer.data(),
                static_cast<std::size_t>(
                    converted.ptr -
                    buffer.data() + 1)},
            internal_static_scope_name);
    }

    [[nodiscard]] server_status resolve_internal_static_identity(
        identity_ref scope,
        string_id name,
        identity_ref& output) noexcept {

        output = {};

        const auto prepared =
            ensure_internal_static_scope_name();

        if (!succeeded(prepared)) {
            return prepared;
        }

        identity_ref internal_scope;

        const auto scope_resolved =
            identities.resolve(
                scope,
                internal_static_scope_name,
                identity_kind::namespace_scope,
                internal_scope);

        if (!succeeded(scope_resolved)) {
            return scope_resolved;
        }

        return identities.resolve(
            internal_scope,
            name,
            identity_kind::object,
            output);
    }

    [[nodiscard]] object_handle find_internal_static_object(
        identity_ref scope,
        string_id name) const noexcept {

        if (!internal_static_scope_name ||
            !name) {

            return {};
        }

        auto current_scope = scope;

        while (current_scope) {
            const auto internal_scope =
                identities.find(
                    current_scope,
                    internal_static_scope_name,
                    identity_kind::namespace_scope);

            if (internal_scope) {
                const auto identity =
                    identities.find(
                        internal_scope,
                        name,
                        identity_kind::object);

                const auto object =
                    G.find_object(identity);

                object_entry value;

                if (object &&
                    read_object(
                        object,
                        value) &&
                    value.internal_static()) {

                    return object;
                }
            }

            if (current_scope ==
                identities.root()) {

                break;
            }

            identity_record record;

            if (!identities.record(
                    current_scope,
                    record)) {

                break;
            }

            current_scope =
                record.parent;
        }

        return {};
    }

    [[nodiscard]] semantic_source_location
    current_location() const noexcept {

        return {
            current.file,
            {
                current.source_offset,
                current.source_length,
            },
        };
    }

    [[nodiscard]] server_status fail_at(
        parser_failure_kind kind,
        std::string_view detail,
        semantic_source_location location) noexcept {

        if (failure != nullptr) {
            failure->kind = kind;
            failure->file = location.file;
            failure->source = location.source;
            failure->detail = detail;
        }

        return server_status::project_configuration_invalid;
    }

    [[nodiscard]] server_status warn_at(
        parser_warning_kind kind,
        std::string_view detail,
        semantic_source_location location) noexcept {

        if (warnings == nullptr) {
            return server_status::success;
        }

        try {
            warnings->push_back({
                kind,
                location.file,
                location.source,
                detail,
            });

            return server_status::success;
        }
        catch (...) {
            return server_status::io_error;
        }
    }

    [[nodiscard]] bool reference_referent(
        type_ref type,
        type_ref& output) const noexcept {

        output = {};

        derived_type_record derived;

        if (!G.derived(
                type,
                derived) ||
            (derived.kind !=
                 derived_type_kind::lvalue_reference &&
             derived.kind !=
                 derived_type_kind::rvalue_reference)) {

            return false;
        }

        output = derived.child;
        return static_cast<bool>(output);
    }

    [[nodiscard]] type_ref expression_type(
        type_ref type) const noexcept {

        type_ref referent;

        return reference_referent(
            type,
            referent)
            ? referent
            : type;
    }

    [[nodiscard]] bool reference_binding_compatible(
        type_ref target,
        type_ref source) const noexcept {

        type_ref referent;

        return reference_referent(
                   target,
                   referent) &&
            referent ==
                expression_type(source);
    }

    [[nodiscard]] server_status resolve_reference_binding(
        identity_ref scope,
        const std::vector<member_record>& members,
        type_ref target,
        string_id name,
        semantic_source_location location,
        construction_value& output) noexcept {

        output = {};

        for (std::size_t index = 0;
             index < members.size();
             ++index) {

            if (members[index].name == name) {
                if (index >=
                    (std::numeric_limits<
                        std::uint32_t>::max)()) {

                    return server_status::io_error;
                }

                if (!reference_binding_compatible(
                        target,
                        members[index].type)) {

                    return fail_at(
                        parser_failure_kind::semantic,
                        "Reference binding type does not match bound member type",
                        location);
                }

                output =
                    construction_value::member_binding(
                        static_cast<std::uint32_t>(
                            index + 1));

                return server_status::success;
            }
        }

        const auto object =
            find_internal_static_object(
                scope,
                name);

        if (!object) {
            return fail_at(
                parser_failure_kind::semantic,
                "Reference binding names neither a member nor a visible Header static object",
                location);
        }

        object_entry object_value;

        if (!read_object(
                object,
                object_value) ||
            !reference_binding_compatible(
                target,
                object_value.type)) {

            return fail_at(
                parser_failure_kind::semantic,
                "Reference binding type does not match bound object type",
                location);
        }

        const auto dependency =
            sources.add_dependency(
                G.identity(object));

        if (!succeeded(dependency)) {
            return dependency;
        }

        output =
            construction_value::object_binding(
                object.value());

        return server_status::success;
    }

    [[nodiscard]] server_status input_failure(
        server_status status) noexcept {

        if (failure != nullptr) {
            const auto& value =
                input.failure();

            failure->kind =
                value.kind ==
                    semantic_input_failure_kind::lexical
                ? parser_failure_kind::lexical
                : parser_failure_kind::preprocessing;

            failure->file = value.file;
            failure->source = value.source;
            failure->detail = value.detail;
        }

        return status;
    }

    [[nodiscard]] server_status fail(
        parser_failure_kind kind,
        std::string_view detail) noexcept {

        return fail_at(
            kind,
            detail,
            current_location());
    }

    [[nodiscard]] server_status advance() noexcept {

        current = {};

        if (has_buffered) {
            current = buffered;
            buffered = {};
            has_buffered = false;
            return server_status::success;
        }

        if (input.finished()) {
            return server_status::success;
        }

        const auto result =
            input.next(current);

        return succeeded(result)
            ? result
            : input_failure(result);
    }

    [[nodiscard]] server_status peek(
        semantic_token& output) noexcept {

        output = {};

        if (has_buffered) {
            output = buffered;
            return server_status::success;
        }

        if (input.finished()) {
            return server_status::success;
        }

        const auto result =
            input.next(buffered);

        if (!succeeded(result)) {
            return input_failure(result);
        }

        has_buffered = true;
        output = buffered;

        return server_status::success;
    }

    [[nodiscard]] bool at(
        token_kind kind) const noexcept {

        return current.kind == kind;
    }

    [[nodiscard]] server_status expect(
        token_kind kind,
        std::string_view detail) noexcept {

        return at(kind)
            ? server_status::success
            : fail(
                parser_failure_kind::syntax,
                detail);
    }

    [[nodiscard]] std::string_view token_text(
        const semantic_token& token) const noexcept {

        if (!token.file ||
            !files.contains(token.file) ||
            !files.content_available(token.file)) {

            return {};
        }

        const auto source =
            files.content(token.file);

        const auto begin =
            static_cast<std::size_t>(
                token.source_offset);

        const auto length =
            static_cast<std::size_t>(
                token.source_length);

        if (begin > source.size() ||
            length > source.size() - begin) {

            return {};
        }

        return source.substr(
            begin,
            length);
    }

    [[nodiscard]] identity_ref find_type_identity(
        identity_ref scope,
        string_id name) const noexcept {

        auto current_scope = scope;

        while (current_scope) {
            if (const auto found =
                    identities.find(
                        current_scope,
                        name,
                        identity_kind::type);
                found) {

                return found;
            }

            if (current_scope ==
                identities.root()) {

                break;
            }

            identity_record record;

            if (!identities.record(
                    current_scope,
                    record)) {
                break;
            }

            current_scope =
                record.parent;
        }

        return {};
    }

    [[nodiscard]] identity_ref find_object_identity(
        identity_ref scope,
        string_id name) const noexcept {

        auto current_scope = scope;

        while (current_scope) {
            if (const auto found =
                    identities.find(
                        current_scope,
                        name,
                        identity_kind::object);
                found) {

                return found;
            }

            if (current_scope ==
                identities.root()) {

                break;
            }

            identity_record record;

            if (!identities.record(
                    current_scope,
                    record)) {
                break;
            }

            current_scope =
                record.parent;
        }

        return {};
    }

    [[nodiscard]] bool reference_type(
        type_ref type) const noexcept {

        type_ref referent;

        return reference_referent(
            type,
            referent);
    }

    [[nodiscard]] server_status parse_intrinsic(
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
                    return fail(
                        parser_failure_kind::syntax,
                        "Invalid signed/unsigned type specifier combination");
                }
                is_signed = true;
            }
            else if (at(token_kind::kw_unsigned)) {
                if (is_signed || is_unsigned) {
                    return fail(
                        parser_failure_kind::syntax,
                        "Invalid signed/unsigned type specifier combination");
                }
                is_unsigned = true;
            }
            else if (at(token_kind::kw_short)) {
                if (is_short || long_count != 0) {
                    return fail(
                        parser_failure_kind::syntax,
                        "Invalid short/long type specifier combination");
                }
                is_short = true;
            }
            else {
                if (is_short || long_count == 2) {
                    return fail(
                        parser_failure_kind::syntax,
                        "Invalid long type specifier combination");
                }
                ++long_count;
            }

            const auto advanced = advance();
            if (!succeeded(advanced)) {
                return advanced;
            }
        }

        if (at(token_kind::identifier) &&
            strings.get(current.identifier) == "__int64") {
            if (is_short || long_count != 0) {
                return fail(parser_failure_kind::syntax,
                    "__int64 cannot use short/long modifiers");
            }
            output = is_unsigned
                ? intrinsic_type::unsigned_long_long
                : intrinsic_type::signed_long_long;
            return advance();
        }

        if (at(token_kind::kw_char)) {
            if (is_short || long_count != 0) {
                return fail(
                    parser_failure_kind::syntax,
                    "char cannot use short/long modifiers");
            }

            output =
                is_unsigned
                ? intrinsic_type::unsigned_char
                : is_signed
                    ? intrinsic_type::signed_char
                    : intrinsic_type::char_type;

            return advance();
        }

        if (at(token_kind::kw_double)) {
            if (is_signed ||
                is_unsigned ||
                is_short ||
                long_count > 1) {

                return fail(
                    parser_failure_kind::syntax,
                    "Invalid double type specifier combination");
            }

            output =
                long_count == 1
                ? intrinsic_type::long_double_type
                : intrinsic_type::double_type;

            return advance();
        }

        if (at(token_kind::kw_int)) {
            const auto advanced = advance();
            if (!succeeded(advanced)) {
                return advanced;
            }
        }
        else if (!is_signed &&
                 !is_unsigned &&
                 !is_short &&
                 long_count == 0) {

            switch (current.kind) {
            case token_kind::kw_void:
                output = intrinsic_type::void_type;
                break;
            case token_kind::kw_bool:
                output = intrinsic_type::bool_type;
                break;
            case token_kind::kw_wchar_t:
                output = intrinsic_type::wchar_type;
                break;
            case token_kind::kw_char8_t:
                output = intrinsic_type::char8_type;
                break;
            case token_kind::kw_char16_t:
                output = intrinsic_type::char16_type;
                break;
            case token_kind::kw_char32_t:
                output = intrinsic_type::char32_type;
                break;
            case token_kind::kw_float:
                output = intrinsic_type::float_type;
                break;
            default:
                return fail(
                    parser_failure_kind::syntax,
                    "Expected supported intrinsic type");
            }

            return advance();
        }

        if (long_count == 2) {
            output =
                is_unsigned
                ? intrinsic_type::unsigned_long_long
                : intrinsic_type::signed_long_long;
        }
        else if (long_count == 1) {
            output =
                is_unsigned
                ? intrinsic_type::unsigned_long
                : intrinsic_type::signed_long;
        }
        else if (is_short) {
            output =
                is_unsigned
                ? intrinsic_type::unsigned_short
                : intrinsic_type::signed_short;
        }
        else {
            output =
                is_unsigned
                ? intrinsic_type::unsigned_int
                : intrinsic_type::signed_int;
        }

        return server_status::success;
    }

    [[nodiscard]] server_status apply_qualifiers(
        bool const_qualified,
        bool volatile_qualified,
        type_ref& type) noexcept {

        if (const_qualified) {
            type_ref wrapped;
            const auto status =
                G.derive(
                    type,
                    derived_type_kind::const_qualified,
                    0,
                    wrapped);

            if (!succeeded(status)) {
                return status;
            }
            type = wrapped;
        }

        if (volatile_qualified) {
            type_ref wrapped;
            const auto status =
                G.derive(
                    type,
                    derived_type_kind::volatile_qualified,
                    0,
                    wrapped);

            if (!succeeded(status)) {
                return status;
            }
            type = wrapped;
        }

        return server_status::success;
    }

    [[nodiscard]] server_status parse_type_specifier(
        identity_ref scope,
        type_ref& output) noexcept {

        output = {};

        bool const_qualified = false;
        bool volatile_qualified = false;

        while (at(token_kind::kw_const) ||
               at(token_kind::kw_volatile)) {

            if (at(token_kind::kw_const)) {
                const_qualified = true;
            }
            else {
                volatile_qualified = true;
            }

            const auto advanced = advance();
            if (!succeeded(advanced)) {
                return advanced;
            }
        }

        if (builtin_start(current.kind) ||
            (at(token_kind::identifier) && strings.get(current.identifier) == "__int64")) {
            intrinsic_type intrinsic;
            const auto parsed =
                parse_intrinsic(intrinsic);

            if (!succeeded(parsed)) {
                return parsed;
            }

            output = G.intrinsic(intrinsic);
        }
        else if (at(token_kind::identifier) &&
                 current.identifier) {

            const auto identity =
                find_type_identity(
                    scope,
                    current.identifier);

            if (!identity) {
                return fail(
                    parser_failure_kind::semantic,
                    "Named type is not declared in the visible semantic scope");
            }

            const auto handle =
                G.find_type(identity);

            if (!handle) {
                return fail(
                    parser_failure_kind::semantic,
                    "Named semantic identity has no type in G");
            }

            const auto dependency =
                sources.add_dependency(
                    identity);

            if (!succeeded(dependency)) {
                return dependency;
            }

            output = G.named(handle);

            const auto advanced = advance();
            if (!succeeded(advanced)) {
                return advanced;
            }
        }
        else {
            return fail(
                parser_failure_kind::syntax,
                "Expected type");
        }

        if (!output) {
            return fail(
                parser_failure_kind::semantic,
                "Type could not be represented in G");
        }

        while (at(token_kind::kw_const) ||
               at(token_kind::kw_volatile)) {

            if (at(token_kind::kw_const)) {
                const_qualified = true;
            }
            else {
                volatile_qualified = true;
            }

            const auto advanced = advance();
            if (!succeeded(advanced)) {
                return advanced;
            }
        }

        return apply_qualifiers(
            const_qualified,
            volatile_qualified,
            output);
    }

    [[nodiscard]] bool is_void_type(
        type_ref type) const noexcept {

        derived_type_record derived;

        while (G.derived(
                   type,
                   derived) &&
               (derived.kind ==
                    derived_type_kind::const_qualified ||
                derived.kind ==
                    derived_type_kind::volatile_qualified)) {

            type = derived.child;
        }

        intrinsic_type intrinsic;

        return G.intrinsic(
                   type,
                   intrinsic) &&
            intrinsic ==
                intrinsic_type::void_type;
    }

    [[nodiscard]] server_status append_declarator_modifier(
        derived_type_kind kind,
        std::uint64_t payload,
        semantic_source_location location) noexcept {

        try {
            declarator_modifiers.push_back({
                payload,
                kind,
                location,
            });

            return server_status::success;
        }
        catch (...) {
            return server_status::io_error;
        }
    }

    [[nodiscard]] server_status parse_declarator_node(
        parsed_declarator& output,
        std::size_t depth,
        bool allow_named_operator = false) noexcept {

        if (depth >=
            declarator_depth_limit) {

            return fail(
                parser_failure_kind::unsupported,
                "Declarator nesting exceeds the supported depth");
        }

        const auto prefix_begin =
            declarator_modifiers.size();

        while (at(token_kind::star) ||
               at(token_kind::ampersand) ||
               at(token_kind::logical_and)) {

            const auto location =
                current_location();

            const auto kind =
                at(token_kind::star)
                ? derived_type_kind::pointer
                : at(token_kind::ampersand)
                    ? derived_type_kind::lvalue_reference
                    : derived_type_kind::rvalue_reference;

            auto status =
                append_declarator_modifier(
                    kind,
                    0,
                    location);

            if (!succeeded(status)) {
                return status;
            }

            status = advance();

            if (!succeeded(status)) {
                return status;
            }
        }

        const auto prefix_end =
            declarator_modifiers.size();

        if (allow_named_operator && depth == 0 && at(token_kind::kw_operator)) {
            output.location = current_location();
            auto status = advance();
            if (!succeeded(status)) {
                return status;
            }
            std::string_view operator_name = "operator=";
            if (at(token_kind::l_bracket)) {
                operator_name = "operator[]";
                status = advance();
                if (!succeeded(status)) {
                    return status;
                }
                if (!at(token_kind::r_bracket)) {
                    return fail(parser_failure_kind::syntax,
                        "Expected ']' in operator[] declarator");
                }
            }
            else if (!at(token_kind::assign)) {
                return fail(parser_failure_kind::unsupported,
                    "Only operator= and operator[] are supported in named operator declarators");
            }
            status = strings.intern(operator_name, output.name);
            if (!succeeded(status)) {
                return status;
            }
            status = advance();
            if (!succeeded(status)) {
                return status;
            }
            if (!at(token_kind::l_paren)) {
                return fail(parser_failure_kind::syntax,
                    "Expected '(' after named operator");
            }
        }
        else if (at(token_kind::identifier) &&
            current.identifier) {

            if (output.name) {
                return fail(
                    parser_failure_kind::syntax,
                    "Declarator contains multiple identifiers");
            }

            output.name =
                current.identifier;

            output.location =
                current_location();

            const auto status = advance();

            if (!succeeded(status)) {
                return status;
            }
        }
        else if (at(token_kind::l_paren)) {
            auto status = advance();

            if (!succeeded(status)) {
                return status;
            }

            status =
                parse_declarator_node(
                    output,
                    depth + 1);

            if (!succeeded(status)) {
                return status;
            }

            status =
                expect(
                    token_kind::r_paren,
                    "Expected ')' after parenthesized declarator");

            if (!succeeded(status)) {
                return status;
            }

            status = advance();

            if (!succeeded(status)) {
                return status;
            }
        }
        else {
            return fail(
                parser_failure_kind::syntax,
                "Expected declarator identifier or parenthesized declarator");
        }

        while (at(token_kind::l_bracket)) {
            const auto array_location =
                current_location();

            auto status = advance();

            if (!succeeded(status)) {
                return status;
            }

            if (at(token_kind::r_bracket)) {
                return fail_at(
                    parser_failure_kind::unsupported,
                    "Unbounded arrays are not implemented",
                    array_location);
            }

            if (!at(token_kind::pp_number)) {
                return fail(
                    parser_failure_kind::unsupported,
                    "Array bounds require a positive decimal integer literal");
            }

            const auto bound_location =
                current_location();

            const auto spelling =
                token_text(current);

            std::uint64_t bound = 0;

            const auto converted =
                std::from_chars(
                    spelling.data(),
                    spelling.data() +
                        spelling.size(),
                    bound);

            if (converted.ec != std::errc{} ||
                converted.ptr !=
                    spelling.data() +
                        spelling.size()) {

                return fail_at(
                    parser_failure_kind::unsupported,
                    "Only positive decimal array bounds are supported",
                    bound_location);
            }

            if (bound == 0) {
                return fail_at(
                    parser_failure_kind::semantic,
                    "Array bound must be greater than zero",
                    bound_location);
            }

            status = advance();

            if (!succeeded(status)) {
                return status;
            }

            if (!at(token_kind::r_bracket)) {
                return fail(
                    parser_failure_kind::unsupported,
                    "Array bound expressions are not implemented");
            }

            status = advance();

            if (!succeeded(status)) {
                return status;
            }

            status =
                append_declarator_modifier(
                    derived_type_kind::bounded_array,
                    bound,
                    array_location);

            if (!succeeded(status)) {
                return status;
            }
        }

        if (prefix_begin !=
            prefix_end) {

            auto begin =
                declarator_modifiers.begin() +
                static_cast<std::ptrdiff_t>(
                    prefix_begin);

            auto middle =
                declarator_modifiers.begin() +
                static_cast<std::ptrdiff_t>(
                    prefix_end);

            std::reverse(
                begin,
                middle);

            std::rotate(
                begin,
                middle,
                declarator_modifiers.end());
        }

        return server_status::success;
    }

    [[nodiscard]] server_status apply_declarator(
        type_ref base,
        type_ref& output) noexcept {

        output = base;

        for (std::size_t index =
                 declarator_modifiers.size();
             index != 0;
             --index) {

            const auto& modifier =
                declarator_modifiers[
                    index - 1];

            const auto child_is_reference =
                reference_type(output);

            if (modifier.kind ==
                    derived_type_kind::pointer &&
                child_is_reference) {

                return fail_at(
                    parser_failure_kind::semantic,
                    "Pointers to references are not valid C++",
                    modifier.location);
            }

            if (modifier.kind ==
                    derived_type_kind::lvalue_reference ||
                modifier.kind ==
                    derived_type_kind::rvalue_reference) {

                if (child_is_reference) {
                    return fail_at(
                        parser_failure_kind::semantic,
                        "References to references are not valid C++",
                        modifier.location);
                }

                if (is_void_type(output)) {
                    return fail_at(
                        parser_failure_kind::semantic,
                        "References to void are not valid C++",
                        modifier.location);
                }
            }

            if (modifier.kind ==
                derived_type_kind::bounded_array) {

                if (child_is_reference) {
                    return fail_at(
                        parser_failure_kind::semantic,
                        "Arrays of references are not valid C++",
                        modifier.location);
                }

                if (is_void_type(output)) {
                    return fail_at(
                        parser_failure_kind::semantic,
                        "Arrays of void are not valid C++",
                        modifier.location);
                }
            }

            type_ref wrapped;

            const auto derived =
                G.derive(
                    output,
                    modifier.kind,
                    modifier.payload,
                    wrapped);

            if (!succeeded(derived)) {
                if (derived ==
                    server_status::
                        project_configuration_invalid) {

                    return fail_at(
                        parser_failure_kind::semantic,
                        "Declarator produces an invalid C++ type",
                        modifier.location);
                }

                return derived;
            }

            output = wrapped;
        }

        return server_status::success;
    }

    [[nodiscard]] server_status parse_declared_type(
        identity_ref scope,
        type_ref& output,
        parsed_declarator& declarator,
        bool allow_named_operator = false) noexcept {

        output = {};
        declarator = {};
        declarator_modifiers.clear();

        type_ref base;

        auto status =
            parse_type_specifier(
                scope,
                base);

        if (!succeeded(status)) {
            return status;
        }

        status =
            parse_declarator_node(
                declarator,
                0,
                allow_named_operator);

        if (!succeeded(status)) {
            return status;
        }

        if (!declarator.name) {
            return fail(
                parser_failure_kind::syntax,
                "Declarator has no identifier");
        }

        return apply_declarator(
            base,
            output);
    }

    [[nodiscard]] server_status parse_number(
        bool negative,
        construction_value& output) noexcept {

        if (!at(token_kind::pp_number)) {
            return fail(
                parser_failure_kind::syntax,
                "Expected numeric initializer");
        }

        const auto spelling =
            token_text(current);

        if (spelling.empty()) {
            return fail(
                parser_failure_kind::syntax,
                "Numeric initializer spelling is unavailable");
        }

        if (spelling.find_first_of(".eE") !=
            std::string_view::npos) {

            double value = 0;
            const auto parsed =
                std::from_chars(
                    spelling.data(),
                    spelling.data() + spelling.size(),
                    value);

            if (parsed.ec != std::errc{} ||
                parsed.ptr !=
                    spelling.data() + spelling.size()) {

                return fail(
                    parser_failure_kind::unsupported,
                    "Only plain decimal real initializers are supported");
            }

            if (negative) {
                value = -value;
            }

            output =
                construction_value::constant(
                    construction_kind::real,
                    std::bit_cast<std::uint64_t>(value));

            return advance();
        }

        std::uint64_t magnitude = 0;
        const auto parsed =
            std::from_chars(
                spelling.data(),
                spelling.data() + spelling.size(),
                magnitude);

        if (parsed.ec != std::errc{} ||
            parsed.ptr !=
                spelling.data() + spelling.size()) {

            return fail(
                parser_failure_kind::unsupported,
                "Only plain decimal integer initializers are supported");
        }

        if (!negative) {
            output =
                construction_value::constant(
                    construction_kind::unsigned_integer,
                    magnitude);

            return advance();
        }

        constexpr auto minimum_magnitude =
            std::uint64_t{
                (std::numeric_limits<std::int64_t>::max)()} +
            1;

        if (magnitude > minimum_magnitude) {
            return fail(
                parser_failure_kind::semantic,
                "Signed integer initializer exceeds 64-bit range");
        }

        const auto signed_value =
            magnitude == minimum_magnitude
            ? (std::numeric_limits<std::int64_t>::min)()
            : -static_cast<std::int64_t>(magnitude);

        output =
            construction_value::constant(
                construction_kind::signed_integer,
                std::bit_cast<std::uint64_t>(
                    signed_value));

        return advance();
    }

    [[nodiscard]] server_status parse_construction_atom(
        type_ref target,
        bool allow_member_name,
        pending_construction& output) noexcept {

        output = {};
        output.location =
            current_location();

        if (reference_type(target)) {
            if (!allow_member_name) {
                return fail(
                    parser_failure_kind::unsupported,
                    "Top-level reference object initialization is not implemented");
            }

            if (at(token_kind::kw_this)) {
                auto status = advance();
                if (!succeeded(status)) {
                    return status;
                }

                status =
                    expect(
                        token_kind::dot,
                        "Expected '.' after this in reference member initializer");

                if (!succeeded(status)) {
                    return status;
                }

                status = advance();
                if (!succeeded(status)) {
                    return status;
                }
            }

            if (!at(token_kind::identifier) ||
                !current.identifier) {

                return fail(
                    parser_failure_kind::unsupported,
                    "Reference member initializer must name another member");
            }

            output.kind =
                pending_construction_kind::member_name;
            output.member_name =
                current.identifier;
            output.location =
                current_location();

            return advance();
        }

        if (at(token_kind::kw_false) ||
            at(token_kind::kw_nullptr)) {

            output.value = {};
            return advance();
        }

        if (at(token_kind::kw_true)) {
            output.value =
                construction_value::constant(
                    construction_kind::unsigned_integer,
                    1);

            return advance();
        }

        bool negative = false;

        if (at(token_kind::plus) ||
            at(token_kind::minus)) {

            negative = at(token_kind::minus);

            const auto advanced = advance();
            if (!succeeded(advanced)) {
                return advanced;
            }

            output.location =
                current_location();
        }

        if (at(token_kind::pp_number)) {
            return parse_number(
                negative,
                output.value);
        }

        return fail(
            parser_failure_kind::unsupported,
            "Initializer supports scalar constants or local reference member bindings");
    }

    [[nodiscard]] server_status parse_initializer(
        type_ref target,
        bool allow_member_name,
        pending_construction& output) noexcept {

        output = {};

        if (at(token_kind::assign)) {
            auto status = advance();
            if (!succeeded(status)) {
                return status;
            }

            return parse_construction_atom(
                target,
                allow_member_name,
                output);
        }

        if (at(token_kind::l_brace)) {
            auto status = advance();
            if (!succeeded(status)) {
                return status;
            }

            if (at(token_kind::r_brace)) {
                return advance();
            }

            status =
                parse_construction_atom(
                    target,
                    allow_member_name,
                    output);

            if (!succeeded(status)) {
                return status;
            }

            status =
                expect(
                    token_kind::r_brace,
                    "Expected '}' after initializer");

            if (!succeeded(status)) {
                return status;
            }

            return advance();
        }

        return server_status::success;
    }

    [[nodiscard]] server_status parse_constructor_expression(
        pending_construction& output) noexcept {

        output = {};
        output.location =
            current_location();

        if (at(token_kind::kw_this)) {
            auto status = advance();

            if (!succeeded(status)) {
                return status;
            }

            status =
                expect(
                    token_kind::dot,
                    "Expected '.' after this in constructor expression");

            if (!succeeded(status)) {
                return status;
            }

            status = advance();

            if (!succeeded(status)) {
                return status;
            }
        }

        if (at(token_kind::identifier) &&
            current.identifier) {

            output.kind =
                pending_construction_kind::member_name;
            output.member_name =
                current.identifier;
            output.location =
                current_location();

            return advance();
        }

        if (at(token_kind::kw_false) ||
            at(token_kind::kw_nullptr)) {

            output.value = {};
            return advance();
        }

        if (at(token_kind::kw_true)) {
            output.value =
                construction_value::constant(
                    construction_kind::unsigned_integer,
                    1);

            return advance();
        }

        bool negative = false;

        if (at(token_kind::plus) ||
            at(token_kind::minus)) {

            negative =
                at(token_kind::minus);

            const auto status =
                advance();

            if (!succeeded(status)) {
                return status;
            }

            output.location =
                current_location();
        }

        if (at(token_kind::pp_number)) {
            return parse_number(
                negative,
                output.value);
        }

        return fail(
            parser_failure_kind::unsupported,
            "Managed constructor supports scalar constants and local member bindings only");
    }

    [[nodiscard]] server_status append_constructor_operation(
        std::vector<constructor_operation>& operations,
        string_id target,
        semantic_source_location target_location,
        pending_construction expression) noexcept {

        if (!target) {
            return fail(
                parser_failure_kind::syntax,
                "Managed constructor target is invalid");
        }

        try {
            operations.push_back({
                target,
                target_location,
                expression,
            });

            return server_status::success;
        }
        catch (...) {
            return server_status::io_error;
        }
    }

    [[nodiscard]] server_status parse_constructor(
        string_id record_name,
        std::vector<constructor_operation>& operations) noexcept {

        if (!at(token_kind::identifier) ||
            current.identifier != record_name) {

            return fail(
                parser_failure_kind::syntax,
                "Managed constructor name must match its record");
        }

        auto status = advance();

        if (!succeeded(status)) {
            return status;
        }

        status =
            expect(
                token_kind::l_paren,
                "Expected '(' after managed constructor name");

        if (!succeeded(status)) {
            return status;
        }

        status = advance();

        if (!succeeded(status)) {
            return status;
        }

        if (!at(token_kind::r_paren)) {
            return fail(
                parser_failure_kind::unsupported,
                "Managed constructors must have no parameters");
        }

        status = advance();

        if (!succeeded(status)) {
            return status;
        }

        if (at(token_kind::kw_noexcept)) {
            status = advance();

            if (!succeeded(status)) {
                return status;
            }
        }

        if (at(token_kind::semicolon)) {
            return advance();
        }

        if (at(token_kind::assign)) {
            status = advance();

            if (!succeeded(status)) {
                return status;
            }

            status =
                expect(
                    token_kind::kw_default,
                    "Managed constructors support '= default' only");

            if (!succeeded(status)) {
                return status;
            }

            status = advance();

            if (!succeeded(status)) {
                return status;
            }

            status =
                expect(
                    token_kind::semicolon,
                    "Expected ';' after '= default'");

            if (!succeeded(status)) {
                return status;
            }

            return advance();
        }

        if (at(token_kind::colon)) {
            status = advance();

            if (!succeeded(status)) {
                return status;
            }

            for (;;) {
                if (!at(token_kind::identifier) ||
                    !current.identifier) {

                    return fail(
                        parser_failure_kind::syntax,
                        "Expected constructor member name");
                }

                const auto target =
                    current.identifier;

                const auto target_location =
                    current_location();

                status = advance();

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
                        parser_failure_kind::syntax,
                        "Expected constructor member initializer");
                }

                status = advance();

                if (!succeeded(status)) {
                    return status;
                }

                pending_construction expression;

                if (!at(close)) {
                    status =
                        parse_constructor_expression(
                            expression);

                    if (!succeeded(status)) {
                        return status;
                    }
                }

                status =
                    expect(
                        close,
                        "Expected end of constructor member initializer");

                if (!succeeded(status)) {
                    return status;
                }

                status =
                    append_constructor_operation(
                        operations,
                        target,
                        target_location,
                        expression);

                if (!succeeded(status)) {
                    return status;
                }

                status = advance();

                if (!succeeded(status)) {
                    return status;
                }

                if (!at(token_kind::comma)) {
                    break;
                }

                status = advance();

                if (!succeeded(status)) {
                    return status;
                }
            }
        }

        status =
            expect(
                token_kind::l_brace,
                "Expected managed constructor body");

        if (!succeeded(status)) {
            return status;
        }

        status = advance();

        if (!succeeded(status)) {
            return status;
        }

        while (!at(token_kind::r_brace)) {
            if (at(token_kind::invalid)) {
                return fail(
                    parser_failure_kind::syntax,
                    "Managed constructor body is not closed");
            }

            if (!at(token_kind::identifier) ||
                !current.identifier) {

                return fail(
                    parser_failure_kind::unsupported,
                    "Managed constructor body supports field assignments only");
            }

            const auto target =
                current.identifier;

            const auto target_location =
                current_location();

            status = advance();

            if (!succeeded(status)) {
                return status;
            }

            status =
                expect(
                    token_kind::assign,
                    "Managed constructor body supports field assignments only");

            if (!succeeded(status)) {
                return status;
            }

            status = advance();

            if (!succeeded(status)) {
                return status;
            }

            pending_construction expression;

            status =
                parse_constructor_expression(
                    expression);

            if (!succeeded(status)) {
                return status;
            }

            status =
                expect(
                    token_kind::semicolon,
                    "Expected ';' after constructor field assignment");

            if (!succeeded(status)) {
                return status;
            }

            status =
                append_constructor_operation(
                    operations,
                    target,
                    target_location,
                    expression);

            if (!succeeded(status)) {
                return status;
            }

            status = advance();

            if (!succeeded(status)) {
                return status;
            }
        }

        return advance();
    }

    [[nodiscard]] server_status normalize_constructor_operations(
        identity_ref scope,
        const std::vector<member_record>& members,
        const std::vector<constructor_operation>& operations,
        std::vector<construction_value>& construction) noexcept {

        if (operations.empty()) {
            return server_status::success;
        }

        auto& assigned = record_assigned;

        try {
            assigned.assign(members.size(), false);
        }
        catch (...) {
            return server_status::io_error;
        }

        for (const auto& operation : operations) {
            std::size_t target_index = 0;

            for (;
                 target_index < members.size();
                 ++target_index) {

                if (members[target_index].name ==
                    operation.target) {

                    break;
                }
            }

            if (target_index == members.size()) {
                return fail_at(
                    parser_failure_kind::semantic,
                    "Constructor target is not a field of this record",
                    operation.target_location);
            }

            construction_value value;

            if (reference_type(
                    members[target_index].type)) {

                if (operation.expression.kind !=
                        pending_construction_kind::member_name ||
                    !operation.expression.member_name) {

                    return fail_at(
                        parser_failure_kind::unsupported,
                        "Reference constructor operation requires a member or Header static object binding",
                        operation.expression.location);
                }

                const auto resolved =
                    resolve_reference_binding(
                        scope,
                        members,
                        members[target_index].type,
                        operation.expression.member_name,
                        operation.expression.location,
                        value);

                if (!succeeded(resolved)) {
                    return resolved;
                }
            }
            else {
                if (operation.expression.kind !=
                        pending_construction_kind::value) {

                    return fail_at(
                        parser_failure_kind::unsupported,
                        "Value constructor operation requires a scalar constant",
                        operation.expression.location);
                }

                value =
                    operation.expression.value;

                if (!construction_compatible(
                        G,
                        members[target_index].type,
                        value)) {

                    return fail_at(
                        parser_failure_kind::semantic,
                        "Constructor initializer is not compatible with target type",
                        operation.expression.location);
                }
            }

            if (assigned[target_index] &&
                construction[target_index].kind ==
                    construction_kind::member_binding &&
                construction[target_index] != value) {

                return fail_at(
                    parser_failure_kind::semantic,
                    "Conflicting constructor reference bindings",
                    operation.expression.location);
            }

            construction[target_index] =
                value;
            assigned[target_index] =
                true;
        }

        return server_status::success;
    }

    [[nodiscard]] server_status parse_namespace(
        identity_ref parent,
        std::size_t scope_depth) noexcept {

        if (scope_depth >=
            parser_scope_depth_limit) {

            return fail(
                parser_failure_kind::unsupported,
                "Namespace nesting exceeds the supported parser scope depth");
        }

        auto status = advance();
        if (!succeeded(status)) {
            return status;
        }

        if (!at(token_kind::identifier) ||
            !current.identifier) {

            return fail(
                parser_failure_kind::syntax,
                "Expected namespace identifier");
        }

        auto scope = parent;

        for (;;) {
            identity_ref resolved;

            status =
                identities.resolve(
                    scope,
                    current.identifier,
                    identity_kind::namespace_scope,
                    resolved);

            if (!succeeded(status)) {
                return status;
            }

            scope = resolved;

            status = advance();
            if (!succeeded(status)) {
                return status;
            }

            if (!at(token_kind::scope)) {
                break;
            }

            status = advance();
            if (!succeeded(status)) {
                return status;
            }

            if (!at(token_kind::identifier) ||
                !current.identifier) {

                return fail(
                    parser_failure_kind::syntax,
                    "Expected namespace identifier after ::");
            }
        }

        status =
            expect(
                token_kind::l_brace,
                "Expected '{' after namespace name");

        if (!succeeded(status)) {
            return status;
        }

        status = advance();
        if (!succeeded(status)) {
            return status;
        }

        status =
            parse_scope(
                scope,
                true,
                scope_depth + 1);

        if (!succeeded(status)) {
            return status;
        }

        status =
            expect(
                token_kind::r_brace,
                "Expected '}' after namespace body");

        if (!succeeded(status)) {
            return status;
        }

        status = advance();
        if (!succeeded(status)) {
            return status;
        }

        if (at(token_kind::semicolon)) {
            return advance();
        }

        return server_status::success;
    }

    [[nodiscard]] server_status parse_record(
        identity_ref scope) noexcept {

        if (domain != semantic_domain::header) {
            return fail(
                parser_failure_kind::unsupported,
                "Type declarations are supported only in Header inputs");
        }

        const auto kind =
            record_kind(current.kind);

        auto status = advance();
        if (!succeeded(status)) {
            return status;
        }

        if (!at(token_kind::identifier) ||
            !current.identifier) {

            return fail(
                parser_failure_kind::syntax,
                "Expected record identifier");
        }

        const auto record_file =
            current.file;

        const auto record_name =
            current.identifier;

        identity_ref identity;

        status =
            identities.resolve(
                scope,
                record_name,
                identity_kind::type,
                identity);

        if (!succeeded(status)) {
            return status;
        }

        type_handle handle;

        status =
            G.declare_record(
                identity,
                kind,
                handle);

        if (!succeeded(status)) {
            return fail(
                parser_failure_kind::semantic,
                "Record declaration conflicts with existing semantic type");
        }

        status = advance();
        if (!succeeded(status)) {
            return status;
        }

        auto& bases = record_bases;
        bases.clear();

        if (at(token_kind::colon)) {
            if (kind ==
                graph_record_kind::union_type) {

                return fail(
                    parser_failure_kind::semantic,
                    "Union types cannot have base classes");
            }

            status = advance();
            if (!succeeded(status)) {
                return status;
            }

            graph_member_access base_access =
                default_access(kind);

            bool virtual_base = false;
            bool access_seen = false;
            bool virtual_seen = false;

            for (;;) {
                if (!access_seen &&
                    (at(token_kind::kw_public) ||
                     at(token_kind::kw_protected) ||
                     at(token_kind::kw_private))) {

                    if (at(token_kind::kw_public)) {
                        base_access =
                            graph_member_access::
                                public_access;
                    }
                    else if (at(token_kind::kw_protected)) {
                        base_access =
                            graph_member_access::
                                protected_access;
                    }
                    else {
                        base_access =
                            graph_member_access::
                                private_access;
                    }

                    access_seen = true;

                    status = advance();
                    if (!succeeded(status)) {
                        return status;
                    }

                    continue;
                }

                if (!virtual_seen &&
                    at(token_kind::kw_virtual)) {

                    virtual_base = true;
                    virtual_seen = true;

                    status = advance();
                    if (!succeeded(status)) {
                        return status;
                    }

                    continue;
                }

                break;
            }

            if (virtual_base) {
                return fail(
                    parser_failure_kind::unsupported,
                    "Virtual base classes are not implemented in CXX-CLASS-ABI-V1");
            }

            if (!at(token_kind::identifier) ||
                !current.identifier) {

                return fail(
                    parser_failure_kind::syntax,
                    "Expected base class type");
            }

            const auto base_identity =
                find_type_identity(
                    scope,
                    current.identifier);

            const auto base =
                G.find_type(
                    base_identity);

            type_entry base_type;

            if (!base_identity ||
                !base ||
                !read_type(
                    base,
                    base_type) ||
                !base_type.defined() ||
                base_type.record_kind ==
                    graph_record_kind::union_type) {

                return fail(
                    parser_failure_kind::semantic,
                    "Base class must name a complete non-union record type");
            }

            const auto dependency =
                sources.add_dependency(
                    base_identity);

            if (!succeeded(dependency)) {
                return dependency;
            }

            try {
                bases.push_back({
                    base,
                    base_access,
                    0,
                    0,
                });
            }
            catch (...) {
                return server_status::io_error;
            }

            status = advance();
            if (!succeeded(status)) {
                return status;
            }

            if (at(token_kind::comma)) {
                return fail(
                    parser_failure_kind::unsupported,
                    "Multiple inheritance is not implemented in CXX-CLASS-ABI-V1");
            }
        }

        if (at(token_kind::semicolon)) {
            if (!bases.empty()) {
                return fail(
                    parser_failure_kind::syntax,
                    "A base-specifier list requires a record definition");
            }

            status =
                sources.add(
                    record_file,
                    source_data_ref::type(
                        identity,
                        false));

            if (!succeeded(status)) {
                return status;
            }

            return advance();
        }

        status =
            expect(
                token_kind::l_brace,
                "Expected ';', base-specifier list, or '{' after record declaration");

        if (!succeeded(status)) {
            return status;
        }

        status = advance();
        if (!succeeded(status)) {
            return status;
        }

        // Record parsing is non-recursive. Reuse scratch storage across records
        // and roots instead of allocating several vectors per definition.
        auto& members = record_members;
        auto& pending = record_pending;
        auto& constructor_operations = record_operations;
        members.clear();
        pending.clear();
        constructor_operations.clear();

        bool constructor_seen = false;
        bool declares_virtual = false;

        const bool base_polymorphic =
            !bases.empty() &&
            record_polymorphic(
                bases.front().type);

        const auto contextual =
            [&](std::string_view value) noexcept {
                return
                    at(token_kind::identifier) &&
                    current.identifier &&
                    strings.get(
                        current.identifier) ==
                        value;
            };

        const auto parse_method_tail =
            [&](bool virtual_prefix, bool conversion = false,
                bool assignment_operator = false,
                bool subscript_operator = false) -> server_status {
                if (!at(token_kind::l_paren)) {
                    return fail(
                        parser_failure_kind::syntax,
                        "Expected '(' after method declarator");
                }

                std::size_t depth = 0;
                bool conversion_void = false;
                std::size_t parameter_tokens = 0;
                bool sole_void = false;

                for (;;) {
                    if (at(token_kind::invalid)) {
                        return fail(
                            parser_failure_kind::syntax,
                            "Method parameter list is not closed");
                    }

                    if ((assignment_operator || subscript_operator) &&
                        depth == 1 && !at(token_kind::r_paren)) {
                        if (at(token_kind::comma) || at(token_kind::ellipsis) ||
                            at(token_kind::assign)) {
                            return fail(parser_failure_kind::syntax,
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
                            return fail(parser_failure_kind::syntax,
                                "Conversion operator must have an empty parameter list");
                        }
                    }

                    if (at(token_kind::l_paren)) {
                        ++depth;
                    }
                    else if (at(token_kind::r_paren)) {
                        if (depth == 0) {
                            return fail(
                                parser_failure_kind::syntax,
                                "Unexpected ')' in method declaration");
                        }

                        --depth;
                    }

                    auto advanced = advance();
                    if (!succeeded(advanced)) {
                        return advanced;
                    }

                    if (depth == 0) {
                        break;
                    }
                }

                if ((assignment_operator || subscript_operator) &&
                    (parameter_tokens == 0 || sole_void)) {
                    return fail(parser_failure_kind::syntax,
                        "Named operator requires one parameter");
                }

                bool override_seen = false;
                bool final_seen = false;
                bool ref_qualifier_seen = false;

                for (;;) {
                    if ((conversion || assignment_operator || subscript_operator) && !ref_qualifier_seen &&
                        (at(token_kind::ampersand) || at(token_kind::logical_and))) {
                        ref_qualifier_seen = true;
                        const auto advanced = advance();
                        if (!succeeded(advanced)) {
                            return advanced;
                        }
                        continue;
                    }
                    if (at(token_kind::kw_const) ||
                        at(token_kind::kw_volatile)) {

                        const auto advanced =
                            advance();

                        if (!succeeded(advanced)) {
                            return advanced;
                        }

                        continue;
                    }

                    if (at(token_kind::kw_noexcept)) {
                        auto advanced =
                            advance();

                        if (!succeeded(advanced)) {
                            return advanced;
                        }

                        if (at(token_kind::l_paren)) {
                            std::size_t noexcept_depth = 0;

                            for (;;) {
                                if (at(token_kind::invalid)) {
                                    return fail(
                                        parser_failure_kind::syntax,
                                        "noexcept expression is not closed");
                                }

                                if (at(token_kind::l_paren)) {
                                    ++noexcept_depth;
                                }
                                else if (at(token_kind::r_paren)) {
                                    if (noexcept_depth == 0) {
                                        return fail(
                                            parser_failure_kind::syntax,
                                            "Unexpected ')' in noexcept expression");
                                    }

                                    --noexcept_depth;
                                }

                                advanced =
                                    advance();

                                if (!succeeded(advanced)) {
                                    return advanced;
                                }

                                if (noexcept_depth == 0) {
                                    break;
                                }
                            }
                        }

                        continue;
                    }

                    if (!override_seen &&
                        contextual("override")) {

                        override_seen = true;

                        const auto advanced =
                            advance();

                        if (!succeeded(advanced)) {
                            return advanced;
                        }

                        continue;
                    }

                    if (!final_seen &&
                        contextual("final")) {

                        final_seen = true;

                        const auto advanced =
                            advance();

                        if (!succeeded(advanced)) {
                            return advanced;
                        }

                        continue;
                    }

                    break;
                }

                bool pure = false;

                if (at(token_kind::assign)) {
                    auto advanced =
                        advance();

                    if (!succeeded(advanced)) {
                        return advanced;
                    }

                    if (at(token_kind::pp_number) &&
                        token_text(current) == "0") {

                        pure = true;
                    }
                    else if ((conversion || subscript_operator || !at(token_kind::kw_default)) &&
                             !at(token_kind::kw_delete)) {

                        return fail(
                            parser_failure_kind::unsupported,
                            "Method declaration supports only '= 0', '= default', or '= delete'");
                    }

                    advanced =
                        advance();

                    if (!succeeded(advanced)) {
                        return advanced;
                    }
                }

                const bool contextual_virtual =
                    override_seen ||
                    final_seen ||
                    pure;

                if (contextual_virtual &&
                    !virtual_prefix &&
                    !base_polymorphic) {

                    return fail(
                        parser_failure_kind::semantic,
                        "override/final/pure method requires a polymorphic base or explicit virtual");
                }

                if (virtual_prefix ||
                    contextual_virtual) {

                    declares_virtual = true;
                }

                if (!at(token_kind::semicolon)) {
                    return fail(
                        parser_failure_kind::unsupported,
                        "CXX-CLASS-ABI-V1 stores method declarations only; method bodies are not implemented");
                }

                return advance();
            };

        auto access =
            default_access(kind);

        while (!at(token_kind::r_brace)) {
            if (at(token_kind::invalid)) {
                return fail(
                    parser_failure_kind::syntax,
                    "Expected '}' before end of semantic root");
            }

            if (at(token_kind::semicolon)) {
                status = advance();
                if (!succeeded(status)) {
                    return status;
                }
                continue;
            }

            if (at(token_kind::kw_public) ||
                at(token_kind::kw_protected) ||
                at(token_kind::kw_private)) {

                if (at(token_kind::kw_public)) {
                    access = graph_member_access::public_access;
                }
                else if (at(token_kind::kw_protected)) {
                    access = graph_member_access::protected_access;
                }
                else {
                    access = graph_member_access::private_access;
                }

                status = advance();
                if (!succeeded(status)) {
                    return status;
                }

                status =
                    expect(
                        token_kind::colon,
                        "Expected ':' after access specifier");

                if (!succeeded(status)) {
                    return status;
                }

                status = advance();
                if (!succeeded(status)) {
                    return status;
                }
                continue;
            }

            if (at(token_kind::identifier) &&
                current.identifier == record_name) {

                semantic_token next;

                status =
                    peek(next);

                if (!succeeded(status)) {
                    return status;
                }

                if (next.kind ==
                    token_kind::l_paren) {

                    if (constructor_seen) {
                        return fail(
                            parser_failure_kind::semantic,
                            "Multiple managed default constructors are not supported");
                    }

                    constructor_seen = true;

                    status =
                        parse_constructor(
                            record_name,
                            constructor_operations);

                    if (!succeeded(status)) {
                        return status;
                    }

                    continue;
                }
            }

            bool virtual_prefix = false;

            if (at(token_kind::kw_virtual)) {
                virtual_prefix = true;

                status = advance();
                if (!succeeded(status)) {
                    return status;
                }
            }

            if (at(token_kind::tilde)) {
                status = advance();
                if (!succeeded(status)) {
                    return status;
                }

                if (!at(token_kind::identifier) ||
                    current.identifier !=
                        record_name) {

                    return fail(
                        parser_failure_kind::syntax,
                        "Destructor name must match its record");
                }

                status = advance();
                if (!succeeded(status)) {
                    return status;
                }

                status =
                    parse_method_tail(
                        virtual_prefix);

                if (!succeeded(status)) {
                    return status;
                }

                continue;
            }

            if (at(token_kind::kw_static)) {
                return fail(
                    parser_failure_kind::unsupported,
                    "Static data members and static methods are not part of the current instance ABI slice");
            }

            if (at(token_kind::kw_explicit)) {
                status = advance();
                if (!succeeded(status)) {
                    return status;
                }
                if (!at(token_kind::kw_operator)) {
                    return fail(parser_failure_kind::unsupported,
                        "explicit is supported here only on conversion operator declarations");
                }
            }

            if (at(token_kind::kw_operator)) {
                status = advance();
                if (!succeeded(status)) {
                    return status;
                }
                type_ref conversion_type;
                status = parse_type_specifier(scope, conversion_type);
                if (!succeeded(status)) {
                    return status;
                }
                while (at(token_kind::star) || at(token_kind::ampersand) ||
                       at(token_kind::logical_and)) {
                    const auto modifier = at(token_kind::star)
                        ? derived_type_kind::pointer
                        : at(token_kind::ampersand)
                            ? derived_type_kind::lvalue_reference
                            : derived_type_kind::rvalue_reference;
                    if (reference_type(conversion_type) ||
                        (modifier != derived_type_kind::pointer && is_void_type(conversion_type))) {
                        return fail(parser_failure_kind::semantic,
                            "Invalid pointer or reference conversion target");
                    }
                    type_ref wrapped;
                    status = G.derive(conversion_type, modifier, 0, wrapped);
                    if (!succeeded(status)) {
                        return status;
                    }
                    conversion_type = wrapped;
                    status = advance();
                    if (!succeeded(status)) {
                        return status;
                    }
                    if (modifier == derived_type_kind::pointer) {
                        bool const_qualified = false;
                        bool volatile_qualified = false;
                        while (at(token_kind::kw_const) || at(token_kind::kw_volatile)) {
                            const_qualified |= at(token_kind::kw_const);
                            volatile_qualified |= at(token_kind::kw_volatile);
                            status = advance();
                            if (!succeeded(status)) {
                                return status;
                            }
                        }
                        status = apply_qualifiers(const_qualified, volatile_qualified, conversion_type);
                        if (!succeeded(status)) {
                            return status;
                        }
                    }
                }
                status = parse_method_tail(virtual_prefix, true);
                if (!succeeded(status)) {
                    return status;
                }
                continue;
            }

            type_ref member_type;
            parsed_declarator declarator;

            status =
                parse_declared_type(
                    scope,
                    member_type,
                    declarator,
                    true);

            if (!succeeded(status)) {
                return status;
            }

            if (at(token_kind::l_paren)) {
                status =
                    parse_method_tail(
                        virtual_prefix, false,
                        strings.get(declarator.name) == "operator=",
                        strings.get(declarator.name) == "operator[]");

                if (!succeeded(status)) {
                    return status;
                }

                continue;
            }

            if (virtual_prefix) {
                return fail(
                    parser_failure_kind::syntax,
                    "virtual must declare a member function");
            }

            const auto name =
                declarator.name;

            for (const auto& existing : members) {
                if (existing.name == name) {
                    return fail_at(
                        parser_failure_kind::semantic,
                        "Record member name is duplicated",
                        declarator.location);
                }
            }

            pending_construction initializer;

            if (at(token_kind::assign) ||
                at(token_kind::l_brace)) {

                status =
                    parse_initializer(
                        member_type,
                        true,
                        initializer);

                if (!succeeded(status)) {
                    return status;
                }
            }

            status =
                expect(
                    token_kind::semicolon,
                    "Expected ';' after record member");

            if (!succeeded(status)) {
                return status;
            }

            try {
                members.push_back({
                    name,
                    member_type,
                    access,
                    {},
                });
                pending.push_back(initializer);
            }
            catch (...) {
                return server_status::io_error;
            }

            status = advance();
            if (!succeeded(status)) {
                return status;
            }
        }

        auto& construction = record_construction;

        try {
            construction.resize(
                members.size());
        }
        catch (...) {
            return server_status::io_error;
        }

        for (std::size_t index = 0;
             index < pending.size();
             ++index) {

            if (pending[index].kind ==
                pending_construction_kind::value) {

                if (!construction_compatible(
                        G,
                        members[index].type,
                        pending[index].value)) {

                    return fail_at(
                        parser_failure_kind::semantic,
                        "Initializer is not compatible with target type",
                        pending[index].location);
                }

                construction[index] =
                    pending[index].value;
                continue;
            }

            const auto resolved =
                resolve_reference_binding(
                    scope,
                    members,
                    members[index].type,
                    pending[index].member_name,
                    pending[index].location,
                    construction[index]);

            if (!succeeded(resolved)) {
                return resolved;
            }
        }

        status =
            normalize_constructor_operations(
                scope,
                members,
                constructor_operations,
                construction);

        if (!succeeded(status)) {
            return status;
        }

        status = advance();
        if (!succeeded(status)) {
            return status;
        }

        status =
            expect(
                token_kind::semicolon,
                "Expected ';' after record definition");

        if (!succeeded(status)) {
            return status;
        }

        status =
            G.define_record(
                handle,
                kind,
                members,
                construction,
                bases,
                declares_virtual);

        if (!succeeded(status)) {
            return fail(
                parser_failure_kind::semantic,
                "Record definition conflicts with existing semantic definition");
        }

        status =
            sources.add(
                record_file,
                source_data_ref::type(
                    identity,
                    true));

        if (!succeeded(status)) {
            return status;
        }

        return advance();
    }

    [[nodiscard]] server_status parse_object(
        identity_ref scope) noexcept {

        bool static_storage = false;
        bool inline_storage = false;

        while (at(token_kind::kw_static) ||
               at(token_kind::kw_inline)) {

            if (at(token_kind::kw_static)) {
                static_storage = true;
            }
            else {
                inline_storage = true;
            }

            const auto status =
                advance();

            if (!succeeded(status)) {
                return status;
            }
        }

        if (domain == semantic_domain::header &&
            !static_storage) {

            return fail(
                parser_failure_kind::unsupported,
                "Header namespace-scope objects require internal static storage");
        }

        if (domain == semantic_domain::source &&
            (static_storage ||
             inline_storage)) {

            return fail(
                parser_failure_kind::unsupported,
                "Source object declarations do not use C++ storage specifiers");
        }

        type_ref type;
        parsed_declarator declarator;

        auto status =
            parse_declared_type(
                scope,
                type,
                declarator);

        if (!succeeded(status)) {
            return status;
        }

        const auto object_file =
            declarator.location.file;

        const auto name =
            declarator.name;

        identity_ref identity;

        status =
            domain == semantic_domain::header
            ? resolve_internal_static_identity(
                scope,
                name,
                identity)
            : identities.resolve(
                scope,
                name,
                identity_kind::object,
                identity);

        if (!succeeded(status)) {
            return status;
        }

        std::uint32_t flags =
            domain == semantic_domain::header
            ? graph_object_internal_static
            : 0;

        construction_value initial;
        semantic_source_location
            initial_location;

        if (at(token_kind::assign)) {
            status =
                advance();

            if (!succeeded(status)) {
                return status;
            }

            if (at(token_kind::l_brace)) {
                status =
                    advance();

                if (!succeeded(status)) {
                    return status;
                }

                if (!at(token_kind::r_brace)) {
                    pending_construction parsed;

                    status =
                        parse_construction_atom(
                            type,
                            false,
                            parsed);

                    if (!succeeded(status)) {
                        return status;
                    }

                    initial =
                        parsed.value;
                    initial_location =
                        parsed.location;

                    flags |=
                        graph_object_non_default_initializer;

                    status =
                        expect(
                            token_kind::r_brace,
                            "Expected '}' after object initializer");

                    if (!succeeded(status)) {
                        return status;
                    }
                }

                status =
                    advance();

                if (!succeeded(status)) {
                    return status;
                }
            }
            else {
                pending_construction parsed;

                status =
                    parse_construction_atom(
                        type,
                        false,
                        parsed);

                if (!succeeded(status)) {
                    return status;
                }

                initial =
                    parsed.value;
                initial_location =
                    parsed.location;

                flags |=
                    graph_object_non_default_initializer;
            }
        }
        else if (at(token_kind::l_brace)) {
            status =
                advance();

            if (!succeeded(status)) {
                return status;
            }

            if (!at(token_kind::r_brace)) {
                pending_construction parsed;

                status =
                    parse_construction_atom(
                        type,
                        false,
                        parsed);

                if (!succeeded(status)) {
                    return status;
                }

                initial =
                    parsed.value;
                initial_location =
                    parsed.location;

                flags |=
                    graph_object_non_default_initializer;

                status =
                    expect(
                        token_kind::r_brace,
                        "Expected '}' after object initializer");

                if (!succeeded(status)) {
                    return status;
                }
            }

            status =
                advance();

            if (!succeeded(status)) {
                return status;
            }
        }

        if ((flags &
                graph_object_non_default_initializer) !=
                    0 &&
            !construction_compatible(
                G,
                type,
                initial)) {

            return fail_at(
                parser_failure_kind::semantic,
                "Object initializer is not compatible with target type",
                initial_location);
        }

        status =
            expect(
                token_kind::semicolon,
                "Expected ';' after object declaration");

        if (!succeeded(status)) {
            return status;
        }

        object_handle object;

        status =
            G.add_object(
                identity,
                type,
                object,
                flags,
                initial);

        if (!succeeded(status)) {
            return fail(
                parser_failure_kind::semantic,
                "Object declaration conflicts with existing semantic object");
        }

        status =
            sources.add(
                object_file,
                source_data_ref::object(
                    identity));

        if (!succeeded(status)) {
            return status;
        }

        return advance();
    }

    [[nodiscard]] server_status endpoint(
        identity_ref scope,
        resolved_link_endpoint& output) noexcept {

        output = {};
        endpoint_steps.clear();

        member_index direct_member;

        if (!at(token_kind::identifier) ||
            !current.identifier) {

            return fail(
                parser_failure_kind::syntax,
                "Expected object identifier in link endpoint");
        }

        const auto object_location =
            current_location();

        const auto object_identity =
            find_object_identity(
                scope,
                current.identifier);

        if (!object_identity) {
            return fail(
                parser_failure_kind::semantic,
                "Link endpoint object is not visible");
        }

        const auto object =
            G.find_object(
                object_identity);

        object_entry object_value;

        if (!object ||
            !read_object(
                object,
                object_value)) {

            return fail(
                parser_failure_kind::semantic,
                "Link endpoint object is not present in G");
        }

        auto dependency =
            sources.add_dependency(
                object_identity);

        if (!succeeded(dependency)) {
            return dependency;
        }

        auto current_type =
            object_value.type;

        auto status = advance();

        if (!succeeded(status)) {
            return status;
        }

        const auto strip_cv =
            [this](type_ref& type) noexcept {
                derived_type_record derived;

                while (G.derived(
                           type,
                           derived) &&
                       (derived.kind ==
                            derived_type_kind::const_qualified ||
                        derived.kind ==
                            derived_type_kind::volatile_qualified)) {

                    type =
                        derived.child;
                }
            };

        for (;;) {
            if (at(token_kind::l_bracket)) {
                const auto bracket_location =
                    current_location();

                auto indexed_type =
                    current_type;

                strip_cv(
                    indexed_type);

                derived_type_record array;

                if (!G.derived(
                        indexed_type,
                        array) ||
                    array.kind !=
                        derived_type_kind::bounded_array) {

                    return fail_at(
                        parser_failure_kind::semantic,
                        "Array index is applied to a non-array link subobject",
                        bracket_location);
                }

                status = advance();

                if (!succeeded(status)) {
                    return status;
                }

                if (!at(token_kind::pp_number)) {
                    return fail(
                        parser_failure_kind::unsupported,
                        "Array link index requires a decimal integer literal");
                }

                const auto index_location =
                    current_location();

                const auto spelling =
                    token_text(current);

                std::uint64_t index = 0;

                const auto converted =
                    std::from_chars(
                        spelling.data(),
                        spelling.data() +
                            spelling.size(),
                        index);

                if (converted.ec !=
                        std::errc{} ||
                    converted.ptr !=
                        spelling.data() +
                            spelling.size()) {

                    return fail_at(
                        parser_failure_kind::unsupported,
                        "Only decimal integer array link indices are supported",
                        index_location);
                }

                if (index >=
                    array.payload) {

                    return fail_at(
                        parser_failure_kind::semantic,
                        "Array link index is outside the declared bound",
                        index_location);
                }

                status = advance();

                if (!succeeded(status)) {
                    return status;
                }

                if (!at(token_kind::r_bracket)) {
                    return fail(
                        parser_failure_kind::unsupported,
                        "Array link index expressions are not implemented");
                }

                status = advance();

                if (!succeeded(status)) {
                    return status;
                }

                direct_member = {};

                try {
                    endpoint_steps.push_back({
                        index,
                        endpoint_path_step_kind::
                            array_index,
                        {},
                    });
                }
                catch (...) {
                    return server_status::io_error;
                }

                current_type =
                    array.child;

                output.location =
                    index_location;

                continue;
            }

            if (at(token_kind::dot)) {
                const auto dot_location =
                    current_location();

                auto record_type =
                    current_type;

                strip_cv(
                    record_type);

                type_handle record;

                if (!G.named(
                        record_type,
                        record)) {

                    return fail_at(
                        parser_failure_kind::unsupported,
                        "Link member selection requires a direct record subobject",
                        dot_location);
                }

                dependency =
                    sources.add_dependency(
                        G.identity(record));

                if (!succeeded(dependency)) {
                    return dependency;
                }

                status = advance();

                if (!succeeded(status)) {
                    return status;
                }

                if (!at(token_kind::identifier) ||
                    !current.identifier) {

                    return fail(
                        parser_failure_kind::syntax,
                        "Expected member identifier after '.' in link endpoint");
                }

                const auto member_location =
                    current_location();

                const auto member =
                    G.find_member(
                        record,
                        current.identifier);

                if (!member) {
                    return fail_at(
                        parser_failure_kind::semantic,
                        "Link endpoint member is not visible",
                        member_location);
                }

                member_record member_value;

                if (!read_member(
                        record,
                        member,
                        member_value)) {

                    return fail_at(
                        parser_failure_kind::semantic,
                        "Link endpoint member is not present in G",
                        member_location);
                }

                if (endpoint_steps.empty()) {
                    direct_member =
                        member;
                }
                else {
                    direct_member = {};
                }

                try {
                    endpoint_steps.push_back({
                        member.value(),
                        endpoint_path_step_kind::
                            member,
                        {},
                    });
                }
                catch (...) {
                    return server_status::io_error;
                }

                current_type =
                    member_value.type;

                output.location =
                    member_location;

                status = advance();

                if (!succeeded(status)) {
                    return status;
                }

                continue;
            }

            break;
        }

        if (endpoint_steps.empty()) {
            return fail_at(
                parser_failure_kind::syntax,
                "Link endpoint must select a member or bounded-array subobject",
                object_location);
        }

        output.type =
            current_type;

        if (endpoint_steps.size() == 1 &&
            endpoint_steps.front().kind ==
                endpoint_path_step_kind::member) {

            const auto raw =
                endpoint_steps.front().value;

            if (!direct_member ||
                direct_member.value() !=
                    raw) {

                return fail_at(
                    parser_failure_kind::semantic,
                    "Direct link member could not be normalized",
                    output.location);
            }

            output.endpoint = {
                object,
                direct_member,
            };

            return server_status::success;
        }

        endpoint_path_handle path;
        type_ref resolved;

        status =
            G.intern_endpoint_path(
                object_value.type,
                endpoint_steps,
                path,
                &resolved);

        if (!succeeded(status) ||
            !path ||
            resolved !=
                current_type) {

            return succeeded(status)
                ? fail_at(
                    parser_failure_kind::semantic,
                    "Link subobject path could not be normalized",
                    output.location)
                : status;
        }

        output.endpoint = {
            object,
            endpoint_ref::from_path(
                path),
        };

        return server_status::success;
    }

    [[nodiscard]] server_status parse_source_assignment(
        identity_ref scope) noexcept {

        if (domain != semantic_domain::source) {
            return fail(
                parser_failure_kind::unsupported,
                "Source assignments are supported only in Source inputs");
        }

        const auto statement_file =
            current.file;

        resolved_link_endpoint target;

        auto status =
            endpoint(
                scope,
                target);

        if (!succeeded(status)) {
            return status;
        }

        status =
            expect(
                token_kind::assign,
                "Expected '=' after Source target endpoint");

        if (!succeeded(status)) {
            return status;
        }

        status = advance();

        if (!succeeded(status)) {
            return status;
        }

        type_ref target_referent;

        if (reference_referent(
                target.type,
                target_referent)) {

            resolved_link_endpoint source;

            status =
                endpoint(
                    scope,
                    source);

            if (!succeeded(status)) {
                return status;
            }

            if (!reference_binding_compatible(
                    target.type,
                    source.type)) {

                return fail_at(
                    parser_failure_kind::semantic,
                    "Link source type does not match target reference type",
                    source.location);
            }

            status =
                expect(
                    token_kind::semicolon,
                    "Expected ';' after link");

            if (!succeeded(status)) {
                return status;
            }

            link_handle link;

            status =
                G.add_link(
                    source.endpoint,
                    target.endpoint,
                    link);

            if (!succeeded(status)) {
                return fail(
                    parser_failure_kind::semantic,
                    "Link conflicts with an existing binding for the target endpoint");
            }

            status =
                sources.add(
                    statement_file,
                    source_data_ref::link(
                        link));

            if (!succeeded(status)) {
                return status;
            }

            return advance();
        }

        if (at(token_kind::identifier) ||
            at(token_kind::kw_this)) {

            return fail(
                parser_failure_kind::unsupported,
                "Source value initialization requires a scalar constant");
        }

        pending_construction initial;

        status =
            parse_construction_atom(
                target.type,
                false,
                initial);

        if (!succeeded(status)) {
            return status;
        }

        if (initial.kind !=
                pending_construction_kind::value ||
            !construction_compatible(
                G,
                target.type,
                initial.value)) {

            return fail_at(
                parser_failure_kind::semantic,
                "Source value initializer is not compatible with target type",
                initial.location);
        }

        status =
            expect(
                token_kind::semicolon,
                "Expected ';' after Source value initialization");

        if (!succeeded(status)) {
            return status;
        }

        bool replaced = false;

        status =
            G.add_initialization(
                target.endpoint,
                initial.value,
                replaced);

        if (!succeeded(status)) {
            return fail_at(
                parser_failure_kind::semantic,
                "Source value initialization target must be a writable scalar non-reference subobject",
                target.location);
        }

        status =
            sources.add_initialization(
                target.endpoint);

        if (!succeeded(status)) {
            return status;
        }

        const auto object_identity =
            G.identity(
                target.endpoint.object);

        if (!object_identity) {
            return fail_at(
                parser_failure_kind::semantic,
                "Source value initialization object has no semantic identity",
                target.location);
        }

        status =
            sources.add(
                statement_file,
                source_data_ref::object(
                    object_identity));

        if (!succeeded(status)) {
            return status;
        }

        if (replaced) {
            status =
                warn_at(
                    parser_warning_kind::
                        duplicate_initialization,
                    "Object member is initialized more than once; the last initialization is used",
                    target.location);

            if (!succeeded(status)) {
                return status;
            }
        }

        return advance();
    }

    [[nodiscard]] server_status parse_scope(
        identity_ref scope,
        bool expect_close,
        std::size_t scope_depth) noexcept {

        for (;;) {
            if (at(token_kind::invalid)) {
                return expect_close
                    ? fail(
                        parser_failure_kind::syntax,
                        "Semantic scope is not closed before end of input")
                    : server_status::success;
            }

            if (expect_close &&
                at(token_kind::r_brace)) {

                return server_status::success;
            }

            if (at(token_kind::semicolon)) {
                const auto status = advance();
                if (!succeeded(status)) {
                    return status;
                }
                continue;
            }

            if (at(token_kind::kw_namespace)) {
                const auto status =
                    parse_namespace(
                        scope,
                        scope_depth);

                if (!succeeded(status)) {
                    return status;
                }
                continue;
            }

            if (at(token_kind::kw_struct) ||
                at(token_kind::kw_class) ||
                at(token_kind::kw_union)) {

                const auto status =
                    parse_record(scope);

                if (!succeeded(status)) {
                    return status;
                }
                continue;
            }

            if (domain == semantic_domain::source &&
                at(token_kind::identifier) &&
                current.identifier &&
                find_object_identity(
                    scope,
                    current.identifier)) {

                const auto status =
                    parse_source_assignment(
                        scope);

                if (!succeeded(status)) {
                    return status;
                }
                continue;
            }

            if (builtin_start(current.kind) ||
                at(token_kind::kw_const) ||
                at(token_kind::kw_volatile) ||
                at(token_kind::kw_static) ||
                at(token_kind::kw_inline) ||
                at(token_kind::identifier)) {

                const auto status =
                    parse_object(scope);

                if (!succeeded(status)) {
                    return status;
                }
                continue;
            }

            return fail(
                parser_failure_kind::unsupported,
                "Declaration is outside the current direct Parser/Semantic slice");
        }
    }

    file_context& files;
    semantic_input input;
    string_table& strings;
    identity_space& identities;
    Graph& G;
    Sources& sources;
    parser_failure* failure = nullptr;
    std::vector<parser_warning>* warnings = nullptr;
    semantic_domain domain =
        semantic_domain::header;
    file_id semantic_root{};
    string_id internal_static_scope_name{};
    semantic_token current;
    semantic_token buffered;
    bool has_buffered = false;

    std::vector<declarator_modifier> declarator_modifiers;
    std::vector<endpoint_path_step> endpoint_steps;
    std::vector<base_record> record_bases;
    std::vector<member_record> record_members;
    std::vector<pending_construction> record_pending;
    std::vector<constructor_operation> record_operations;
    std::vector<construction_value> record_construction;
    std::vector<bool> record_assigned;
};

}

server_status parse_semantic_project(
    file_context& files,
    lexical_generation& lexical,
    std::size_t frontend_root_count,
    const preprocessor_configuration& configuration,
    string_table& strings,
    identity_space& identities,
    graph& G,
    source_map& sources,
    parser_failure* failure,
    std::vector<parser_warning>* warnings) noexcept {

    if (failure != nullptr) {
        *failure = {};
    }

    if (warnings != nullptr) {
        warnings->clear();
    }

    if (frontend_root_count >
        files.size()) {

        return server_status::project_configuration_invalid;
    }

    const auto source_reset =
        sources.reset(
            files.size());

    if (!succeeded(source_reset)) {
        return source_reset;
    }

    semantic_parser<graph, source_map> parser{
        files,
        lexical,
        configuration,
        strings,
        identities,
        G,
        sources,
        failure,
        warnings};

    for (const auto domain :
         {semantic_domain::header,
          semantic_domain::source}) {

        const auto expected_kind =
            domain == semantic_domain::header
            ? file_kind::header
            : file_kind::source;

        for (std::size_t index = 0;
             index < frontend_root_count;
             ++index) {

            const file_id root{
                static_cast<std::uint32_t>(
                    index + 1)};

            if (files.kind(root) !=
                expected_kind) {

                continue;
            }

            const auto parsed =
                parser.parse(
                    root,
                    domain);

            if (!succeeded(parsed)) {
                return parsed;
            }
        }
    }

    return sources.finalize(files.size(), identities, G);
}

server_status parse_semantic_roots(
    file_context& files,
    lexical_generation& lexical,
    std::span<const file_id> roots,
    const preprocessor_configuration& configuration,
    string_table& strings,
    identity_space& identities,
    graph_delta& G,
    source_map_delta& sources,
    parser_failure* failure,
    std::vector<parser_warning>* warnings) noexcept {

    if (failure != nullptr) {
        *failure = {};
    }

    if (warnings != nullptr) {
        warnings->clear();
    }

    file_id previous;

    for (const auto root : roots) {
        if (!root ||
            !files.contains(root) ||
            (previous &&
             root.value() <=
                previous.value())) {

            return server_status::
                project_configuration_invalid;
        }

        const auto kind =
            files.kind(root);

        if (kind != file_kind::header &&
            kind != file_kind::source) {

            return server_status::
                project_configuration_invalid;
        }

        previous =
            root;
    }

    const auto source_reset =
        sources.reset();

    if (!succeeded(source_reset)) {
        return source_reset;
    }

    semantic_parser<
        graph_delta,
        source_map_delta>
        parser{
            files,
            lexical,
            configuration,
            strings,
            identities,
            G,
            sources,
            failure,
            warnings};

    for (const auto domain :
         {semantic_domain::header,
          semantic_domain::source}) {

        const auto expected_kind =
            domain ==
                semantic_domain::header
            ? file_kind::header
            : file_kind::source;

        for (const auto root : roots) {
            if (files.kind(root) !=
                expected_kind) {

                continue;
            }

            const auto parsed =
                parser.parse(
                    root,
                    domain);

            if (!succeeded(parsed)) {
                return parsed;
            }
        }
    }

    return server_status::success;
}

}
