#include "parser.hpp"

#include "../frontend/semantic_input.hpp"
#include "../graph/construction_semantics.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>
#include <vector>

namespace cw::server {
namespace {

struct parser_stage_timer final {
    using clock_type = std::chrono::steady_clock;
    std::uint64_t* elapsed;
    clock_type::time_point started;
    explicit parser_stage_timer(std::uint64_t* value) noexcept
        : elapsed(value), started(value != nullptr ? clock_type::now() : clock_type::time_point{}) {}
    ~parser_stage_timer() noexcept {
        if (elapsed != nullptr) *elapsed += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now() - started).count());
    }
};

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
    struct path_step final { string_id name{}; std::uint64_t index = 0; };
    std::vector<path_step> path;
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
    bool binary_operator = false;
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
        std::vector<parser_warning>* warnings,
        semantic_input_telemetry* input_telemetry = nullptr,
        semantic_parse_telemetry* parse_telemetry = nullptr,
        semantic_parse_telemetry* coarse_telemetry = nullptr) noexcept
        : files(files_value),
          input(
              files_value,
              lexical,
              configuration,
              strings,
              input_telemetry),
          strings(strings),
          identities(identities),
          G(G),
          sources(sources),
          failure(failure),
          warnings(warnings),
          telemetry(parse_telemetry),
          coarse_telemetry(coarse_telemetry) {
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

        using coarse_clock =
            std::chrono::steady_clock;

        const auto profile_source =
            domain == semantic_domain::source &&
            coarse_telemetry != nullptr;

        const auto setup_started =
            profile_source
            ? coarse_clock::now()
            : coarse_clock::time_point{};

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

        if (profile_source) {
            coarse_telemetry->
                source_root_setup_ns +=
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<
                        std::chrono::nanoseconds>(
                        coarse_clock::now() -
                        setup_started)
                        .count());
        }

        const auto replay_started =
            profile_source
            ? coarse_clock::now()
            : coarse_clock::time_point{};

        const auto parsed =
            parse_scope(
                identities.root(),
                false,
                0);

        if (!succeeded(parsed)) {
            return parsed;
        }

        if (profile_source) {
            coarse_telemetry->
                source_replay_ns +=
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<
                        std::chrono::nanoseconds>(
                        coarse_clock::now() -
                        replay_started)
                        .count());
        }

        const auto finish_started =
            profile_source
            ? coarse_clock::now()
            : coarse_clock::time_point{};

        const auto finished =
            sources.end_root();

        if (profile_source &&
            succeeded(finished)) {

            coarse_telemetry->
                source_root_finish_ns +=
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<
                        std::chrono::nanoseconds>(
                        coarse_clock::now() -
                        finish_started)
                        .count());

            ++coarse_telemetry->
                source_root_count;
        }

        return finished;
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

    [[nodiscard]] server_status inherited_member_path(type_handle record, string_id name,
        std::vector<endpoint_path_step>& path, type_handle& owner, member_index& member,
        std::size_t depth = 0) noexcept {
        if (depth >= parser_scope_depth_limit) {
            return fail(parser_failure_kind::unsupported, "Inherited member lookup exceeds supported depth");
        }
        // The endpoint registers the selected record before consuming '.name'.
        // Only inherited records still need registration here.
        if (depth != 0) {
            const auto dependency = sources.add_dependency(G.identity(record));
            if (!succeeded(dependency)) { return dependency; }
        }
        {
            parser_stage_timer timer{domain == semantic_domain::source && telemetry != nullptr
                ? &telemetry->source_member_ns : nullptr};

            if (domain == semantic_domain::source &&
                coarse_telemetry != nullptr) {

                ++coarse_telemetry->
                    source_member_lookups;
            }

            member = G.find_member(
                record,
                name);
        }
        if (member) { owner = record; return server_status::success; }
        type_entry entry;
        if (!read_type(record, entry)) { return server_status::project_configuration_invalid; }
        bool found = false;
        for (std::uint32_t i = 0; i < entry.bases.count; ++i) {
            base_record base;
            if constexpr (std::is_same_v<Graph, graph>) { base = G.bases(record)[i]; }
            else if (!G.base(record, i, base)) { return server_status::project_configuration_invalid; }
            std::vector<endpoint_path_step> candidate;
            type_handle candidate_owner;
            member_index candidate_member;
            const auto status = inherited_member_path(G.find_type(base.type), name, candidate,
                candidate_owner, candidate_member, depth + 1);
            if (!succeeded(status)) { return status; }
            if (!candidate_member) { continue; }
            if (found || base.virtual_base()) {
                return fail(parser_failure_kind::unsupported, "Ambiguous or virtual inherited member path");
            }
            try {
                path.push_back({i, endpoint_path_step_kind::base, {}});
                path.insert(path.end(), candidate.begin(), candidate.end());
            } catch (...) { return server_status::io_error; }
            owner = candidate_owner;
            member = candidate_member;
            found = true;
        }
        return server_status::success;
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

        const auto object_identity =
            G.identity(
                object);

        if (!object_identity ||
            object_identity.kind() !=
                identity_kind::object) {

            return fail_at(
                parser_failure_kind::semantic,
                "Reference binding object has no semantic identity",
                location);
        }

        const auto dependency =
            sources.add_dependency(
                object_identity);

        if (!succeeded(dependency)) {
            return dependency;
        }

        output =
            construction_value::object_binding(
                object_identity.value());

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

            type_entry entry;
            if (!read_type(handle, entry)) { return server_status::project_artifact_invalid; }
            output = entry.kind == graph_type_kind::intrinsic_alias ? G.intrinsic(entry.alias_intrinsic()) : G.named(handle);

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
        bool allow_named_operator = false,
        string_id enclosing_record = {}) noexcept {

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

        // Legacy generated headers qualify in-class operators with their own
        // record name. Accept only that redundant qualifier, not other scopes.
        if (allow_named_operator && depth == 0 && at(token_kind::identifier)) {
            semantic_token next;
            auto status = peek(next);
            if (!succeeded(status)) {
                return status;
            }
            if (next.kind == token_kind::scope) {
                if (!enclosing_record || current.identifier != enclosing_record) {
                    return fail(parser_failure_kind::semantic,
                        "In-class operator qualifier must name the enclosing record");
                }
                status = advance();
                if (!succeeded(status)) {
                    return status;
                }
                status = advance();
                if (!succeeded(status)) {
                    return status;
                }
                if (!at(token_kind::kw_operator)) {
                    return fail(parser_failure_kind::unsupported,
                        "In-class qualification is supported only for named operators");
                }
            }
        }

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
            else if (at(token_kind::exclamation)) {
                operator_name = "operator!";
            }
            else if (!at(token_kind::assign)) {
                switch (current.kind) {
                case token_kind::plus_assign: operator_name = "operator+="; break;
                case token_kind::minus_assign: operator_name = "operator-="; break;
                case token_kind::star_assign: operator_name = "operator*="; break;
                case token_kind::slash_assign: operator_name = "operator/="; break;
                case token_kind::percent_assign: operator_name = "operator%="; break;
                case token_kind::caret_assign: operator_name = "operator^="; break;
                case token_kind::ampersand_assign: operator_name = "operator&="; break;
                case token_kind::pipe_assign: operator_name = "operator|="; break;
                case token_kind::shift_left_assign: operator_name = "operator<<="; break;
                case token_kind::shift_right_assign: operator_name = "operator>>="; break;
                case token_kind::equal: operator_name = "operator=="; break;
                case token_kind::not_equal: operator_name = "operator!="; break;
                case token_kind::less: operator_name = "operator<"; break;
                case token_kind::greater: operator_name = "operator>"; break;
                case token_kind::less_equal: operator_name = "operator<="; break;
                case token_kind::greater_equal: operator_name = "operator>="; break;
                default:
                    return fail(parser_failure_kind::unsupported,
                        "Named operator declarator is not supported");
                }
                output.binary_operator = true;
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
        bool allow_named_operator = false,
        string_id enclosing_record = {}) noexcept {

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
                allow_named_operator,
                enclosing_record);

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

    [[nodiscard]] server_status parse_constructor_aggregate(
        string_id target, const semantic_source_location& location, type_ref type,
        std::vector<constructor_operation::path_step> path,
        std::vector<constructor_operation>& operations, std::size_t depth = 0) noexcept {
        if (depth >= parser_scope_depth_limit) {
            return fail(parser_failure_kind::unsupported, "Aggregate initializer exceeds supported depth");
        }
        derived_type_record array;
        intrinsic_type intrinsic;
        if (at(token_kind::string_literal) && G.derived(type, array) &&
            array.kind == derived_type_kind::bounded_array && G.intrinsic(array.child, intrinsic) &&
            intrinsic == intrinsic_type::char_type) {
            std::vector<unsigned char> bytes;
            auto status = parse_string_bytes(array.payload, bytes);
            if (!succeeded(status)) { return status; }
            for (std::uint64_t i = 0; i < array.payload; ++i) {
                pending_construction value;
                value.location = location;
                value.value = construction_value::constant(construction_kind::unsigned_integer,
                    i < bytes.size() ? bytes[static_cast<std::size_t>(i)] : 0);
                status = append_constructor_operation(operations, target, location, value);
                if (!succeeded(status)) { return status; }
                try { operations.back().path = path; operations.back().path.push_back({{}, i}); }
                catch (...) { return server_status::io_error; }
            }
            return server_status::success;
        }
        type_handle record;
        type_entry entry;
        if (G.named(type, record)) {
            if (!read_type(record, entry) || entry.bases.count != 0 || entry.polymorphic()) {
                return fail(parser_failure_kind::unsupported, "Aggregate initializer requires a direct non-polymorphic record");
            }
            auto status = expect(token_kind::l_brace, "Expected '{' for aggregate initializer");
            if (!succeeded(status)) { return status; }
            status = advance();
            if (!succeeded(status)) { return status; }
            std::uint32_t position = 0;
            int mode = 0;
            while (!at(token_kind::r_brace)) {
                const bool designated = at(token_kind::dot);
                if (mode != 0 && mode != (designated ? 1 : 2)) {
                    return fail(parser_failure_kind::syntax, "Cannot mix designated and positional aggregate initializers");
                }
                mode = designated ? 1 : 2;
                member_record member;
                if (designated) {
                    status = advance();
                    if (!succeeded(status)) { return status; }
                    const auto index = G.find_member(record, current.identifier);
                    if (!at(token_kind::identifier) || !index || index.value() < position) {
                        return fail(parser_failure_kind::semantic, "Aggregate designators must name distinct fields in declaration order");
                    }
                    position = index.value();
                    status = advance();
                    if (!succeeded(status)) { return status; }
                    status = expect(token_kind::assign, "Expected '=' after aggregate designator");
                    if (!succeeded(status)) { return status; }
                    status = advance();
                    if (!succeeded(status)) { return status; }
                }
                // find_member supplies the checked member_index without constructing a private handle.
                if (position >= entry.members.count) {
                    return fail(parser_failure_kind::semantic, "Too many aggregate initializers");
                }
                if constexpr (std::is_same_v<Graph, graph>) { member = G.members(record)[position]; }
                else {
                    if (!G.member(record, position, member)) {
                        return fail(parser_failure_kind::semantic, "Aggregate field is unavailable");
                    }
                }
                std::vector<constructor_operation::path_step> child;
                try { child = path; child.push_back({member.name, 0}); }
                catch (...) { return server_status::io_error; }
                status = parse_constructor_aggregate(target, location, member.type, std::move(child), operations, depth + 1);
                if (!succeeded(status)) { return status; }
                ++position;
                if (!at(token_kind::comma)) { break; }
                status = advance();
                if (!succeeded(status)) { return status; }
            }
            status = expect(token_kind::r_brace, "Expected '}' after aggregate initializer");
            return succeeded(status) ? advance() : status;
        }
        pending_construction value;
        auto status = parse_constructor_expression(value);
        if (!succeeded(status)) { return status; }
        if (value.kind != pending_construction_kind::value || !construction_compatible(G, type, value.value)) {
            return fail(parser_failure_kind::semantic, "Aggregate value does not match field type");
        }
        status = append_constructor_operation(operations, target, location, value);
        if (!succeeded(status)) { return status; }
        operations.back().path = std::move(path);
        return server_status::success;
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

                if (at(token_kind::l_brace)) {
                    const auto member = std::find_if(record_members.begin(), record_members.end(),
                        [target](const auto& value) { return value.name == target; });
                    if (member != record_members.end() && member->type.kind() == type_ref_kind::named) {
                        status = parse_constructor_aggregate(target, target_location, member->type, {}, operations);
                        if (!succeeded(status)) { return status; }
                        if (!at(token_kind::comma)) { break; }
                        status = advance();
                        if (!succeeded(status)) { return status; }
                        continue;
                    }
                }

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

            std::vector<constructor_operation::path_step> nested_path;
            while (at(token_kind::dot) || at(token_kind::l_bracket)) {
                if (nested_path.size() >= parser_scope_depth_limit) {
                    return fail(parser_failure_kind::unsupported, "Constructor path exceeds supported depth");
                }
                if (at(token_kind::l_bracket)) {
                    status = advance();
                    if (!succeeded(status)) { return status; }
                    const auto spelling = token_text(current);
                    std::uint64_t index = 0;
                    const auto converted = std::from_chars(spelling.data(), spelling.data() + spelling.size(), index);
                    if (!at(token_kind::pp_number) || converted.ec != std::errc{} ||
                        converted.ptr != spelling.data() + spelling.size()) {
                        return fail(parser_failure_kind::unsupported, "Constructor array index requires a decimal integer literal");
                    }
                    status = advance();
                    if (!succeeded(status)) { return status; }
                    status = expect(token_kind::r_bracket, "Expected ']' after constructor array index");
                    if (!succeeded(status)) { return status; }
                    try { nested_path.push_back({{}, index}); }
                    catch (...) { return server_status::io_error; }
                    status = advance();
                    if (!succeeded(status)) { return status; }
                    continue;
                }
                status = advance();
                if (!succeeded(status)) { return status; }
                if (!at(token_kind::identifier) || nested_path.size() >= parser_scope_depth_limit) {
                    return fail(parser_failure_kind::syntax, "Invalid nested constructor field path");
                }
                try { nested_path.push_back({current.identifier, 0}); }
                catch (...) { return server_status::io_error; }
                status = advance();
                if (!succeeded(status)) { return status; }
            }
            status = expect(token_kind::assign,
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
            operations.back().path = std::move(nested_path);

            status = advance();

            if (!succeeded(status)) {
                return status;
            }
        }

        return advance();
    }

    [[nodiscard]] server_status normalize_constructor_operations(
        identity_ref scope,
        identity_ref owner,
        const std::vector<member_record>& members,
        const std::vector<constructor_operation>& operations,
        std::vector<construction_value>& construction) noexcept {

        if (operations.empty()) {
            return server_status::success;
        }

        auto& assigned = record_assigned;
        std::vector<constructor_default> nested_defaults;

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

            if (!operation.path.empty()) {
                auto target_type = members[target_index].type;
                std::string path;
                try { path = strings.get(operation.target); }
                catch (...) { return server_status::io_error; }
                for (const auto& step : operation.path) {
                    if (!step.name) {
                        derived_type_record array;
                        if (!G.derived(target_type, array) || array.kind != derived_type_kind::bounded_array) {
                            return fail_at(parser_failure_kind::semantic,
                                "Constructor array index requires a bounded array", operation.target_location);
                        }
                        if (step.index >= array.payload) {
                            return fail_at(parser_failure_kind::semantic,
                                "Constructor array index is outside the declared bound", operation.target_location);
                        }
                        target_type = array.child;
                        try { path += '['; path += std::to_string(step.index); path += ']'; }
                        catch (...) { return server_status::io_error; }
                        continue;
                    }
                    const auto name = step.name;
                    if (target_type.kind() != type_ref_kind::named) {
                        return fail_at(parser_failure_kind::unsupported,
                            "Nested constructor path requires direct record fields", operation.target_location);
                    }
                    type_handle nested_type;
                    if (!G.named(target_type, nested_type)) {
                        return fail_at(parser_failure_kind::semantic,
                            "Nested constructor target type is invalid", operation.target_location);
                    }
                    const auto member = G.find_member(nested_type, name);
                    member_record record;
                    if (!read_member(nested_type, member, record)) {
                        return fail_at(parser_failure_kind::semantic,
                            "Nested constructor field does not exist", operation.target_location);
                    }
                    target_type = record.type;
                    try { path += '.'; path += strings.get(name); }
                    catch (...) { return server_status::io_error; }
                }
                if (operation.expression.kind != pending_construction_kind::value ||
                    target_type.kind() != type_ref_kind::intrinsic ||
                    !construction_compatible(G, target_type, operation.expression.value)) {
                    return fail_at(parser_failure_kind::unsupported,
                        "Nested constructor assignment requires a writable scalar field and scalar constant",
                        operation.expression.location);
                }
                string_id spelling;
                auto status = strings.intern(path, spelling);
                if (!succeeded(status)) { return status; }
                auto found = std::find_if(nested_defaults.begin(), nested_defaults.end(),
                    [spelling](const auto& item) { return item.path == spelling; });
                if (found != nested_defaults.end()) { found->value = operation.expression.value; }
                else {
                    try { nested_defaults.push_back({owner, spelling, operation.expression.value}); }
                    catch (...) { return server_status::io_error; }
                }
                continue;
            }

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

        for (const auto& value : nested_defaults) {
            const auto status = G.constructor_defaults.add(value);
            if (!succeeded(status)) {
                return fail(parser_failure_kind::semantic, "Conflicting nested constructor defaults");
            }
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
                    base_identity,
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

        const auto base_handle =
            !bases.empty()
            ? G.find_type(
                bases.front().type)
            : type_handle{};

        const bool base_polymorphic =
            base_handle &&
            record_polymorphic(
                base_handle);

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
                bool subscript_operator = false,
                bool binary_operator = false) -> server_status {
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

                    if ((assignment_operator || subscript_operator || binary_operator) &&
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
                                "This operator must have an empty parameter list");
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

                if ((assignment_operator || subscript_operator || binary_operator) &&
                    (parameter_tokens == 0 || sole_void)) {
                    return fail(parser_failure_kind::syntax,
                        "Named operator requires one parameter");
                }

                bool override_seen = false;
                bool final_seen = false;
                bool ref_qualifier_seen = false;

                for (;;) {
                    if ((conversion || assignment_operator || subscript_operator || binary_operator) && !ref_qualifier_seen &&
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
                    else if ((conversion || subscript_operator || binary_operator || !at(token_kind::kw_default)) &&
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
                    true,
                    record_name);

            if (!succeeded(status)) {
                return status;
            }

            if (at(token_kind::l_paren)) {
                status =
                    parse_method_tail(
                        virtual_prefix, strings.get(declarator.name) == "operator!",
                        strings.get(declarator.name) == "operator=",
                        strings.get(declarator.name) == "operator[]",
                        declarator.binary_operator);

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
                identity,
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
        parser_stage_timer timer{domain == semantic_domain::source && telemetry != nullptr
            ? &telemetry->source_object_ns : nullptr};

        if (domain == semantic_domain::source &&
            coarse_telemetry != nullptr) {

            ++coarse_telemetry->
                source_object_statements;
        }

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
        resolved_link_endpoint& output,
        identity_ref known_object = {}) noexcept {
        parser_stage_timer timer{domain == semantic_domain::source && telemetry != nullptr
            ? &telemetry->source_endpoint_ns : nullptr};

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
            known_object ? known_object : find_object_identity(
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

                type_ref referent;
                if (reference_referent(record_type, referent)) {
                    try { endpoint_steps.push_back({0, endpoint_path_step_kind::dereference, {}}); }
                    catch (...) { return server_status::io_error; }
                    record_type = referent;
                }

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

                member_index member;
                type_handle owner;
                status = inherited_member_path(record, current.identifier, endpoint_steps, owner, member);
                if (!succeeded(status)) { return status; }
                if (member) { record = owner; }

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

        output.type =
            current_type;

        const auto record_endpoint_shape =
            [this](bool direct_member_endpoint) noexcept {
                if (domain !=
                        semantic_domain::source ||
                    coarse_telemetry == nullptr) {

                    return;
                }

                ++coarse_telemetry->
                    source_endpoint_count;

                coarse_telemetry->
                    source_endpoint_steps +=
                    static_cast<std::uint64_t>(
                        endpoint_steps.size());

                if (direct_member_endpoint) {
                    ++coarse_telemetry->
                        source_endpoint_direct_members;
                }
                else {
                    ++coarse_telemetry->
                        source_endpoint_path_endpoints;
                }

                for (const auto& step :
                     endpoint_steps) {

                    switch (step.kind) {
                    case endpoint_path_step_kind::member:
                        ++coarse_telemetry->
                            source_endpoint_member_steps;
                        break;

                    case endpoint_path_step_kind::array_index:
                        ++coarse_telemetry->
                            source_endpoint_array_steps;
                        break;

                    case endpoint_path_step_kind::dereference:
                        ++coarse_telemetry->
                            source_endpoint_dereference_steps;
                        break;

                    case endpoint_path_step_kind::base:
                        ++coarse_telemetry->
                            source_endpoint_base_steps;
                        break;

                    default:
                        break;
                    }
                }
            };

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
                object_identity,
                direct_member,
            };

            record_endpoint_shape(
                true);

            return server_status::success;
        }

        endpoint_path_handle path;
        type_ref resolved;

        if constexpr (std::is_same_v<Graph, graph>) {
            if (domain == semantic_domain::source) {
                resolved = current_type;
                status = G.intern_resolved_endpoint_path(object_value.type, endpoint_steps, resolved, path);
            }
            else {
                status = G.intern_endpoint_path(object_value.type, endpoint_steps, path, &resolved);
            }
        }
        else {
            status = G.intern_endpoint_path(object_value.type, endpoint_steps, path, &resolved);
        }

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
            object_identity,
            endpoint_ref::from_path(
                path),
        };

        record_endpoint_shape(
            false);

        return server_status::success;
    }

    [[nodiscard]] server_status parse_string_bytes(std::uint64_t bound,
        std::vector<unsigned char>& bytes) noexcept {
        while (at(token_kind::string_literal)) {
            const auto text = token_text(current);
            if (text.size() < 2 || text.front() != '"' || text.back() != '"') {
                return fail(parser_failure_kind::unsupported, "Only ordinary narrow string literals are supported");
            }
            for (std::size_t i = 1; i + 1 < text.size(); ++i) {
                unsigned int value = static_cast<unsigned char>(text[i]);
                if (value == '\\') {
                    if (++i + 1 >= text.size()) { return fail(parser_failure_kind::syntax, "Incomplete string escape"); }
                    const auto escape = text[i];
                    switch (escape) {
                    case '\\': case '\'': case '"': case '?': value = escape; break;
                    case 'a': value = 7; break;
                    case 'b': value = 8; break;
                    case 'f': value = 12; break;
                    case 'n': value = 10; break;
                    case 'r': value = 13; break;
                    case 't': value = 9; break;
                    case 'v': value = 11; break;
                    default: {
                        const bool hex = escape == 'x';
                        if (!hex && (escape < '0' || escape > '7')) {
                            return fail(parser_failure_kind::unsupported, "Unsupported string escape");
                        }
                        const std::size_t begin = i + (hex ? 1 : 0);
                        auto end = begin;
                        while (end + 1 < text.size()) {
                            const auto c = text[end];
                            const bool digit = hex ? ((c >= '0' && c <= '9') ||
                                (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) : (c >= '0' && c <= '7');
                            if (!digit || (!hex && end - begin == 3)) { break; }
                            ++end;
                        }
                        const auto converted = std::from_chars(text.data() + begin, text.data() + end, value, hex ? 16 : 8);
                        if (converted.ec != std::errc{} || value > 255) {
                            return fail(parser_failure_kind::semantic, "String escape is outside the byte range");
                        }
                        i = end - 1;
                        break;
                    }
                    }
                }
                if (!bound || bytes.size() >= bound - 1) {
                    return fail(parser_failure_kind::semantic, "String including terminator exceeds the char array bound");
                }
                try { bytes.push_back(static_cast<unsigned char>(value)); }
                catch (...) { return server_status::io_error; }
            }
            const auto status = advance();
            if (!succeeded(status)) { return status; }
        }
        return server_status::success;
    }

    [[nodiscard]] server_status commit_source_initialization(
        object_endpoint target, type_ref type, construction_value value, bool& replaced) noexcept {
        parser_stage_timer timer{telemetry != nullptr ? &telemetry->source_initialization_commit_ns : nullptr};
        if constexpr (std::is_same_v<Graph, graph>)
            return G.add_resolved_initialization(target, type, value, replaced);
        else return G.add_initialization(target, value, replaced);
    }

    [[nodiscard]] server_status commit_source_link(
        object_endpoint source, object_endpoint target, link_handle& output) noexcept {
        parser_stage_timer timer{telemetry != nullptr ? &telemetry->source_link_commit_ns : nullptr};
        if constexpr (std::is_same_v<Graph, graph>)
            return G.add_resolved_link(source, target, output);
        else return G.add_link(source, target, output);
    }

    [[nodiscard]] server_status record_source_initialization(object_endpoint target) noexcept {
        parser_stage_timer timer{telemetry != nullptr ? &telemetry->source_provenance_ns : nullptr};
        return sources.add_initialization(target);
    }

    [[nodiscard]] server_status record_source_data(file_id file, source_data_ref data) noexcept {
        parser_stage_timer timer{telemetry != nullptr ? &telemetry->source_provenance_ns : nullptr};
        return sources.add(file, data);
    }

    [[nodiscard]] server_status parse_source_string_assignment(
        const resolved_link_endpoint& target, file_id statement_file) noexcept {
        derived_type_record array;
        intrinsic_type character;
        if (!G.derived(target.type, array) || array.kind != derived_type_kind::bounded_array ||
            !array.payload || !G.intrinsic(array.child, character) || character != intrinsic_type::char_type) {
            return fail(parser_failure_kind::semantic, "String assignment requires a writable bounded char array");
        }

        if (coarse_telemetry != nullptr) {
            ++coarse_telemetry->
                source_string_assignment_statements;

            coarse_telemetry->
                source_string_assignment_elements +=
                array.payload;
        }

        std::vector<unsigned char> bytes;
        auto status = parse_string_bytes(array.payload, bytes);
        if (!succeeded(status)) { return status; }
        status = expect(token_kind::semicolon, "Expected ';' after string assignment");
        if (!succeeded(status)) { return status; }
        object_entry object;
        if (!read_object(G.find_object(target.endpoint.object), object)) {
            return fail(parser_failure_kind::semantic, "String assignment object is invalid");
        }
        // Normalize to scalar paths, reusing persisted initialization and BUILD provenance.
        try { endpoint_steps.push_back({0, endpoint_path_step_kind::array_index, {}}); }
        catch (...) { return server_status::io_error; }
        bool any_replaced = false;
        for (std::uint64_t i = 0; i < array.payload; ++i) {
            endpoint_steps.back().value = i;
            endpoint_path_handle path;
            if constexpr (std::is_same_v<Graph, graph>) {
                status = G.intern_resolved_endpoint_path(object.type, endpoint_steps, array.child, path);
            }
            else {
                status = G.intern_endpoint_path(object.type, endpoint_steps, path);
            }
            if (!succeeded(status)) { return status; }
            const object_endpoint element{target.endpoint.object, endpoint_ref::from_path(path)};
            const auto value = construction_value::constant(construction_kind::unsigned_integer,
                i < bytes.size() ? bytes[static_cast<std::size_t>(i)] : 0);
            bool replaced = false;
            status = commit_source_initialization(element, array.child, value, replaced);
            if (!succeeded(status)) { return status; }
            any_replaced |= replaced;
            status = record_source_initialization(element);
            if (!succeeded(status)) { return status; }
        }
        status = record_source_data(statement_file, source_data_ref::object(target.endpoint.object));
        if (!succeeded(status)) { return status; }
        if (any_replaced) {
            status = warn_at(parser_warning_kind::duplicate_initialization,
                "Object member is initialized more than once; the last initialization is used", target.location);
            if (!succeeded(status)) { return status; }
        }
        return advance();
    }

    [[nodiscard]] server_status parse_source_assignment(
        identity_ref scope, identity_ref target_object) noexcept {
        parser_stage_timer timer{telemetry != nullptr ? &telemetry->source_assignment_ns : nullptr};

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
                target, target_object);

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
            if (telemetry != nullptr) timer.elapsed = &telemetry->source_link_ns;

            if (coarse_telemetry != nullptr) {
                ++coarse_telemetry->
                    source_link_statements;
            }

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
                commit_source_link(
                    source.endpoint,
                    target.endpoint,
                    link);

            if (!succeeded(status)) {
                return fail(
                    parser_failure_kind::semantic,
                    "Link conflicts with an existing binding for the target endpoint");
            }

            status =
                record_source_data(
                    statement_file,
                    source_data_ref::link(
                        link));

            if (!succeeded(status)) {
                return status;
            }

            return advance();
        }

        if (coarse_telemetry != nullptr) {
            ++coarse_telemetry->
                source_value_assignment_statements;
        }

        if (at(token_kind::string_literal)) {
            return parse_source_string_assignment(target, statement_file);
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
            commit_source_initialization(
                target.endpoint, target.type,
                initial.value,
                replaced);

        if (!succeeded(status)) {
            return fail_at(
                parser_failure_kind::semantic,
                "Source value initialization target must be a writable scalar non-reference subobject",
                target.location);
        }

        status =
            record_source_initialization(
                target.endpoint);

        if (!succeeded(status)) {
            return status;
        }

        const auto object_identity =
            target.endpoint.object;

        if (!object_identity ||
            object_identity.kind() !=
                identity_kind::object) {
            return fail_at(
                parser_failure_kind::semantic,
                "Source value initialization object has no semantic identity",
                target.location);
        }

        status =
            record_source_data(
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

    [[nodiscard]] server_status parse_typedef(identity_ref scope) noexcept {
        if (domain != semantic_domain::header) {
            return fail(parser_failure_kind::unsupported, "Typedef declarations belong in Header inputs");
        }
        const auto file = current.file;
        auto status = advance();
        if (!succeeded(status)) { return status; }
        type_ref type;
        parsed_declarator declarator;
        status = parse_declared_type(scope, type, declarator, false, {});
        if (!succeeded(status)) { return status; }
        intrinsic_type intrinsic;
        if (!G.intrinsic(type, intrinsic) || intrinsic < intrinsic_type::bool_type || intrinsic > intrinsic_type::long_double_type) {
            return fail(parser_failure_kind::unsupported, "Typedef currently supports intrinsic scalar aliases");
        }
        status = expect(token_kind::semicolon, "Expected ';' after typedef");
        if (!succeeded(status)) { return status; }
        identity_ref identity;
        status = identities.resolve(scope, declarator.name, identity_kind::type, identity);
        if (!succeeded(status)) { return status; }
        type_handle handle;
        status = G.define_intrinsic_alias(identity, intrinsic, handle);
        if (!succeeded(status)) { return fail(parser_failure_kind::semantic, "Typedef conflicts with an existing type"); }
        status = sources.add(file, source_data_ref::type(identity, true));
        return succeeded(status) ? advance() : status;
    }

    [[nodiscard]] server_status parse_scope(
        identity_ref scope,
        bool expect_close,
        std::size_t scope_depth) noexcept {

        if (domain == semantic_domain::source)
            return parse_source_scope(scope, expect_close, scope_depth);

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

            if (at(token_kind::kw_typedef)) {
                const auto status = parse_typedef(scope);
                if (!succeeded(status)) { return status; }
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

    // Header declarations have completed before Source roots are visited.
    // Dispatch only Source statements; consume each statement immediately.
    [[nodiscard]] server_status parse_source_scope(
        identity_ref scope, bool expect_close, std::size_t scope_depth) noexcept {
        for (;;) {
            if (at(token_kind::invalid)) {
                return expect_close
                    ? fail(parser_failure_kind::syntax, "Semantic scope is not closed before end of input")
                    : server_status::success;
            }
            if (expect_close && at(token_kind::r_brace)) return server_status::success;

            server_status status;
            if (at(token_kind::semicolon)) status = advance();
            else if (at(token_kind::kw_namespace)) status = parse_namespace(scope, scope_depth);
            else if (at(token_kind::identifier)) {
                const auto object = current.identifier
                    ? find_object_identity(scope, current.identifier) : identity_ref{};
                status = object ? parse_source_assignment(scope, object) : parse_object(scope);
            }
            else if (builtin_start(current.kind) || at(token_kind::kw_const) ||
                     at(token_kind::kw_volatile) || at(token_kind::kw_static) || at(token_kind::kw_inline))
                status = parse_object(scope);
            else if (at(token_kind::kw_typedef))
                return fail(parser_failure_kind::unsupported, "Typedef declarations belong in Header inputs");
            else if (at(token_kind::kw_struct) || at(token_kind::kw_class) || at(token_kind::kw_union))
                return fail(parser_failure_kind::unsupported, "Type declarations are supported only in Header inputs");
            else return fail(parser_failure_kind::unsupported,
                "Declaration is outside the current direct Parser/Semantic slice");
            if (!succeeded(status)) return status;
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
    semantic_parse_telemetry* telemetry = nullptr;
    semantic_parse_telemetry* coarse_telemetry = nullptr;
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
    std::vector<parser_warning>* warnings,
    semantic_parse_telemetry* telemetry) noexcept {

    if (telemetry != nullptr) {
        const auto detailed = telemetry->detailed_source;
        *telemetry = {};
        telemetry->detailed_source = detailed;
    }
    using clock_type = std::chrono::steady_clock;

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

    semantic_input_telemetry input_telemetry;
    input_telemetry.detailed_source = telemetry != nullptr && telemetry->detailed_source;
    semantic_parser<graph, source_map> parser{
        files,
        lexical,
        configuration,
        strings,
        identities,
        G,
        sources,
        failure,
        warnings,
        telemetry != nullptr ? &input_telemetry : nullptr,
        telemetry != nullptr && telemetry->detailed_source ? telemetry : nullptr,
        telemetry};

    for (const auto domain :
         {semantic_domain::header,
          semantic_domain::source}) {

        const auto domain_started = telemetry != nullptr
            ? clock_type::now() : clock_type::time_point{};

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

        if (telemetry != nullptr) {
            const auto duration = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    clock_type::now() - domain_started).count());
            (domain == semantic_domain::header
                ? telemetry->header_ns : telemetry->source_ns) = duration;
        }
    }

    const auto finalize_started = telemetry != nullptr
        ? clock_type::now() : clock_type::time_point{};
    const auto result = sources.finalize(files.size(), identities, G);
    if (telemetry != nullptr) {
        telemetry->include_ns = input_telemetry.include_ns;
        telemetry->include_count = input_telemetry.include_count;
        telemetry->include_lexical_ns = input_telemetry.include_lexical_ns;
        telemetry->prepared_include_count = input_telemetry.prepared_include_count;
        telemetry->source_decode_ns = input_telemetry.source_decode_ns;
        telemetry->source_intern_ns = input_telemetry.source_intern_ns;
        telemetry->source_token_count = input_telemetry.source_token_count;
        telemetry->source_identifier_count = input_telemetry.source_identifier_count;

        telemetry->finalize_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                clock_type::now() - finalize_started).count());
    }
    return result;
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
