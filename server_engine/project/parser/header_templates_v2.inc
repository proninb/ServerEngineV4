// Member implementations included by header_parser_v2.cpp inside cw::server.
server_status header_parser_v2::parse_prepared_type(std::span<const prepared_token> tokens,
    identity_ref scope, type_ref& output) noexcept {
    try {
        template_replay replay;
        replay.tokens.assign(tokens.begin(), tokens.end());
        template_replays.push_back(std::move(replay));
        struct pop { std::vector<template_replay>& stack; ~pop() { stack.pop_back(); } } guard{template_replays};
        resolved_type type;
        const auto status = parse_type(scope, type);
        if (!succeeded(status)) return status;
        if (current()) return fail(parser_v2_failure_kind::syntax, "Unexpected token after template type");
        output = type.type;
        return server_status::success;
    } catch (...) { return server_status::io_error; }
}

bool header_parser_v2::template_type_key(type_ref type, std::string& output, unsigned depth) const {
    if (depth > 128 || output.size() > 65536) return false;
    intrinsic_type intrinsic;
    if (G.intrinsic(type, intrinsic)) {
        static constexpr std::string_view names[] = {"?", "void", "bool", "char", "signed char", "unsigned char",
            "wchar_t", "char8_t", "char16_t", "char32_t", "short", "unsigned short", "int", "unsigned int",
            "long", "unsigned long", "long long", "unsigned long long", "float", "double", "long double", "nullptr_t"};
        const auto index = static_cast<std::size_t>(intrinsic);
        if (index >= std::size(names)) return false;
        output += names[index];
        return true;
    }
    derived_type_record derived;
    if (G.derived(type, derived)) {
        static constexpr std::string_view names[] = {"const(", "volatile(", "ptr(", "lref(", "rref(", "array(", "unsized("};
        const auto index = static_cast<std::size_t>(derived.kind);
        if (index >= std::size(names)) return false;
        output += names[index];
        if (!template_type_key(derived.child, output, depth + 1)) return false;
        if (derived.kind == derived_type_kind::bounded_array) output += ',' + std::to_string(derived.payload);
        output += ')';
        return true;
    }
    type_handle handle;
    if (!G.named(type, handle)) return false;
    std::vector<string_id> parts;
    for (auto id = G.identity(handle); id && id != identities.root();) {
        identity_record record;
        if (!identities.record(id, record)) return false;
        parts.push_back(record.name);
        id = record.parent;
    }
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        output += "::";
        output += input.semantic_strings().get(*it);
    }
    return true;
}

server_status header_parser_v2::expect_template_close() noexcept {
    if (at(token_kind::shift_right)) {
        auto token = *current();
        const auto status = consume();
        if (!succeeded(status)) return status;
        token.kind = token_kind::greater;
        split_greater = token;
        has_split_greater = true;
        return server_status::success;
    }
    return expect(token_kind::greater, "Expected '>' after template arguments");
}

// Checked signed integer subset: unary +/- and binary +,-,*,/,% with
// parentheses. Delimiters such as comma, '>' and ']' are not consumed.
server_status header_parser_v2::parse_integer_expression(std::int64_t& output, int minimum, unsigned depth) noexcept {
    if (depth > 128)
        return fail(parser_v2_failure_kind::unsupported, "Constant expression nesting limit exceeded");
    auto status = server_status::success;
    if (at(token_kind::plus) || at(token_kind::minus)) {
        const bool negative = at(token_kind::minus);
        status = consume();
        if (!succeeded(status)) return status;
        status = parse_integer_expression(output, 30, depth + 1);
        if (!succeeded(status)) return status;
        if (negative) {
            if (output == (std::numeric_limits<std::int64_t>::min)())
                return fail(parser_v2_failure_kind::semantic, "Constant expression negation overflows");
            output = -output;
        }
    } else if (at(token_kind::l_paren)) {
        status = consume();
        if (!succeeded(status)) return status;
        status = parse_integer_expression(output, 0, depth + 1);
        if (!succeeded(status)) return status;
        status = expect(token_kind::r_paren, "Expected ')' in constant expression");
        if (!succeeded(status)) return status;
    } else {
        const auto* token = current();
        if (!token || token->kind != token_kind::pp_number ||
            token->number.kind != prepared_number_kind_v2::unsigned_integer)
            return fail(parser_v2_failure_kind::unsupported, "Expected an integer constant expression");
        if (token->number.bits > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
            return fail(parser_v2_failure_kind::semantic, "Integer constant exceeds signed expression range");
        output = static_cast<std::int64_t>(token->number.bits);
        status = consume();
        if (!succeeded(status)) return status;
    }
    return parse_integer_expression_tail(output, minimum, depth);
}

server_status header_parser_v2::parse_integer_expression_tail(std::int64_t& output, int minimum, unsigned depth) noexcept {
    server_status status = server_status::success;
    for (;;) {
        const auto* token = current();
        if (!token) break;
        const auto op = token->kind;
        const int precedence = op == token_kind::star || op == token_kind::slash || op == token_kind::percent ? 20 :
            op == token_kind::plus || op == token_kind::minus ? 10 : -1;
        if (precedence < minimum) break;
        status = consume();
        if (!succeeded(status)) return status;
        std::int64_t right = 0;
        status = parse_integer_expression(right, precedence + 1, depth + 1);
        if (!succeeded(status)) return status;
        constexpr auto low = (std::numeric_limits<std::int64_t>::min)();
        constexpr auto high = (std::numeric_limits<std::int64_t>::max)();
        bool overflow = false;
        switch (op) {
        case token_kind::plus:
            overflow = right > 0 ? output > high - right : right < 0 && output < low - right;
            if (!overflow) output += right;
            break;
        case token_kind::minus:
            overflow = right < 0 ? output > high + right : right > 0 && output < low + right;
            if (!overflow) output -= right;
            break;
        case token_kind::star:
            if (output > 0) overflow = right > 0 ? output > high / right : right < low / output;
            else if (output < 0) overflow = right > 0 ? output < low / right : right < 0 && output < high / right;
            if (!overflow) output *= right;
            break;
        default:
            if (right == 0) return fail(parser_v2_failure_kind::semantic, "Division by zero in constant expression");
            overflow = output == low && right == -1;
            if (!overflow) output = op == token_kind::slash ? output / right : output % right;
            break;
        }
        if (overflow) return fail(parser_v2_failure_kind::semantic, "Constant expression overflows signed 64-bit range");
    }
    return server_status::success;
}

server_status header_parser_v2::parse_using(identity_ref scope) noexcept {
    auto status = consume();
    if (!succeeded(status)) return status;
    if (!at(token_kind::identifier))
        return fail(parser_v2_failure_kind::unsupported, "Expected a using alias declaration");
    const auto name = current()->identifier;
    const auto file = current()->file;
    const source_range alias_source{current()->source_offset, current()->source_length};
    status = consume();
    if (!succeeded(status)) return status;
    bit_rule bits;
    if (at(token_kind::l_bracket)) {
        status = consume();
        if (!succeeded(status)) return status;
        status = expect(token_kind::l_bracket, "Expected '[[' for cw::bits");
        if (!succeeded(status)) return status;
        if (!at(token_kind::identifier) || input.spelling(*current()) != "cw")
            return fail(parser_v2_failure_kind::syntax, "Expected cw::bits attribute");
        status = consume();
        if (!succeeded(status)) return status;
        status = expect(token_kind::scope, "Expected '::' in cw::bits");
        if (!succeeded(status)) return status;
        if (!at(token_kind::identifier) || input.spelling(*current()) != "bits")
            return fail(parser_v2_failure_kind::syntax, "Expected cw::bits attribute");
        status = consume();
        if (!succeeded(status)) return status;
        status = expect(token_kind::l_paren, "Expected '(' after cw::bits");
        if (!succeeded(status)) return status;
        resolved_type result;
        status = parse_type(scope, result);
        if (!succeeded(status)) return status;
        if (result.type != G.intrinsic(intrinsic_type::bool_type)) {
            type_handle handle;
            const auto* entry = G.named(result.type, handle) ? G.find(handle) : nullptr;
            if (!entry || entry->kind == graph_type_kind::enumeration ||
                entry->storage_intrinsic() != intrinsic_type::bool_type)
                return fail(parser_v2_failure_kind::semantic, "cw::bits result type must be bool or a bool alias");
            bits.result_alias = G.identity(handle);
        }
        status = expect(token_kind::comma, "Expected ',' in cw::bits");
        if (!succeeded(status)) return status;
        std::int64_t width = 0;
        status = parse_integer_expression(width);
        if (!succeeded(status)) return status;
        if (width < 1 || width > 64)
            return fail(parser_v2_failure_kind::semantic, "Invalid cw::bits count");
        bits.bit_count = static_cast<std::uint8_t>(width);
        status = expect(token_kind::r_paren, "Expected ')' after cw::bits");
        if (!succeeded(status)) return status;
        status = expect(token_kind::r_bracket, "Expected ']]' after cw::bits");
        if (!succeeded(status)) return status;
        status = expect(token_kind::r_bracket, "Expected ']]' after cw::bits");
        if (!succeeded(status)) return status;
    }
    status = expect(token_kind::assign, "Expected '=' in using alias");
    if (!succeeded(status)) return status;
    resolved_type type;
    status = parse_type(scope, type);
    if (!succeeded(status)) return status;
    status = parse_array_suffix(type);
    if (!succeeded(status)) return status;
    derived_type_record array_shape;
    const bool array_alias = G.derived(type.type, array_shape) &&
        array_shape.kind == derived_type_kind::bounded_array;
    intrinsic_type intrinsic;
    if (!G.intrinsic(type.type, intrinsic)) {
        type_handle handle;
        const auto* entry = G.named(type.type, handle) ? G.find(handle) : nullptr;
        intrinsic = entry && entry->scalar_named() && entry->kind != graph_type_kind::enumeration
            ? entry->storage_intrinsic() : intrinsic_type::none;
    }
    if (!array_alias && (intrinsic < intrinsic_type::bool_type || intrinsic > intrinsic_type::long_double_type))
        return fail(parser_v2_failure_kind::unsupported, "Using aliases require a scalar or bounded array type");
    if (!at(token_kind::semicolon))
        return fail(parser_v2_failure_kind::syntax, "Expected ';' after using alias");
    identity_ref identity;
    status = identities.resolve(scope, name, identity_kind::type, identity);
    if (!succeeded(status)) return status;
    for (const auto& definition : templates->records)
        if (definition.identity == identity)
            return fail(parser_v2_failure_kind::semantic, "Using alias conflicts with a template name");
    const auto previous_array = templates->array_aliases.find(identity.value());
    if (array_alias) {
        if (bits.bit_count || G.find_type(identity) ||
            (previous_array != templates->array_aliases.end() && previous_array->second != type.type))
            return fail(parser_v2_failure_kind::semantic, "Conflicting array using alias");
        try { templates->array_aliases.emplace(identity.value(), type.type); }
        catch (...) { return server_status::io_error; }
        return consume();
    }
    if (previous_array != templates->array_aliases.end())
        return fail(parser_v2_failure_kind::semantic, "Using alias conflicts with an array alias");
    const bool repeated = static_cast<bool>(G.find_type(identity));
    type_handle handle;
    if (bits.bit_count > bit_storage_width(intrinsic))
        return fail(parser_v2_failure_kind::semantic, "cw::bits exceeds the alias storage width");
    status = G.define_intrinsic_alias(identity, intrinsic, handle, true, bits);
    if (!succeeded(status)) return fail(parser_v2_failure_kind::semantic, "Conflicting using alias");
    if (repeated && warnings) {
        try { warnings->push_back({parser_warning_kind::duplicate_alias, file, alias_source,
            "Repeated identical using alias declaration"}); }
        catch (...) { return server_status::io_error; }
    }
    status = record_contribution(file, source_data_ref::type(identity, true));
    if (!succeeded(status)) return status;
    return consume();
}

server_status header_parser_v2::parse_template(identity_ref scope) noexcept {
    try {
        record_template definition;
        definition.scope = scope;
        definition.root = input.semantic_root();
        auto status = consume();
        if (!succeeded(status)) return status;
        status = expect(token_kind::less, "Expected '<' after template");
        if (!succeeded(status)) return status;
        while (!at(token_kind::greater)) {
            template_parameter parameter;
            parameter.type = at(token_kind::kw_typename) || at(token_kind::kw_class);
            if (!parameter.type && !at(token_kind::kw_int))
                return fail(parser_v2_failure_kind::unsupported, "Template parameters support typename/class and int");
            status = consume();
            if (!succeeded(status)) return status;
            if (!at(token_kind::identifier))
                return fail(parser_v2_failure_kind::syntax, "Expected template parameter name");
            parameter.name = current()->identifier;
            for (const auto& previous : definition.parameters)
                if (previous.name == parameter.name)
                    return fail(parser_v2_failure_kind::semantic, "Duplicate template parameter");
            if (definition.parameters.size() >= 32)
                return fail(parser_v2_failure_kind::unsupported, "Template parameter limit exceeded");
            definition.parameters.push_back(parameter);
            status = consume();
            if (!succeeded(status)) return status;
            if (at(token_kind::greater)) break;
            status = expect(token_kind::comma, "Expected ',' between template parameters");
            if (!succeeded(status)) return status;
            if (at(token_kind::greater))
                return fail(parser_v2_failure_kind::syntax, "Trailing comma in template parameters");
        }
        if (definition.parameters.empty())
            return fail(parser_v2_failure_kind::unsupported, "Explicit template specializations are unsupported");
        status = consume();
        if (!succeeded(status)) return status;
        if (!at(token_kind::kw_struct) && !at(token_kind::kw_class))
            return fail(parser_v2_failure_kind::unsupported, "Only record templates are supported");
        definition.tokens.push_back(*current());
        status = consume();
        if (!succeeded(status)) return status;
        status = parse_record_attributes(definition.index_member, definition.index_origin, &definition.index_parameter);
        if (!succeeded(status)) return status;
        if (definition.index_parameter &&
            std::none_of(definition.parameters.begin(), definition.parameters.end(),
                [&](const auto& parameter) { return !parameter.type && parameter.name == definition.index_parameter; }))
            return fail(parser_v2_failure_kind::semantic, "cw::index requires an integer template parameter");
        if (!at(token_kind::identifier))
            return fail(parser_v2_failure_kind::syntax, "Expected template record name");
        const auto name = current()->identifier;
        for (const auto& parameter : definition.parameters)
            if (parameter.name == name)
                return fail(parser_v2_failure_kind::semantic, "Template record shadows its parameter");
        definition.tokens.push_back(*current());
        status = identities.resolve(scope, name, identity_kind::type, definition.identity);
        if (!succeeded(status)) return status;
        if (G.find_type(definition.identity) || templates->array_aliases.contains(definition.identity.value()))
            return fail(parser_v2_failure_kind::semantic, "Template conflicts with a concrete type or alias");
        status = consume();
        if (!succeeded(status)) return status;
        if (at(token_kind::colon)) {
            // Resolve base template arguments when replaying the specialization.
            do {
                if (input.finished() || current() == nullptr || at(token_kind::semicolon) ||
                    definition.tokens.size() >= 1000000)
                    return fail(parser_v2_failure_kind::syntax, "Incomplete template base list");
                definition.tokens.push_back(*current());
                status = consume();
                if (!succeeded(status)) return status;
            } while (!at(token_kind::l_brace));
        }
        if (!at(token_kind::l_brace))
            return fail(parser_v2_failure_kind::unsupported, "Template requires a record body");
        std::size_t braces = 0;
        do {
            const auto* token = current();
            if (!token) return fail(parser_v2_failure_kind::syntax, "Unterminated template record");
            braces += token->kind == token_kind::l_brace;
            if (token->kind == token_kind::r_brace) --braces;
            // Keep the supported template grammar explicit rather than silently
            // accepting packs, constraints or dependent declaration forms.
            if (token->kind == token_kind::kw_template)
                return fail(parser_v2_failure_kind::unsupported, "Nested template declarations are unsupported");
            definition.tokens.push_back(*token);
            if (definition.tokens.size() > 1000000)
                return fail(parser_v2_failure_kind::unsupported, "Template token limit exceeded");
            status = consume();
            if (!succeeded(status)) return status;
        } while (braces);
        if (!at(token_kind::semicolon))
            return fail(parser_v2_failure_kind::syntax, "Expected ';' after template record");
        definition.tokens.push_back(*current());
        for (const auto& previous : templates->records) {
            if (previous.identity != definition.identity) continue;
            if (previous.root == definition.root)
                return fail(parser_v2_failure_kind::semantic, "Duplicate template definition in one Header root");
            bool same = previous.index_member == definition.index_member &&
                previous.index_origin == definition.index_origin && previous.index_parameter == definition.index_parameter &&
                previous.parameters.size() == definition.parameters.size() &&
                previous.tokens.size() == definition.tokens.size();
            for (std::size_t p = 0; same && p < previous.parameters.size(); ++p)
                same = previous.parameters[p].name == definition.parameters[p].name &&
                    previous.parameters[p].type == definition.parameters[p].type;
            for (std::size_t t = 0; same && t < previous.tokens.size(); ++t) {
                const auto& a = previous.tokens[t];
                const auto& b = definition.tokens[t];
                same = a.kind == b.kind && a.identifier == b.identifier &&
                    a.number.kind == b.number.kind && a.number.bits == b.number.bits &&
                    (a.kind != token_kind::string_literal || input.spelling(a) == input.spelling(b));
            }
            if (!same) return fail(parser_v2_failure_kind::semantic, "Conflicting template definitions across Header roots");
            return consume();
        }
        templates->records.push_back(std::move(definition));
        return consume();
    } catch (...) { return server_status::io_error; }
}

server_status header_parser_v2::instantiate_template(identity_ref& identity, resolved_type& output, identity_ref argument_scope) noexcept {
    try {
        const record_template* definition = nullptr;
        for (const auto& candidate : templates->records)
            if (candidate.identity == identity) definition = &candidate;
        if (!definition)
            return fail(parser_v2_failure_kind::semantic, "Type is not a visible record template");
        if (template_replays.size() >= 64)
            return fail(parser_v2_failure_kind::unsupported, "Template instantiation depth exceeded");
        template_replay replay;
        std::vector<std::int64_t> integers;
        std::string key(input.semantic_strings().get(definition->tokens[1].identifier));
        key += '<';
        auto status = consume();
        if (!succeeded(status)) return status;
        for (std::size_t i = 0; i < definition->parameters.size(); ++i) {
            const auto& parameter = definition->parameters[i];
            if (i) {
                status = expect(token_kind::comma, "Expected ',' between template arguments");
                if (!succeeded(status)) return status;
                key += ',';
            }
            if (parameter.type) {
                resolved_type argument;
                status = parse_type(argument_scope, argument);
                if (!succeeded(status)) return status;
                replay.types.emplace_back(parameter.name, argument);
                // Semantic spelling, independent of aliases and allocation order.
                if (!template_type_key(argument.type, key))
                    return fail(parser_v2_failure_kind::semantic, "Invalid template type argument");
                integers.push_back(0);
            } else {
                std::int64_t value = 0;
                status = parse_integer_expression(value);
                if (!succeeded(status)) return status;
                if (value < (std::numeric_limits<int>::min)() || value > (std::numeric_limits<int>::max)())
                    return fail(parser_v2_failure_kind::semantic, "Template int argument is out of range");
                integers.push_back(value);
                key += std::to_string(value);
            }
            if (key.size() > 65536)
                return fail(parser_v2_failure_kind::unsupported, "Template specialization name limit exceeded");
        }
        status = expect_template_close();
        if (!succeeded(status)) return status;
        key += '>';
        string_id specialization;
        status = input.semantic_strings().intern(key, specialization);
        if (!succeeded(status)) return status;
        status = identities.resolve(definition->scope, specialization, identity_kind::type, identity);
        if (!succeeded(status)) return status;
        if (G.find_type(identity)) return resolve_named_type(identity, output);
        for (std::size_t i = 0; i < definition->tokens.size(); ++i) {
            auto token = definition->tokens[i];
            if (i == 1) token.identifier = specialization;
            else if (token.kind == token_kind::identifier && token.identifier == definition->tokens[1].identifier &&
                (i + 1 == definition->tokens.size() || definition->tokens[i + 1].kind != token_kind::less))
                token.identifier = specialization; // injected class name, including constructors
            if (token.kind == token_kind::identifier) {
                for (std::size_t p = 0; p < definition->parameters.size(); ++p) {
                    if (definition->parameters[p].type || token.identifier != definition->parameters[p].name) continue;
                    const auto value = integers[p];
                    if (value < 0) {
                        auto minus = token;
                        minus.kind = token_kind::minus;
                        minus.identifier = {};
                        replay.tokens.push_back(minus);
                    }
                    token.kind = token_kind::pp_number;
                    token.identifier = {};
                    token.number.kind = prepared_number_kind_v2::unsigned_integer;
                    token.number.bits = value < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(value) : value;
                    break;
                }
            }
            replay.tokens.push_back(token);
        }
        template_replays.push_back(std::move(replay));
        struct pop_replay {
            std::vector<template_replay>& stack;
            bool& pending;
            prepared_token& token;
            bool saved;
            prepared_token saved_token;
            ~pop_replay() { stack.pop_back(); pending = saved; token = saved_token; }
        } pop{template_replays, has_split_greater, split_greater, has_split_greater, split_greater};
        has_split_greater = false;
        auto index_origin = definition->index_origin;
        for (std::size_t p = 0; definition->index_parameter && p < definition->parameters.size(); ++p) {
            if (definition->parameters[p].name == definition->index_parameter) {
                index_origin = integers[p];
                break;
            }
        }
        status = parse_record(definition->scope, definition->index_member, index_origin);
        if (!succeeded(status)) return status;
        if (template_replays.back().cursor != template_replays.back().tokens.size())
            return fail(parser_v2_failure_kind::syntax, "Unexpected tokens after instantiated record");
        return resolve_named_type(identity, output);
    } catch (...) { return server_status::io_error; }
}
