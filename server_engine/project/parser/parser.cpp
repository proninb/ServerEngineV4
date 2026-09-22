#include "parser.hpp"

#include "../frontend/semantic_input.hpp"

#include <bit>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string_view>
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

enum class pending_construction_kind : std::uint8_t {
    value,
    member_name,
};

struct pending_construction final {
    construction_value value{};
    string_id member_name{};
    pending_construction_kind kind =
        pending_construction_kind::value;
};

struct constructor_operation final {
    string_id target{};
    pending_construction expression;
};

class semantic_parser final {
public:
    semantic_parser(
        file_context& files_value,
        lexical_generation& lexical,
        const preprocessor_configuration& configuration,
        string_table& strings,
        identity_space& identities,
        graph& G,
        parser_failure* failure) noexcept
        : files(files_value),
          input(
              files_value,
              lexical,
              configuration,
              strings),
          identities(identities),
          G(G),
          failure(failure) {
    }

    [[nodiscard]] server_status parse(
        file_id root) noexcept {

        if (failure != nullptr) {
            *failure = {};
        }

        const auto started =
            input.start(root);

        if (!succeeded(started)) {
            return input_failure(started);
        }

        const auto advanced =
            advance();

        if (!succeeded(advanced)) {
            return advanced;
        }

        return parse_scope(
            identities.root(),
            false);
    }

private:
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

        if (failure != nullptr) {
            failure->kind = kind;
            failure->file = current.file;
            failure->source = {
                current.source_offset,
                current.source_length,
            };
            failure->detail = detail;
        }

        return server_status::project_configuration_invalid;
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

            const auto* record =
                identities.record(
                    current_scope);

            if (record == nullptr) {
                break;
            }

            current_scope =
                record->parent;
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

            const auto* record =
                identities.record(
                    current_scope);

            if (record == nullptr) {
                break;
            }

            current_scope =
                record->parent;
        }

        return {};
    }

    [[nodiscard]] bool reference_type(
        type_ref type) const noexcept {

        derived_type_record derived;

        while (G.derived(type, derived)) {
            if (derived.kind ==
                    derived_type_kind::lvalue_reference ||
                derived.kind ==
                    derived_type_kind::rvalue_reference) {

                return true;
            }

            type = derived.child;
        }

        return false;
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

    [[nodiscard]] server_status parse_type(
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

        if (builtin_start(current.kind)) {
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

        const auto qualified =
            apply_qualifiers(
                const_qualified,
                volatile_qualified,
                output);

        if (!succeeded(qualified)) {
            return qualified;
        }

        for (;;) {
            derived_type_kind modifier;

            if (at(token_kind::star)) {
                modifier = derived_type_kind::pointer;
            }
            else if (at(token_kind::ampersand)) {
                modifier = derived_type_kind::lvalue_reference;
            }
            else if (at(token_kind::logical_and)) {
                modifier = derived_type_kind::rvalue_reference;
            }
            else {
                break;
            }

            const auto advanced = advance();
            if (!succeeded(advanced)) {
                return advanced;
            }

            type_ref wrapped;
            const auto derived =
                G.derive(
                    output,
                    modifier,
                    0,
                    wrapped);

            if (!succeeded(derived)) {
                return derived;
            }

            output = wrapped;
        }

        return server_status::success;
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
        pending_construction expression) noexcept {

        if (!target) {
            return fail(
                parser_failure_kind::syntax,
                "Managed constructor target is invalid");
        }

        try {
            operations.push_back({
                target,
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
        const std::vector<member_record>& members,
        const std::vector<constructor_operation>& operations,
        std::vector<construction_value>& construction) noexcept {

        std::vector<bool> assigned;

        try {
            assigned.resize(
                members.size());
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
                return fail(
                    parser_failure_kind::syntax,
                    "Constructor target is not a field of this record");
            }

            construction_value value;

            if (reference_type(
                    members[target_index].type)) {

                if (operation.expression.kind !=
                        pending_construction_kind::member_name ||
                    !operation.expression.member_name) {

                    return fail(
                        parser_failure_kind::unsupported,
                        "Reference constructor operation requires a local member binding");
                }

                std::size_t source_index = 0;

                for (;
                     source_index < members.size();
                     ++source_index) {

                    if (members[source_index].name ==
                        operation.expression.member_name) {

                        break;
                    }
                }

                if (source_index == members.size() ||
                    source_index >=
                        (std::numeric_limits<std::uint32_t>::max)()) {

                    return fail(
                        parser_failure_kind::semantic,
                        "Constructor reference binding does not name a member of this record");
                }

                value =
                    construction_value::member_binding(
                        static_cast<std::uint32_t>(
                            source_index + 1));
            }
            else {
                if (operation.expression.kind !=
                        pending_construction_kind::value) {

                    return fail(
                        parser_failure_kind::unsupported,
                        "Value constructor operation requires a scalar constant");
                }

                value =
                    operation.expression.value;
            }

            if (assigned[target_index] &&
                construction[target_index].kind ==
                    construction_kind::member_binding &&
                construction[target_index] != value) {

                return fail(
                    parser_failure_kind::syntax,
                    "Conflicting constructor reference bindings");
            }

            construction[target_index] =
                value;
            assigned[target_index] =
                true;
        }

        return server_status::success;
    }

    [[nodiscard]] server_status parse_namespace(
        identity_ref parent) noexcept {

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
                true);

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

        if (at(token_kind::semicolon)) {
            return advance();
        }

        status =
            expect(
                token_kind::l_brace,
                "Expected ';' or '{' after record declaration");

        if (!succeeded(status)) {
            return status;
        }

        status = advance();
        if (!succeeded(status)) {
            return status;
        }

        std::vector<member_record> members;
        std::vector<pending_construction> pending;
        std::vector<constructor_operation> constructor_operations;

        bool constructor_seen = false;

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

            if (at(token_kind::kw_static)) {
                return fail(
                    parser_failure_kind::unsupported,
                    "Static data members are not part of the current instance-member Graph slice");
            }

            type_ref member_type;

            status =
                parse_type(
                    scope,
                    member_type);

            if (!succeeded(status)) {
                return status;
            }

            if (!at(token_kind::identifier) ||
                !current.identifier) {

                return fail(
                    parser_failure_kind::syntax,
                    "Expected record member identifier");
            }

            const auto name =
                current.identifier;

            for (const auto& existing : members) {
                if (existing.name == name) {
                    return fail(
                        parser_failure_kind::semantic,
                        "Record member name is duplicated");
                }
            }

            status = advance();
            if (!succeeded(status)) {
                return status;
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

        std::vector<construction_value> construction;

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

                construction[index] =
                    pending[index].value;
                continue;
            }

            std::size_t target = 0;

            for (;
                 target < members.size();
                 ++target) {

                if (members[target].name ==
                    pending[index].member_name) {

                    break;
                }
            }

            if (target == members.size() ||
                target >=
                    (std::numeric_limits<std::uint32_t>::max)()) {

                return fail(
                    parser_failure_kind::semantic,
                    "Reference member initializer does not name a member of this record");
            }

            construction[index] =
                construction_value::member_binding(
                    static_cast<std::uint32_t>(
                        target + 1));
        }

        status =
            normalize_constructor_operations(
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
                construction);

        if (!succeeded(status)) {
            return fail(
                parser_failure_kind::semantic,
                "Record definition conflicts with existing semantic definition");
        }

        return advance();
    }

    [[nodiscard]] server_status parse_object(
        identity_ref scope) noexcept {

        // Declaration/storage specifiers do not participate in identity_ref.
        while (at(token_kind::kw_static) ||
               at(token_kind::kw_inline)) {

            const auto status =
                advance();

            if (!succeeded(status)) {
                return status;
            }
        }

        type_ref type;

        auto status =
            parse_type(
                scope,
                type);

        if (!succeeded(status)) {
            return status;
        }

        if (!at(token_kind::identifier) ||
            !current.identifier) {

            return fail(
                parser_failure_kind::syntax,
                "Expected object identifier");
        }

        const auto name =
            current.identifier;

        identity_ref identity;

        status =
            identities.resolve(
                scope,
                name,
                identity_kind::object,
                identity);

        if (!succeeded(status)) {
            return status;
        }

        status =
            advance();

        if (!succeeded(status)) {
            return status;
        }

        std::uint32_t construction_flags = 0;

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
                    pending_construction ignored;

                    status =
                        parse_construction_atom(
                            type,
                            false,
                            ignored);

                    if (!succeeded(status)) {
                        return status;
                    }

                    construction_flags |=
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
                pending_construction ignored;

                status =
                    parse_construction_atom(
                        type,
                        false,
                        ignored);

                if (!succeeded(status)) {
                    return status;
                }

                construction_flags |=
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
                pending_construction ignored;

                status =
                    parse_construction_atom(
                        type,
                        false,
                        ignored);

                if (!succeeded(status)) {
                    return status;
                }

                construction_flags |=
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
                construction_flags);

        if (!succeeded(status)) {
            return fail(
                parser_failure_kind::semantic,
                "Object declaration conflicts with existing semantic object");
        }

        return advance();
    }

    [[nodiscard]] server_status endpoint(
        identity_ref scope,
        object_endpoint& output) noexcept {

        output = {};

        if (!at(token_kind::identifier) ||
            !current.identifier) {

            return fail(
                parser_failure_kind::syntax,
                "Expected object identifier in link endpoint");
        }

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

        const auto* object_value =
            G.find(object);

        if (!object ||
            object_value == nullptr) {

            return fail(
                parser_failure_kind::semantic,
                "Link endpoint object is not present in G");
        }

        auto status = advance();
        if (!succeeded(status)) {
            return status;
        }

        status =
            expect(
                token_kind::dot,
                "Expected '.' in link endpoint");

        if (!succeeded(status)) {
            return status;
        }

        status = advance();
        if (!succeeded(status)) {
            return status;
        }

        if (!at(token_kind::identifier) ||
            !current.identifier) {

            return fail(
                parser_failure_kind::syntax,
                "Expected member identifier in link endpoint");
        }

        auto object_type =
            object_value->type;

        derived_type_record derived;

        while (G.derived(
                object_type,
                derived)) {

            if (derived.kind !=
                    derived_type_kind::const_qualified &&
                derived.kind !=
                    derived_type_kind::volatile_qualified) {

                return fail(
                    parser_failure_kind::unsupported,
                    "Link endpoints require direct named record objects");
            }

            object_type = derived.child;
        }

        type_handle record;

        if (!G.named(
                object_type,
                record)) {

            return fail(
                parser_failure_kind::unsupported,
                "Link endpoints require direct named record objects");
        }

        const auto member =
            G.find_member(
                record,
                current.identifier);

        if (!member) {
            return fail(
                parser_failure_kind::semantic,
                "Link endpoint member is not visible");
        }

        output = {
            object,
            member,
        };

        return advance();
    }

    [[nodiscard]] server_status parse_link(
        identity_ref scope) noexcept {

        object_endpoint target;

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
                "Expected '=' between link endpoints");

        if (!succeeded(status)) {
            return status;
        }

        status = advance();
        if (!succeeded(status)) {
            return status;
        }

        object_endpoint source;

        status =
            endpoint(
                scope,
                source);

        if (!succeeded(status)) {
            return status;
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
                source,
                target,
                link);

        if (!succeeded(status)) {
            return fail(
                parser_failure_kind::semantic,
                "Link conflicts with an existing binding for the target endpoint");
        }

        return advance();
    }

    [[nodiscard]] server_status parse_scope(
        identity_ref scope,
        bool expect_close) noexcept {

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
                    parse_namespace(scope);

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

            if (at(token_kind::identifier) &&
                current.identifier &&
                find_object_identity(
                    scope,
                    current.identifier)) {

                const auto status =
                    parse_link(scope);

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
    identity_space& identities;
    graph& G;
    parser_failure* failure = nullptr;
    semantic_token current;
    semantic_token buffered;
    bool has_buffered = false;
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
    parser_failure* failure) noexcept {

    if (failure != nullptr) {
        *failure = {};
    }

    if (frontend_root_count >
        files.size()) {

        return server_status::project_configuration_invalid;
    }

    semantic_parser parser{
        files,
        lexical,
        configuration,
        strings,
        identities,
        G,
        failure};

    for (std::size_t index = 0;
         index < frontend_root_count;
         ++index) {

        const file_id root{
            static_cast<std::uint32_t>(
                index + 1)};

        const auto kind =
            files.kind(root);

        if (kind != file_kind::header &&
            kind != file_kind::source) {

            continue;
        }

        const auto parsed =
            parser.parse(root);

        if (!succeeded(parsed)) {
            return parsed;
        }
    }

    return server_status::success;
}

}
