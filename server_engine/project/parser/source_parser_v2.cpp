/*
 * PARSER-V2-SOURCE-01
 * Restricted Source domain. Header Parser has already constructed Type Graph G.
 * Only object declarations, object values, char[] string writes, and links.
 * Writes to the SAME G / source map: no intermediate facts / AST / projection.
 */
#include "source_parser_v2.hpp"
#include "../frontend/semantic_input.hpp"
#include "../frontend/frontend_input.hpp"
#include "source_model_v2.hpp"
#include "../construction/execution_lanes.hpp"
#include "../graph/construction_semantics.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cw::server {
namespace {

// Reports each canonical Graph derivation layer and the final leaf type.
// Executed ONLY when a Source reference binding is rejected.
static void source_v2_type_description(
    const graph& G, const identity_space& identities,
    const string_table& strings, type_ref type,
    char* output, std::size_t capacity) noexcept {
    if (!capacity) return;
    output[0] = '\0';
    std::size_t used = 0;
    const auto append = [&](std::string_view text) noexcept {
        if (used >= capacity - 1) return;
        const auto remain = capacity - 1 - used;
        const auto count = text.size() < remain ? text.size() : remain;
        if (count) std::memcpy(output + used, text.data(), count);
        used += count;
        output[used] = '\0';
    };
    for (unsigned depth = 0; depth != 32; ++depth) {
        if (!type) { append("invalid"); return; }
        derived_type_record derived;
        if (G.derived(type, derived)) {
            switch (derived.kind) {
            case derived_type_kind::const_qualified: append("const->"); break;
            case derived_type_kind::volatile_qualified: append("volatile->"); break;
            case derived_type_kind::lvalue_reference: append("lref->"); break;
            case derived_type_kind::rvalue_reference: append("rref->"); break;
            case derived_type_kind::pointer: append("ptr->"); break;
            case derived_type_kind::bounded_array: append("array->"); break;
            case derived_type_kind::unbounded_array: append("unsized_array->"); break;
            }
            type = derived.child;
            continue;
        }
        intrinsic_type builtin;
        if (G.intrinsic(type, builtin)) {
            char tmp[48];
            std::snprintf(tmp, sizeof(tmp), "intrinsic(%u)",
                static_cast<unsigned>(builtin));
            append(tmp);
            return;
        }
        type_handle handle;
        if (G.named(type, handle)) {
            identity_record identity_record_value;
            if (identities.record(G.identity(handle), identity_record_value)) {
                append("named(");
                append(strings.get(identity_record_value.name));
                append(")");
            } else {
                append("named");
            }
            return;
        }
        char tmp[48];
        std::snprintf(tmp, sizeof(tmp), "kind=%u/value=%u",
            static_cast<unsigned>(type.kind()), type.value());
        append(tmp);
        return;
    }
    append("deep-derived-type");
}

// SOURCE-V2-PARALLEL-01: immutable lexical replay, NOT parallel Graph writes.
// Each lane owns its destination file_id slot. No shared append/locks/atomics.
class source_predecode_batch final {
public:
    [[nodiscard]] bool prepare(const file_context& files,
        const lexical_generation& lexical, const string_table& strings,
        std::size_t file_count) noexcept {
        ready_value = false;
        if (file_count == 0) return false;
        try { roots.resize(file_count); }
        catch (...) { return false; }
        auto capacity = execution_lane_capacity();
        if (capacity == 0) capacity = 1;
        const auto active = (std::min)(file_count,
            (std::min)(capacity, std::size_t{16}));
        std::vector<std::uint8_t> failures;
        try { failures.resize(active); }
        catch (...) { roots.clear(); return false; }
        job work{&files, &lexical, &strings, &roots, &failures, file_count, active};
        execution_lanes workers;
        if (!succeeded(workers.start(active)) ||
            !succeeded(workers.run(active, decode_lane, &work))) {
            roots.clear();
            return false;
        }
        for (const auto failed : failures) {
            if (failed != 0) { roots.clear(); return false; }
        }
        ready_value = true;
        return true;
    }

    [[nodiscard]] bool ready() const noexcept { return ready_value; }

    [[nodiscard]] std::span<const source_predecoded_token> tokens(
        file_id file) const noexcept {
        const auto index = static_cast<std::size_t>(file.value() - 1);
        if (!ready_value || !file || index >= roots.size()) return {};
        return roots[index].tokens;
    }

    // Syntactic operations are file-ordered and contain no semantic handles.
    [[nodiscard]] std::span<const source_model_operation> operations(
        file_id file) const noexcept {
        const auto index = static_cast<std::size_t>(file.value() - 1);
        if (!ready_value || !file || index >= roots.size()) return {};
        return roots[index].operations;
    }

private:
    struct job final {
        const file_context* files;
        const lexical_generation* lexical;
        const string_table* strings;  // read only until all lanes join
        std::vector<source_model_file>* roots;
        std::vector<std::uint8_t>* failures;
        std::size_t file_count;
        std::size_t lane_count;
    };

    static void decode_lane(void* raw, std::size_t lane) noexcept {
        auto& task = *static_cast<job*>(raw);
        try {
            for (auto i = lane; i < task.file_count; i += task.lane_count) {
                const file_id file{static_cast<std::uint32_t>(i + 1)};
                if (task.files->kind(file) != file_kind::source) continue;
                if (!task.lexical->contains(file)) {
                    (*task.failures)[lane] = 1;
                    return;
                }
                const auto words = task.lexical->words(file);
                const auto native = words.native_words();
#ifdef CW_SOURCE_PREBIND_PARALLEL_V2
                if (!task.files->content_available(file)) {
                    (*task.failures)[lane] = 1;
                    return;
                }
                const auto content = task.files->content(file);
#endif
                auto& model = (*task.roots)[i];
                auto& output = model.tokens;
                output.reserve(words.size());
#ifdef CW_SOURCE_MODEL_V2
                source_model_builder model_builder{model};
#endif
                std::uint32_t word_offset = 0;
                std::uint32_t source_offset = 0;
                while (word_offset != words.size()) {
                    frontend_token token{};
                    const auto result = !native.empty()
                        ? decode_frontend_token(native, file, word_offset,
                            source_offset, token)
                        : decode_frontend_token(words, file, word_offset,
                            source_offset, token);
                    if (!succeeded(result)) {
                        (*task.failures)[lane] = 1;
                        return;
                    }
#ifdef CW_SOURCE_PREBIND_PARALLEL_V2
                    string_id known{};
                    if (token.kind == token_kind::identifier) {
                        const auto offset = static_cast<std::size_t>(token.source_offset);
                        const auto length = static_cast<std::size_t>(token.source_length);
                        if (offset > content.size() ||
                            length > content.size() - offset) {
                            // The canonical parser produces the original diagnostic.
                            (*task.failures)[lane] = 1;
                            return;
                        }
                        known = task.strings->find(content.substr(offset, length));
                    }
                    output.push_back({token.source_offset,
                        token.source_length, token.kind, known});
#else
                    output.push_back({token.source_offset,
                        token.source_length, token.kind});
#endif
#ifdef CW_SOURCE_MODEL_V2
                    if (!model_builder.accept(output.back())) {
                        (*task.failures)[lane] = 1;
                        return;
                    }
#endif
                }
#ifdef CW_SOURCE_MODEL_V2
                if (!model_builder.finish()) {
                    (*task.failures)[lane] = 1;
                    return;
                }
#endif
            }
        } catch (...) { (*task.failures)[lane] = 1; }
    }

    bool ready_value = false;
    std::vector<source_model_file> roots;
};

class source_parser_v2 final {
public:
    source_parser_v2(file_context& f, lexical_generation& l,
        const preprocessor_configuration& c, string_table& s,
        identity_space& ids, graph& g, source_map& sm,
        parser_failure* err, std::vector<parser_warning>* warns,
        semantic_parse_telemetry* t,
        const source_predecode_batch* decoded = nullptr) noexcept
        : files(f), input(f, l, c, s, t ? &input_profile : nullptr),
          strings(s), identities(ids), G(g), sources(sm), failure(err),
          warnings(warns), telemetry(t), prepared(decoded) {
        // SOURCE-V2-PERF-01-INPUT-TELEMETRY: detailed token clocks only
        // in explicit publish-profile, never in normal PUBLISH/publish-stage.
        input_profile.detailed_source = t != nullptr && t->detailed_source;
    }

#ifdef CW_SOURCE_V2_STRING_PATH_REUSE_01
    // Source string paths use immutable root TYPE and the full typed prefix,
    // never object WHO, to identify canonical Graph endpoint paths.
    // Lookup uses a non-owning view: no temporary key allocations on hits.
    struct string_path_view final {
        type_ref root{};
        type_ref array{};
        std::span<const endpoint_path_step> prefix{};
    };
    struct string_path_key final {
        type_ref root{};
        type_ref array{};
        std::vector<endpoint_path_step> prefix;
    };
    struct string_path_hash final {
        using is_transparent = void;
        [[nodiscard]] static std::size_t calculate(string_path_view k) noexcept {
            std::uint64_t hash = 14695981039346656037ULL;
            const auto mix = [&](std::uint64_t v) noexcept {
                hash ^= v;
                hash *= 1099511628211ULL;
            };
            mix(k.root.value());
            mix(k.array.value());
            mix(k.prefix.size());
            for (const auto& step : k.prefix) {
                mix(step.value);
                mix(static_cast<std::uint8_t>(step.kind));
                // Equality includes reserved[]; a hash collision cannot
                // produce a false positive.
            }
            return static_cast<std::size_t>(hash);
        }
        [[nodiscard]] std::size_t operator()(string_path_view k) const noexcept {
            return calculate(k);
        }
        [[nodiscard]] std::size_t operator()(const string_path_key& k) const noexcept {
            return calculate({k.root, k.array, k.prefix});
        }
    };
    struct string_path_equal final {
        using is_transparent = void;
        [[nodiscard]] static string_path_view view(string_path_view k) noexcept {
            return k;
        }
        [[nodiscard]] static string_path_view view(const string_path_key& k) noexcept {
            return {k.root, k.array, k.prefix};
        }
        template<class Left, class Right>
        [[nodiscard]] bool operator()(const Left& left, const Right& right) const noexcept {
            const auto a = view(left);
            const auto b = view(right);
            return a.root == b.root && a.array == b.array &&
                a.prefix.size() == b.prefix.size() &&
                std::equal(a.prefix.begin(), a.prefix.end(), b.prefix.begin());
        }
    };
    using string_path_cache_type = std::unordered_map<string_path_key,
        std::vector<endpoint_path_handle>, string_path_hash, string_path_equal>;
    static constexpr std::size_t string_path_max_prefixes = 8192;
    static constexpr std::uint64_t string_path_max_bound = 256;
#endif

    void export_input_profile(semantic_parse_telemetry& result) const noexcept {
        result.source_decode_ns += input_profile.source_decode_ns;
        result.source_intern_ns += input_profile.source_intern_ns;
        result.source_token_count += input_profile.source_token_count;
        result.source_identifier_count += input_profile.source_identifier_count;
    }

    // Do not count the declaration scan twice in the existing Source lexical
    // counters; benchmark counters continue to describe one canonical replay.
    // One Source replay only. No deferred binding and no second Graph.
    [[nodiscard]] server_status parse(file_id root) noexcept {
        if (failure) *failure = {};
        current = {};
        buffered = {};
        has_buffered = false;
        steps.clear();
        auto status = sources.begin_root(root);
        if (!succeeded(status)) return status;
#ifdef CW_SOURCE_MODEL_DIRECT_V2
        model_stream = prepared && prepared->ready();
        model_root = root;
        model_token_cursor = 0;
        if (!model_stream) {
            status = input.start(root, semantic_input_mode::source);
            if (!succeeded(status)) return input_error();
        }
#else
        status = input.start(root, semantic_input_mode::source);
        if (!succeeded(status)) return input_error();
#endif
#ifdef CW_SOURCE_MODEL_V2
        model_operations = prepared && prepared->ready()
            ? prepared->operations(root) : std::span<const source_model_operation>{};
        model_tokens = prepared && prepared->ready()
            ? prepared->tokens(root) : std::span<const source_predecoded_token>{};
        model_cursor = 0;
#endif
#ifndef CW_SOURCE_MODEL_DIRECT_V2
        if (prepared && prepared->ready())
            input.set_predecoded_source(prepared->tokens(root));
#else
        if (!model_stream && prepared && prepared->ready())
            input.set_predecoded_source(prepared->tokens(root));
#endif
        status = advance();
        if (!succeeded(status)) return status;
        status = scope(identities.root(), false, 0);
        if (!succeeded(status)) return status;
        return sources.end_root();
    }

    [[nodiscard]] server_status validate_externs() noexcept {
        const extern_declaration* earliest = nullptr;
        for (const auto& entry : extern_objects) {
            const auto& state = entry.second;
            if (state.defined) continue;
            if (!earliest || state.file.value() < earliest->file.value() ||
                (state.file == earliest->file &&
                 state.location.offset < earliest->location.offset))
                earliest = &state;
        }
        if (!earliest) return server_status::success;
        if (failure) *failure = {parser_failure_kind::semantic,
            earliest->file, earliest->location,
            "Source extern object has no definition"};
        return server_status::project_configuration_invalid;
    }

private:
    // SOURCE-V2-UNIT-BIND-01: WHO is distinct from physical producer file.
    struct source_unit final { file_id file{}; identity_ref who{}; };
    // Only explicit extern WHO are tracked; ordinary Source objects require
    // no retained operation records or replay markers.
    struct extern_declaration final {
        file_id file{};
        source_range location{};
        bool defined = false;
    };
    struct endpoint final {
        object_endpoint ref{};
        type_ref type{};
        type_ref object_type{};
        source_unit unit{};
        source_range location{};
    };

    // SOURCE-V2-G-CONSTRUCTION-01: compact, already-typed Source operation.
    // This is a stack value, not an AST or a second semantic Graph. WHO and
    // endpoint are references into the single existing G.
    enum class construction_opcode : std::uint8_t {
        initialization,
        reference_binding,
    };
    struct source_construction final {
        source_unit producer{};          // file_id + WHO; provenance and ownership
        object_endpoint target{};        // canonical typed target
        object_endpoint source{};        // used for reference binding
        type_ref target_type{};          // supplied by Header type domain
        construction_value value{};      // used for initialization
        construction_opcode opcode = construction_opcode::initialization;
    };

    // Graph mutation occurs only at canonical Source statement order.
    // Each caller still publishes Source Map and warnings in its ORIGINAL
    // order; no new Graph handles or extra Graph validation passes appear.
    [[nodiscard]] server_status commit_construction(
        const source_construction& construction,
        bool& replaced, link_handle& link) noexcept {
        replaced = false;
        link = {};
        if (!construction.producer.file || !construction.producer.who ||
            !construction.target.object ||
            construction.producer.who != construction.target.object)
            return server_status::project_configuration_invalid;
        if (construction.opcode == construction_opcode::reference_binding)
            return G.rebind_resolved_link(
                construction.source, construction.target, link, replaced);
        return G.add_resolved_initialization(
            construction.target, construction.target_type,
            construction.value, replaced);
    }

    [[nodiscard]] bool at(token_kind k) const noexcept { return current.kind == k; }
    [[nodiscard]] bool eof() const noexcept { return at(token_kind::invalid); }

    [[nodiscard]] server_status input_error() noexcept {
        const auto& e = input.failure();
        if (failure) *failure = {parser_failure_kind::lexical, e.file, e.source,
            e.detail.empty() ? std::string_view{"Source V2 lexical input failed"} : e.detail};
        return server_status::project_configuration_invalid;
    }

#ifdef CW_SOURCE_MODEL_DIRECT_V2
    [[nodiscard]] std::string_view model_spelling(std::size_t index) const noexcept {
        if (index >= model_tokens.size() || !files.content_available(model_root))
            return {};
        const auto& token = model_tokens[index];
        const auto contents = files.content(model_root);
        const auto begin = static_cast<std::size_t>(token.source_offset);
        const auto length = static_cast<std::size_t>(token.source_length);
        return begin <= contents.size() && length <= contents.size() - begin
            ? contents.substr(begin, length) : std::string_view{};
    }

    [[nodiscard]] server_status model_identifier(
        std::size_t index, string_id& output) noexcept {
        output = {};
        if (index >= model_tokens.size() ||
            model_tokens[index].kind != token_kind::identifier)
            return server_status::project_configuration_invalid;
        const auto text = model_spelling(index);
        if (text.empty()) return server_status::project_configuration_invalid;
        return strings.intern(text, output);
    }

    // Materialize one model token lazily, in canonical order. Importantly,
    // there is NO semantic_input.next()/preprocessor/lexer replay here.
    [[nodiscard]] server_status read_model_token(semantic_token& output) noexcept {
        output = {};
        if (model_token_cursor >= model_tokens.size()) return server_status::success;
        const auto index = model_token_cursor++;
        const auto& token = model_tokens[index];
        output = {model_root, token.kind, token.source_offset, token.source_length, {}};
        switch (token.kind) {
        case token_kind::pp_include:
        case token_kind::pp_define:
        case token_kind::pp_undef:
        case token_kind::pp_if:
        case token_kind::pp_ifdef:
        case token_kind::pp_ifndef:
        case token_kind::pp_elif:
        case token_kind::pp_else:
        case token_kind::pp_endif:
        case token_kind::pp_line:
        case token_kind::pp_error:
        case token_kind::pp_pragma:
        case token_kind::pp_unknown:
            if (failure) *failure = {parser_failure_kind::lexical,
                model_root, {token.source_offset, token.source_length},
                "C++ preprocessing directives are not supported in Source inputs"};
            return server_status::project_configuration_invalid;
        default: break;
        }
        ++input_profile.source_token_count;
        if (token.kind == token_kind::identifier) {
            ++input_profile.source_identifier_count;
            const auto result = model_identifier(index, output.identifier);
            if (!succeeded(result)) {
                if (failure) *failure = {parser_failure_kind::lexical,
                    model_root, {token.source_offset, token.source_length},
                    "Source Model identifier could not be interned"};
                return result;
            }
        }
        return server_status::success;
    }

    // Direct operation validation consumes already-decoded Source Model
    // ranges. No parser `advance()` calls for the skipped operands.
    [[nodiscard]] server_status model_finish_operation(
        const source_model_operation& op) noexcept {
        const auto end = static_cast<std::size_t>(op.token_begin) + op.token_count;
        if (end > model_tokens.size() || end < model_token_cursor)
            return server_status::project_configuration_invalid;
        for (auto i = model_token_cursor; i < end; ++i) {
            ++input_profile.source_token_count;
            if (model_tokens[i].kind == token_kind::identifier)
                ++input_profile.source_identifier_count;
        }
        model_token_cursor = end;
        has_buffered = false;
        buffered = {};
        return advance();
    }
#endif

    [[nodiscard]] server_status advance() noexcept {
        current = {};
        if (has_buffered) {
            current = buffered;
            buffered = {};
            has_buffered = false;
            return server_status::success;
        }
#ifdef CW_SOURCE_MODEL_DIRECT_V2
        if (model_stream) return read_model_token(current);
#endif
        const auto status = input.next(current);
        if (!succeeded(status)) return input_error();
        return server_status::success;
    }

    [[nodiscard]] server_status peek_kind(token_kind& kind) noexcept {
        if (!has_buffered) {
            buffered = {};
#ifdef CW_SOURCE_MODEL_DIRECT_V2
            if (model_stream) {
                const auto status = read_model_token(buffered);
                if (!succeeded(status)) return status;
            } else
#endif
            {
                const auto status = input.next(buffered);
                if (!succeeded(status)) return input_error();
            }
            has_buffered = true;
        }
        kind = buffered.kind;
        return server_status::success;
    }

    [[nodiscard]] server_status fail(parser_failure_kind kind,
        std::string_view detail) noexcept {
        if (failure) *failure = {kind, current.file,
            {current.source_offset, current.source_length}, detail};
        return server_status::project_configuration_invalid;
    }

    [[nodiscard]] server_status expect(token_kind k, std::string_view detail) noexcept {
        return at(k) ? server_status::success : fail(parser_failure_kind::syntax, detail);
    }

    [[nodiscard]] std::string_view spelling() const noexcept {
        if (!current.file || !files.content_available(current.file)) return {};
        const auto src = files.content(current.file);
        const auto begin = static_cast<std::size_t>(current.source_offset);
        const auto size = static_cast<std::size_t>(current.source_length);
        return begin <= src.size() && size <= src.size() - begin
            ? src.substr(begin, size) : std::string_view{};
    }

    [[nodiscard]] identity_ref find_visible(identity_ref scope, string_id name,
        identity_kind kind) const noexcept {
        for (auto parent = scope; parent;) {
            if (const auto id = identities.find(parent, name, kind)) return id;
            identity_record value;
            if (!identities.record(parent, value)) break;
            parent = value.parent;
        }
        return {};
    }

    [[nodiscard]] server_status dependency(identity_ref id) noexcept {
        return sources.add_dependency(id);
    }

    [[nodiscard]] server_status parse_namespace(identity_ref scope_id,
        std::size_t depth) noexcept {
        auto status = advance();
        if (!succeeded(status)) return status;
        if (!at(token_kind::identifier))
            return fail(parser_failure_kind::syntax, "Expected namespace name");
        for (;;) {
            identity_ref child;
            status = identities.resolve(scope_id, current.identifier,
                identity_kind::namespace_scope, child);
            if (!succeeded(status)) return status;
            scope_id = child;
            status = advance();
            if (!succeeded(status)) return status;
            if (!at(token_kind::scope)) break;
            status = advance();
            if (!succeeded(status)) return status;
            if (!at(token_kind::identifier))
                return fail(parser_failure_kind::syntax, "Expected nested namespace name");
        }
        status = expect(token_kind::l_brace, "Expected '{' after namespace");
        if (!succeeded(status)) return status;
        status = advance();
        if (!succeeded(status)) return status;
        status = this->scope(scope_id, true, depth + 1);
        if (!succeeded(status)) return status;
        status = expect(token_kind::r_brace, "Unclosed namespace");
        if (!succeeded(status)) return status;
        return advance();
    }

    [[nodiscard]] server_status named_type(identity_ref scope_id,
        type_ref& result) noexcept {
        // Single or qualified name resolved only from the committed Header G.
        if (!at(token_kind::identifier))
            return fail(parser_failure_kind::syntax, "Expected Header-defined type");
        auto type_id = find_visible(scope_id, current.identifier, identity_kind::type);
        auto namespace_id = find_visible(scope_id, current.identifier, identity_kind::namespace_scope);
        auto status = advance();
        if (!succeeded(status)) return status;
        while (at(token_kind::scope)) {
            status = advance();
            if (!succeeded(status)) return status;
            if (!at(token_kind::identifier))
                return fail(parser_failure_kind::syntax, "Expected qualified type name");
            // Only qualified namespace traversal; no implicit C++ expression parsing.
            if (!namespace_id)
                return fail(parser_failure_kind::semantic, "Qualified namespace is not defined");
            type_id = identities.find(namespace_id, current.identifier, identity_kind::type);
            namespace_id = identities.find(namespace_id, current.identifier, identity_kind::namespace_scope);
            status = advance();
            if (!succeeded(status)) return status;
        }
        const auto handle = G.find_type(type_id);
        if (!type_id || !handle)
            return fail(parser_failure_kind::semantic, "Source type is absent in Header G");
        status = dependency(type_id);
        if (!succeeded(status)) return status;
        const auto* definition = G.find(handle);
        if (!definition)
            return fail(parser_failure_kind::semantic, "Invalid Header type definition");
        // PARSER-V2-SOURCE-06-INTRINSIC-ALIAS: the OLD Parser and Header V2
        // fold an intrinsic typedef (e.g. `byte`) into its canonical scalar
        // type_ref. Source must do the same BEFORE constructing an object,
        // reference, array, or endpoint; do not relax link compatibility.
        result = definition->kind == graph_type_kind::intrinsic_alias
            ? G.intrinsic(definition->alias_intrinsic())
            : G.named(type_id);
        return result ? server_status::success :
            fail(parser_failure_kind::semantic, "Invalid Header type reference");
    }

    // Same restricted C++ intrinsic spellings as the existing Header/OLD Parser.
    // A Source may declare objects, but cannot define types or parse expressions.
    [[nodiscard]] static constexpr bool builtin_start(token_kind kind) noexcept {
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

    [[nodiscard]] bool intrinsic_start() const noexcept {
        return builtin_start(current.kind) ||
            (at(token_kind::identifier) &&
             strings.get(current.identifier) == "__int64");
    }

    [[nodiscard]] server_status parse_intrinsic(intrinsic_type& output) noexcept {
        output = intrinsic_type::none;
        bool is_signed = false;
        bool is_unsigned = false;
        bool is_short = false;
        unsigned long_count = 0;

        while (at(token_kind::kw_signed) || at(token_kind::kw_unsigned) ||
               at(token_kind::kw_short) || at(token_kind::kw_long)) {
            if (at(token_kind::kw_signed)) {
                if (is_signed || is_unsigned)
                    return fail(parser_failure_kind::syntax,
                        "Invalid signed/unsigned type specifier combination");
                is_signed = true;
            }
            else if (at(token_kind::kw_unsigned)) {
                if (is_signed || is_unsigned)
                    return fail(parser_failure_kind::syntax,
                        "Invalid signed/unsigned type specifier combination");
                is_unsigned = true;
            }
            else if (at(token_kind::kw_short)) {
                if (is_short || long_count)
                    return fail(parser_failure_kind::syntax,
                        "Invalid short/long type specifier combination");
                is_short = true;
            }
            else {
                if (is_short || long_count == 2)
                    return fail(parser_failure_kind::syntax,
                        "Invalid long type specifier combination");
                ++long_count;
            }
            const auto status = advance();
            if (!succeeded(status)) return status;
        }

        if (at(token_kind::identifier) &&
            strings.get(current.identifier) == "__int64") {
            if (is_short || long_count)
                return fail(parser_failure_kind::syntax,
                    "__int64 cannot use short/long modifiers");
            output = is_unsigned ? intrinsic_type::unsigned_long_long
                : intrinsic_type::signed_long_long;
            return advance();
        }
        if (at(token_kind::kw_char)) {
            if (is_short || long_count)
                return fail(parser_failure_kind::syntax,
                    "char cannot use short/long modifiers");
            output = is_unsigned ? intrinsic_type::unsigned_char
                : is_signed ? intrinsic_type::signed_char : intrinsic_type::char_type;
            return advance();
        }
        if (at(token_kind::kw_double)) {
            if (is_signed || is_unsigned || is_short || long_count > 1)
                return fail(parser_failure_kind::syntax,
                    "Invalid double type specifier combination");
            output = long_count ? intrinsic_type::long_double_type
                : intrinsic_type::double_type;
            return advance();
        }
        if (at(token_kind::kw_int)) {
            const auto status = advance();
            if (!succeeded(status)) return status;
        }
        else if (!is_signed && !is_unsigned && !is_short && !long_count) {
            switch (current.kind) {
            case token_kind::kw_void: output = intrinsic_type::void_type; break;
            case token_kind::kw_bool: output = intrinsic_type::bool_type; break;
            case token_kind::kw_wchar_t: output = intrinsic_type::wchar_type; break;
            case token_kind::kw_char8_t: output = intrinsic_type::char8_type; break;
            case token_kind::kw_char16_t: output = intrinsic_type::char16_type; break;
            case token_kind::kw_char32_t: output = intrinsic_type::char32_type; break;
            case token_kind::kw_float: output = intrinsic_type::float_type; break;
            default:
                return fail(parser_failure_kind::syntax,
                    "Expected supported Source intrinsic type");
            }
            return advance();
        }
        else if (builtin_start(current.kind)) {
            // A modifier was present but the following builtin cannot follow it.
            return fail(parser_failure_kind::syntax,
                "Invalid Source intrinsic type combination");
        }

        if (long_count == 2)
            output = is_unsigned ? intrinsic_type::unsigned_long_long
                : intrinsic_type::signed_long_long;
        else if (long_count == 1)
            output = is_unsigned ? intrinsic_type::unsigned_long
                : intrinsic_type::signed_long;
        else if (is_short)
            output = is_unsigned ? intrinsic_type::unsigned_short
                : intrinsic_type::signed_short;
        else
            output = is_unsigned ? intrinsic_type::unsigned_int
                : intrinsic_type::signed_int;
        return server_status::success;
    }

    [[nodiscard]] server_status parse_type(identity_ref scope_id,
        type_ref& output) noexcept {
        output = {};
        bool is_const = false;
        bool is_volatile = false;
        auto qualifiers = [this, &is_const, &is_volatile]() noexcept -> server_status {
            while (at(token_kind::kw_const) || at(token_kind::kw_volatile)) {
                is_const |= at(token_kind::kw_const);
                is_volatile |= at(token_kind::kw_volatile);
                const auto status = advance();
                if (!succeeded(status)) return status;
            }
            return server_status::success;
        };
        auto status = qualifiers();
        if (!succeeded(status)) return status;

        if (intrinsic_start()) {
            intrinsic_type value = intrinsic_type::none;
            status = parse_intrinsic(value);
            if (!succeeded(status)) return status;
            if (value == intrinsic_type::void_type)
                return fail(parser_failure_kind::semantic,
                    "Source objects cannot have void type");
            output = G.intrinsic(value);
        }
        else if (at(token_kind::identifier)) {
            status = named_type(scope_id, output);
            if (!succeeded(status)) return status;
        }
        else return fail(parser_failure_kind::unsupported,
            "Source requires an existing Header type or intrinsic type");

        status = qualifiers();
        if (!succeeded(status)) return status;
        if (!output) return fail(parser_failure_kind::semantic,
            "Source type is absent in Graph G");

        if (is_const) {
            type_ref wrapped;
            status = G.derive_resolved(output, derived_type_kind::const_qualified, 0, wrapped);
            if (!succeeded(status)) return status;
            output = wrapped;
        }
        if (is_volatile) {
            type_ref wrapped;
            status = G.derive_resolved(output, derived_type_kind::volatile_qualified, 0, wrapped);
            if (!succeeded(status)) return status;
            output = wrapped;
        }
        return server_status::success;
    }

    [[nodiscard]] server_status scalar(construction_value& output) noexcept {
        output = {};
        if (at(token_kind::kw_true) || at(token_kind::kw_false) ||
            at(token_kind::kw_nullptr)) {
            if (at(token_kind::kw_true))
                output = construction_value::constant(construction_kind::unsigned_integer, 1);
            return advance();
        }
        bool negative = false;
        if (at(token_kind::plus) || at(token_kind::minus)) {
            negative = at(token_kind::minus);
            auto status = advance();
            if (!succeeded(status)) return status;
        }
        if (!at(token_kind::pp_number))
            return fail(parser_failure_kind::unsupported,
                "Source initializer requires a scalar constant");
        const auto text = spelling();
        if (text.empty()) return fail(parser_failure_kind::syntax, "Empty number");
        const bool real = text.find_first_of(".eE") != std::string_view::npos;
        if (real) {
            double number = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(),
                number, std::chars_format::general);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                return fail(parser_failure_kind::unsupported, "Invalid floating literal");
            if (negative) number = -number;
            output = construction_value::constant(construction_kind::real,
                std::bit_cast<std::uint64_t>(number));
        }
        else {
            std::uint64_t value = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 10);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                return fail(parser_failure_kind::unsupported, "Only decimal integer literals are supported");
            if (negative) {
                if (value > (std::uint64_t{1} << 63))
                    return fail(parser_failure_kind::semantic, "Signed integer overflows");
                output = construction_value::constant(construction_kind::signed_integer,
                    std::uint64_t{} - value);
            }
            else output = construction_value::constant(construction_kind::unsigned_integer, value);
        }
        return advance();
    }

    [[nodiscard]] server_status object(identity_ref scope_id,
        bool is_extern = false) noexcept {
        const auto declaration_file = current.file;
        const source_range declaration_location{
            current.source_offset, current.source_length};
        type_ref type;
        auto status = parse_type(scope_id, type);
        if (!succeeded(status)) return status;

        // A Source reference is an object with a reference type in the SAME G.
        // It may be bound by a later statement: bool& R; R = S[21][0];
        // Header already resolved all involved type definitions.
        const bool is_reference = at(token_kind::ampersand) ||
            at(token_kind::logical_and);
        if (is_reference) {
            const auto kind = at(token_kind::ampersand)
                ? derived_type_kind::lvalue_reference
                : derived_type_kind::rvalue_reference;
            type_ref reference_type;
            status = G.derive_resolved(type, kind, 0, reference_type);
            if (!succeeded(status) || !reference_type)
                return fail(parser_failure_kind::semantic,
                    "Source reference type cannot be represented in Graph G");
            type = reference_type;
            status = advance();
            if (!succeeded(status)) return status;
        }

        if (!at(token_kind::identifier))
            return fail(parser_failure_kind::syntax, "Expected object name");
        const auto object_file = current.file;
        const auto name = current.identifier;
        status = advance();
        if (!succeeded(status)) return status;

        // C++ object declarator: bool S[123][52] creates ONE array object.
        // Bounds are spelled outermost-to-innermost; Graph types are derived
        // in reverse so indexing S[122][51] resolves bool, not bool[123].
        // Fixed stack storage avoids an allocation per Source declaration.
        std::array<std::uint64_t, 32> array_extents{};
        std::size_t array_rank = 0;
        if (is_reference && at(token_kind::l_bracket))
            return fail(parser_failure_kind::unsupported,
                "Arrays of references are not valid Source object declarations");
        while (at(token_kind::l_bracket)) {
            if (array_rank == array_extents.size())
                return fail(parser_failure_kind::unsupported,
                    "Source array declarator has too many dimensions");
            status = advance();
            if (!succeeded(status)) return status;
            if (!at(token_kind::pp_number))
                return fail(parser_failure_kind::syntax,
                    "Source array bound requires a decimal integer literal");
            const auto literal = spelling();
            std::uint64_t bound = 0;
            const auto parsed = std::from_chars(
                literal.data(), literal.data() + literal.size(), bound, 10);
            if (parsed.ec != std::errc{} ||
                parsed.ptr != literal.data() + literal.size() || bound == 0)
                return fail(parser_failure_kind::semantic,
                    "Source array bound must be a positive decimal integer");
            array_extents[array_rank++] = bound;
            status = advance();
            if (!succeeded(status)) return status;
            status = expect(token_kind::r_bracket,
                "Expected ']' after Source array bound");
            if (!succeeded(status)) return status;
            status = advance();
            if (!succeeded(status)) return status;
        }
        for (std::size_t i = array_rank; i != 0; --i) {
            type_ref wrapped;
            status = G.derive_resolved(
                type, derived_type_kind::bounded_array, array_extents[i - 1], wrapped);
            if (!succeeded(status))
                return fail(parser_failure_kind::semantic,
                    "Source array type cannot be represented in Graph G");
            type = wrapped;
        }

        // Extern is only a typed WHO declaration, never an initializer.
        if (is_extern && (at(token_kind::assign) || at(token_kind::l_brace)))
            return fail(parser_failure_kind::unsupported,
                "Extern Source object cannot have an initializer");
        construction_value init{};
        std::uint32_t flags = 0;
        token_kind close = token_kind::invalid;
        if (at(token_kind::assign)) {
            status = advance();
            if (!succeeded(status)) return status;
            if (at(token_kind::l_brace)) { close = token_kind::r_brace;
                status = advance(); if (!succeeded(status)) return status; }
            if (!at(token_kind::r_brace)) {
                status = scalar(init);
                if (!succeeded(status)) return status;
                flags = graph_object_non_default_initializer;
            }
        }
        else if (at(token_kind::l_brace)) {
            close = token_kind::r_brace;
            status = advance(); if (!succeeded(status)) return status;
            if (!at(token_kind::r_brace)) {
                status = scalar(init); if (!succeeded(status)) return status;
                flags = graph_object_non_default_initializer;
            }
        }
        if (close != token_kind::invalid) {
            status = expect(close, "Expected '}' after object initializer");
            if (!succeeded(status)) return status;
            status = advance(); if (!succeeded(status)) return status;
        }
        status = expect(token_kind::semicolon, "Expected ';' after object");
        if (!succeeded(status)) return status;
        if (flags && !construction_compatible(G, type, init))
            return fail(parser_failure_kind::semantic, "Object initializer type mismatch");
        identity_ref id;
        status = identities.resolve(scope_id, name, identity_kind::object, id);
        if (!succeeded(status)) return status;
        object_handle result;
        const auto existing = G.find_object(id);
        auto declared = extern_objects.find(id.value());
        if (is_extern) {
            if (existing) {
                const auto* entry = G.find(existing);
                if (!entry || entry->type != type)
                    return fail(parser_failure_kind::semantic,
                        "Extern Source object type conflicts with existing WHO");
                result = existing;
            } else {
                status = G.add_resolved_object(id, type, result);
                if (!succeeded(status)) return fail(parser_failure_kind::semantic,
                    "Cannot register extern Source WHO");
            }
            if (declared == extern_objects.end()) {
                try {
                    extern_objects.emplace(id.value(), extern_declaration{
                        declaration_file, declaration_location, bool(existing)});
                } catch (...) { return server_status::io_error; }
            }
            // An extern declaration is not an object definition and does not
            // produce a Source Map definition contribution.
        } else {
            if (existing) {
                if (declared == extern_objects.end() || declared->second.defined)
                    return fail(parser_failure_kind::semantic,
                        "Duplicate Source object definition");
                status = G.complete_resolved_extern_object(
                    id, type, result, flags, init);
                if (!succeeded(status)) return fail(parser_failure_kind::semantic,
                    "Source definition conflicts with extern WHO");
                declared->second.defined = true;
            } else {
                status = G.add_resolved_object(id, type, result, flags, init);
                if (!succeeded(status)) return fail(parser_failure_kind::semantic,
                    "Source object conflicts with existing G");
            }
            const source_unit unit{object_file, id};
            status = sources.add(unit.file, source_data_ref::object(unit.who));
            if (!succeeded(status)) return status;
            if (telemetry) ++telemetry->source_object_statements;
        }
        return advance();
    }

    [[nodiscard]] static type_ref strip_cv(const graph& G, type_ref type) noexcept {
        derived_type_record d;
        while (G.derived(type, d) &&
            (d.kind == derived_type_kind::const_qualified ||
             d.kind == derived_type_kind::volatile_qualified)) type = d.child;
        return type;
    }

    [[nodiscard]] bool reference_referent(type_ref type, type_ref& referent) const noexcept {
        derived_type_record d;
        type = strip_cv(G, type);
        if (!G.derived(type, d) ||
            (d.kind != derived_type_kind::lvalue_reference &&
             d.kind != derived_type_kind::rvalue_reference)) return false;
        referent = d.child;
        return true;
    }

    // SOURCE-V2-FAST-BIND-02: the Header type/member catalog is immutable
    // for the whole Source pass. Cache resolved typed transitions, NOT text.
    // Every hit contains the exact member, defining owner and base ancestry;
    // member, dependency, endpoint-path and Graph mutation semantics stay intact.
    struct member_transition final {
        std::uint64_t key = 0;
        member_index member{};
        type_handle owner{};
        type_ref value_type{};
        std::uint32_t base_begin = 0;
        std::uint32_t base_count = 0;
    };

    static constexpr std::size_t member_transition_slots = 1u << 16;

    [[nodiscard]] static std::uint64_t transition_key(
        type_handle record, string_id name) noexcept {
        return (static_cast<std::uint64_t>(record.value()) << 32) |
            static_cast<std::uint64_t>(name.value());
    }

    [[nodiscard]] static std::size_t transition_slot(std::uint64_t key) noexcept {
        // Strong 64-bit mixing: low slot bits must depend on both the type and
        // member ID. A direct-mapped table has no per-lookup allocations.
        key ^= key >> 33;
        key *= 0xff51afd7ed558ccdULL;
        key ^= key >> 33;
        key *= 0xc4ceb9fe1a85ec53ULL;
        key ^= key >> 33;
        return static_cast<std::size_t>(key) & (member_transition_slots - 1);
    }

    [[nodiscard]] bool cached_transition(type_handle record, string_id name,
        member_index& member, type_handle& owner, type_ref& value_type,
        std::vector<endpoint_path_step>& ancestry) noexcept {
        if (member_transitions.empty()) return false;
        const auto key = transition_key(record, name);
        const auto& entry = member_transitions[transition_slot(key)];
        if (!key || entry.key != key || !entry.member) return false;
        if (entry.base_count != 0) {
            const auto begin = static_cast<std::size_t>(entry.base_begin);
            const auto count = static_cast<std::size_t>(entry.base_count);
            if (begin > transition_bases.size() ||
                count > transition_bases.size() - begin) return false;
            try {
                ancestry.insert(ancestry.end(),
                    transition_bases.begin() + begin,
                    transition_bases.begin() + begin + count);
            } catch (...) { return false; }
        }
        member = entry.member;
        owner = entry.owner;
        value_type = entry.value_type;
        return true;
    }

    void remember_transition(type_handle record, string_id name,
        member_index member, type_handle owner, type_ref value_type,
        std::span<const endpoint_path_step> base_steps) noexcept {
        if (!record || !name || !member || !owner || !value_type) return;
        // One allocation for the whole table. If it fails, parsing proceeds
        // through the original uncached Graph lookup path.
        if (member_transitions.empty()) {
            if (transition_allocation_attempted) return;
            transition_allocation_attempted = true;
            try { member_transitions.resize(member_transition_slots); }
            catch (...) { return; }
        }
        std::uint32_t begin = 0;
        if (!base_steps.empty()) {
            constexpr auto max_u32 =
                static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)());
            if (transition_bases.size() > max_u32 - base_steps.size()) return;
            begin = static_cast<std::uint32_t>(transition_bases.size());
            try {
                transition_bases.insert(transition_bases.end(),
                    base_steps.begin(), base_steps.end());
            } catch (...) { return; }
        }
        const auto key = transition_key(record, name);
        member_transitions[transition_slot(key)] = {
            key, member, owner, value_type, begin,
            static_cast<std::uint32_t>(base_steps.size())};
    }

    [[nodiscard]] server_status find_inherited(type_handle record, string_id name,
        member_index& index, type_handle& owner,
        std::vector<endpoint_path_step>& ancestry, unsigned depth = 0) noexcept {
        if (depth >= 64) return fail(parser_failure_kind::unsupported,
            "Inherited member path exceeds maximum depth");
        if (telemetry) ++telemetry->source_member_lookups;
        index = G.find_member(record, name);
        if (index) { owner = record; return server_status::success; }
        const auto bases = G.bases(record);
        for (std::size_t i = 0; i < bases.size(); ++i) {
            const auto base = G.find_type(bases[i].type);
            if (!base) continue;
            std::vector<endpoint_path_step> local;
            try {
                local = ancestry;
                local.push_back({i, endpoint_path_step_kind::base, {}});
            } catch (...) { return server_status::io_error; }
            member_index found;
            type_handle found_owner;
            auto status = find_inherited(base, name, found, found_owner, local, depth + 1);
            if (!succeeded(status)) return status;
            if (found) { ancestry = std::move(local); index = found;
                owner = found_owner; return server_status::success; }
        }
        return server_status::success;
    }

#ifdef CW_SOURCE_MODEL_DIRECT_V2
    // The Source Model already gives the exact statement and '=' boundary.
    // Shape checking is read-only, before any Graph writes or interning.
    [[nodiscard]] bool model_endpoint_shape(
        std::size_t begin, std::size_t end) const noexcept {
        if (begin >= end || model_tokens[begin].kind != token_kind::identifier)
            return false;
        for (auto i = begin + 1; i < end;) {
            const auto kind = model_tokens[i++].kind;
            if (kind == token_kind::dot) {
                if (i >= end || model_tokens[i++].kind != token_kind::identifier)
                    return false;
            } else if (kind == token_kind::l_bracket) {
                if (i + 1 >= end ||
                    model_tokens[i++].kind != token_kind::pp_number ||
                    model_tokens[i++].kind != token_kind::r_bracket)
                    return false;
            } else return false;
        }
        return true;
    }

    [[nodiscard]] bool model_fast_assignment_shape(
        const source_model_operation& op) const noexcept {
        if (!model_stream || op.opcode != source_model_opcode::assignment ||
            op.token_count < 4 || op.assignment_token == 0xffffffffu ||
            op.assignment_token + 2 > op.token_count) return false;
        const auto b = static_cast<std::size_t>(op.token_begin);
        const auto e = b + op.token_count;
        const auto equal = b + op.assignment_token;
        if (e > model_tokens.size() ||
            model_tokens[e - 1].kind != token_kind::semicolon ||
            model_tokens[equal].kind != token_kind::assign ||
            !model_endpoint_shape(b, equal)) return false;
        const auto rhs = equal + 1;
        if (rhs >= e - 1) return false;
        if (model_tokens[rhs].kind == token_kind::identifier)
            return model_endpoint_shape(rhs, e - 1);
        const auto value_tokens = (e - 1) - rhs;
        if (value_tokens == 1) {
            const auto kind = model_tokens[rhs].kind;
            return kind == token_kind::pp_number || kind == token_kind::kw_true ||
                kind == token_kind::kw_false || kind == token_kind::kw_nullptr;
        }
        return value_tokens == 2 &&
            (model_tokens[rhs].kind == token_kind::plus ||
             model_tokens[rhs].kind == token_kind::minus) &&
            model_tokens[rhs + 1].kind == token_kind::pp_number;
    }

    // Validate a model endpoint exactly once against the immutable Header type
    // domain and already-published Source object WHO. The final path is interned
    // directly into G. Syntax comes from the model, not semantic_input.next().
    [[nodiscard]] server_status model_direct_endpoint(
        identity_ref scope_id, std::size_t begin, std::size_t end,
        endpoint& output) noexcept {
        string_id object_name;
        auto status = server_status::success;
        if (begin + 1 == model_token_cursor &&
            current.kind == token_kind::identifier &&
            current.source_offset == model_tokens[begin].source_offset)
            object_name = current.identifier;
        else {
            status = model_identifier(begin, object_name);
            if (!succeeded(status)) return status;
        }
        const auto who = find_visible(scope_id, object_name, identity_kind::object);
        const auto handle = G.find_object(who);
        const auto* object = G.find(handle);
        if (!who || !object) return fail(parser_failure_kind::semantic,
            "Source object is not defined in G");
        output = {};
        output.unit = source_unit{model_root, who};
        output.type = object->type;
        output.object_type = object->type;
        output.location = {model_tokens[begin].source_offset,
            model_tokens[begin].source_length};
        status = dependency(who);
        if (!succeeded(status)) return status;
        steps.clear();
        auto& path = steps;
        for (auto i = begin + 1; i < end;) {
            if (path.size() >= 128)
                return fail(parser_failure_kind::unsupported,
                    "Source endpoint too deep");
            if (model_tokens[i].kind == token_kind::l_bracket) {
                derived_type_record derived;
                if (!G.derived(strip_cv(G, output.type), derived) ||
                    derived.kind != derived_type_kind::bounded_array)
                    return fail(parser_failure_kind::semantic,
                        "Index requires bounded array");
                const auto text = model_spelling(i + 1);
                std::uint64_t index_value = 0;
                const auto parsed = std::from_chars(
                    text.data(), text.data() + text.size(), index_value);
                if (parsed.ec != std::errc{} ||
                    parsed.ptr != text.data() + text.size() ||
                    index_value >= derived.payload)
                    return fail(parser_failure_kind::semantic,
                        "Array index out of range");
                try { path.push_back({index_value,
                    endpoint_path_step_kind::array_index, {}}); }
                catch (...) { return server_status::io_error; }
                output.type = derived.child;
                i += 3;
                continue;
            }
            // Syntactically known '.identifier' step.
            type_ref selected = strip_cv(G, output.type);
            type_ref referent;
            if (reference_referent(selected, referent)) {
                try { path.push_back({0,
                    endpoint_path_step_kind::dereference, {}}); }
                catch (...) { return server_status::io_error; }
                selected = strip_cv(G, referent);
            }
            type_handle record;
            if (!G.named(selected, record))
                return fail(parser_failure_kind::semantic,
                    "Member access requires record type");
            status = dependency(G.identity(record));
            if (!succeeded(status)) return status;
            const auto member_position = i + 1;
            string_id member_name;
            status = model_identifier(member_position, member_name);
            if (!succeeded(status)) return status;
            member_index member;
            type_handle owner;
            type_ref member_type;
            const auto ancestry_begin = path.size();
            if (!cached_transition(record, member_name, member,
                    owner, member_type, path)) {
                status = find_inherited(record, member_name, member, owner, path);
                if (!succeeded(status)) return status;
                if (!member) return fail(parser_failure_kind::semantic,
                    "Member not found in Header G");
                const auto* value = G.member(owner, member);
                if (!value) return fail(parser_failure_kind::semantic,
                    "Missing member definition");
                member_type = value->type;
                remember_transition(record, member_name, member, owner,
                    member_type,
                    std::span<const endpoint_path_step>{path}.subspan(ancestry_begin));
            }
            if (owner != record) {
                status = dependency(G.identity(owner));
                if (!succeeded(status)) return status;
            }
            try { path.push_back({member.value(),
                endpoint_path_step_kind::member, {}}); }
            catch (...) { return server_status::io_error; }
            output.type = member_type;
            output.location = {model_tokens[member_position].source_offset,
                model_tokens[member_position].source_length};
            i += 2;
        }
        if (telemetry) {
            ++telemetry->source_endpoint_count;
            telemetry->source_endpoint_steps += path.size();
            if (path.size() == 1 &&
                path.front().kind == endpoint_path_step_kind::member)
                ++telemetry->source_endpoint_direct_members;
            else ++telemetry->source_endpoint_path_endpoints;
            for (const auto& step : path) {
                switch (step.kind) {
                case endpoint_path_step_kind::member:
                    ++telemetry->source_endpoint_member_steps; break;
                case endpoint_path_step_kind::array_index:
                    ++telemetry->source_endpoint_array_steps; break;
                case endpoint_path_step_kind::dereference:
                    ++telemetry->source_endpoint_dereference_steps; break;
                case endpoint_path_step_kind::base:
                    ++telemetry->source_endpoint_base_steps; break;
                default: break;
                }
            }
        }
        if (path.size() == 1 &&
            path.front().kind == endpoint_path_step_kind::member) {
            output.ref = {who, endpoint_ref::from_raw(
                static_cast<std::uint32_t>(path.front().value))};
        } else {
            endpoint_path_handle result;
            status = G.intern_resolved_endpoint_path(
                object->type, path, output.type, result);
            if (!succeeded(status)) return fail(parser_failure_kind::semantic,
                "Invalid Source endpoint path");
            output.ref = {who, endpoint_ref::from_path(result)};
        }
        return server_status::success;
    }

    [[nodiscard]] bool model_scalar_value(
        std::size_t begin, std::size_t end,
        construction_value& value) const noexcept {
        value = {};
        if (begin + 1 == end) {
            const auto kind = model_tokens[begin].kind;
            if (kind == token_kind::kw_true) {
                value = construction_value::constant(construction_kind::unsigned_integer, 1);
                return true;
            }
            if (kind == token_kind::kw_false || kind == token_kind::kw_nullptr)
                return true;
        }
        bool negative = false;
        if (begin + 2 == end &&
            (model_tokens[begin].kind == token_kind::plus ||
             model_tokens[begin].kind == token_kind::minus)) {
            negative = model_tokens[begin].kind == token_kind::minus;
            ++begin;
        }
        if (begin + 1 != end || model_tokens[begin].kind != token_kind::pp_number)
            return false;
        const auto literal = model_spelling(begin);
        if (literal.empty()) return false;
        if (literal.find_first_of(".eE") != std::string_view::npos) {
            double number = 0;
            const auto parsed = std::from_chars(literal.data(), literal.data() + literal.size(),
                number, std::chars_format::general);
            if (parsed.ec != std::errc{} || parsed.ptr != literal.data() + literal.size())
                return false;
            if (negative) number = -number;
            value = construction_value::constant(construction_kind::real,
                std::bit_cast<std::uint64_t>(number));
            return true;
        }
        std::uint64_t number = 0;
        const auto parsed = std::from_chars(literal.data(), literal.data() + literal.size(), number, 10);
        if (parsed.ec != std::errc{} || parsed.ptr != literal.data() + literal.size())
            return false;
        if (negative) {
            if (number > (std::uint64_t{1} << 63)) return false;
            value = construction_value::constant(construction_kind::signed_integer,
                std::uint64_t{} - number);
        } else {
            value = construction_value::constant(construction_kind::unsigned_integer, number);
        }
        return true;
    }

    [[nodiscard]] server_status model_validate_assignment(
        identity_ref scope_id, const source_model_operation& op,
        bool& consumed) noexcept {
        consumed = false;
        if (!model_fast_assignment_shape(op)) return server_status::success;
        // Preserve strict pre-declaration diagnostics from the canonical path.
        if (!find_visible(scope_id, current.identifier, identity_kind::object))
            return server_status::success;
        const auto b = static_cast<std::size_t>(op.token_begin);
        const auto e = b + op.token_count;
        const auto equal = b + op.assignment_token;
        const auto rhs = equal + 1;
        const bool is_link_syntax = model_tokens[rhs].kind == token_kind::identifier;
        construction_value scalar_value;
        if (!is_link_syntax && !model_scalar_value(rhs, e - 1, scalar_value))
            return server_status::success; // Preserve the exact old numeric diagnostic.
        endpoint target;
        auto status = model_direct_endpoint(scope_id, b, equal, target);
        if (!succeeded(status)) return status;
        type_ref referent;
        const bool target_is_reference = reference_referent(target.type, referent);
        if (target_is_reference != is_link_syntax)
            return server_status::success; // Let existing validator report mismatch.
        if (is_link_syntax) {
            endpoint source;
            status = model_direct_endpoint(scope_id, rhs, e - 1, source);
            if (!succeeded(status)) return status;
            type_ref source_type = source.type;
            type_ref inner;
            if (reference_referent(source_type, inner)) source_type = inner;
            if (referent != source_type) return server_status::success;
            const source_construction c{target.unit, target.ref, source.ref,
                target.type, {}, construction_opcode::reference_binding};
            link_handle link;
            bool replaced = false;
            status = commit_construction(c, replaced, link);
            if (!succeeded(status)) return fail(parser_failure_kind::semantic,
                "Source reference binding cannot be represented in G");
            status = sources.add(target.unit.file, source_data_ref::link(link));
            if (!succeeded(status)) return status;
            if (telemetry) ++telemetry->source_link_statements;
            if (replaced) {
                status = warn_rebound(target);
                if (!succeeded(status)) return status;
            }
        } else {
            if (!construction_compatible(G, target.type, scalar_value))
                return server_status::success;
            const source_construction c{target.unit, target.ref, {},
                target.type, scalar_value, construction_opcode::initialization};
            link_handle link;
            bool replaced = false;
            status = commit_construction(c, replaced, link);
            if (!succeeded(status)) return status;
            status = sources.add_initialization(target.ref);
            if (!succeeded(status)) return status;
            status = sources.add(target.unit.file, source_data_ref::object(target.unit.who));
            if (!succeeded(status)) return status;
            if (telemetry) ++telemetry->source_value_assignment_statements;
            if (replaced) {
                status = warn_replaced(target);
                if (!succeeded(status)) return status;
            }
        }
        if (telemetry) ++telemetry->source_model_direct_assignments;
        consumed = true;
        return model_finish_operation(op);
    }
#endif
    // SOURCE-V2-FAST-BIND-01: scope() already resolved the target WHO.
    // Source operands still resolve from scope_id; no semantic lookup is skipped.
    [[nodiscard]] server_status resolve_endpoint(identity_ref scope_id,
        endpoint& output, identity_ref prebound_object = {}) noexcept {
        if (!at(token_kind::identifier))
            return fail(parser_failure_kind::syntax, "Expected Source object identifier");
        const auto id = prebound_object ? prebound_object
            : find_visible(scope_id, current.identifier, identity_kind::object);
        const auto h = G.find_object(id);
        const auto* obj = G.find(h);
        if (!id || !obj)
            return fail(parser_failure_kind::semantic, "Source object is not defined in G");
        output = {};
        output.unit = source_unit{current.file, id};
        output.type = obj->type;
        output.object_type = obj->type;
        output.location = {current.source_offset, current.source_length};
        auto status = dependency(id);
        if (!succeeded(status)) return status;
        steps.clear();
        status = advance();
        if (!succeeded(status)) return status;
        while (at(token_kind::dot) || at(token_kind::l_bracket)) {
            if (steps.size() >= 128)
                return fail(parser_failure_kind::unsupported, "Source endpoint too deep");
            if (at(token_kind::l_bracket)) {
                derived_type_record d;
                if (!G.derived(strip_cv(G, output.type), d) ||
                    d.kind != derived_type_kind::bounded_array)
                    return fail(parser_failure_kind::semantic, "Index requires bounded array");
                status = advance(); if (!succeeded(status)) return status;
                if (!at(token_kind::pp_number))
                    return fail(parser_failure_kind::syntax, "Array index requires decimal integer");
                const auto text = spelling();
                std::uint64_t value = 0;
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
                    value >= d.payload)
                    return fail(parser_failure_kind::semantic, "Array index out of range");
                try { steps.push_back({value, endpoint_path_step_kind::array_index, {}}); }
                catch (...) { return server_status::io_error; }
                output.type = d.child;
                status = advance(); if (!succeeded(status)) return status;
                status = expect(token_kind::r_bracket, "Expected ']'");
                if (!succeeded(status)) return status;
                status = advance(); if (!succeeded(status)) return status;
                continue;
            }
            type_ref selected = strip_cv(G, output.type);
            type_ref referent;
            if (reference_referent(selected, referent)) {
                try { steps.push_back({0, endpoint_path_step_kind::dereference, {}}); }
                catch (...) { return server_status::io_error; }
                selected = strip_cv(G, referent);
            }
            type_handle record;
            if (!G.named(selected, record))
                return fail(parser_failure_kind::semantic, "Member access requires record type");
            status = dependency(G.identity(record));
            if (!succeeded(status)) return status;
            status = advance(); if (!succeeded(status)) return status;
            if (!at(token_kind::identifier))
                return fail(parser_failure_kind::syntax, "Expected member identifier after '.'");
            member_index member;
            type_handle owner;
            type_ref member_type;
            // The current member name was interned by the Source cursor. Reuse
            // the semantic transition instead of repeating Graph member search
            // and inherited traversal on every occurrence of the same chain.
            const auto ancestry_begin = steps.size();
            if (!cached_transition(record, current.identifier, member,
                    owner, member_type, steps)) {
                status = find_inherited(record, current.identifier,
                    member, owner, steps);
                if (!succeeded(status)) return status;
                if (!member) return fail(parser_failure_kind::semantic,
                    "Member not found in Header G");
                const auto* value = G.member(owner, member);
                if (!value) return fail(parser_failure_kind::semantic,
                    "Missing member definition");
                member_type = value->type;
                remember_transition(record, current.identifier, member, owner,
                    member_type, std::span<const endpoint_path_step>{steps}.subspan(
                        ancestry_begin));
            }
            if (owner != record) { status = dependency(G.identity(owner));
                if (!succeeded(status)) return status; }
            try { steps.push_back({member.value(), endpoint_path_step_kind::member, {}}); }
            catch (...) { return server_status::io_error; }
            output.type = member_type;
            output.location = {current.source_offset, current.source_length};
            status = advance(); if (!succeeded(status)) return status;
        }
        // Zero-clock path shape counters, active in profiling only.
        if (telemetry) {
            ++telemetry->source_endpoint_count;
            telemetry->source_endpoint_steps += steps.size();
            if (steps.size() == 1 && steps.front().kind == endpoint_path_step_kind::member)
                ++telemetry->source_endpoint_direct_members;
            else
                ++telemetry->source_endpoint_path_endpoints;
            for (const auto& step : steps) {
                switch (step.kind) {
                case endpoint_path_step_kind::member:
                    ++telemetry->source_endpoint_member_steps; break;
                case endpoint_path_step_kind::array_index:
                    ++telemetry->source_endpoint_array_steps; break;
                case endpoint_path_step_kind::dereference:
                    ++telemetry->source_endpoint_dereference_steps; break;
                case endpoint_path_step_kind::base:
                    ++telemetry->source_endpoint_base_steps; break;
                default: break;
                }
            }
        }
        if (steps.size() == 1 && steps.front().kind == endpoint_path_step_kind::member)
            output.ref = {id, endpoint_ref::from_raw(
                static_cast<std::uint32_t>(steps.front().value))};
        else {
            endpoint_path_handle path;
            status = G.intern_resolved_endpoint_path(obj->type, steps, output.type, path);
            if (!succeeded(status))
                return fail(parser_failure_kind::semantic, "Invalid Source endpoint path");
            output.ref = {id, endpoint_ref::from_path(path)};
        }
        return server_status::success;
    }

    [[nodiscard]] server_status warn_replaced(const endpoint& target) noexcept {
        if (!warnings) return server_status::success;
        try { warnings->push_back({parser_warning_kind::duplicate_initialization,
                target.unit.file, target.location, "Duplicate Source initializer; last value wins"}); }
        catch (...) { return server_status::io_error; }
        return server_status::success;
    }

    [[nodiscard]] server_status warn_rebound(const endpoint& target) noexcept {
        if (!warnings) return server_status::success;
        try {
            warnings->push_back({parser_warning_kind::duplicate_initialization,
                target.unit.file, target.location,
                "Duplicate Source reference binding; last binding wins"});
        } catch (...) { return server_status::io_error; }
        return server_status::success;
    }

    [[nodiscard]] server_status string_literal(std::vector<std::uint8_t>& bytes,
        std::uint64_t bound) noexcept {
        for (; at(token_kind::string_literal);) {
            const auto text = spelling();
            if (text.size() < 2 || text.front() != '"' || text.back() != '"')
                return fail(parser_failure_kind::unsupported, "Only narrow string literals are supported");
            for (std::size_t i = 1; i + 1 < text.size(); ++i) {
                unsigned char b = static_cast<unsigned char>(text[i]);
                if (b == '\\') {
                    if (++i + 1 >= text.size())
                        return fail(parser_failure_kind::syntax, "Incomplete string escape");
                    switch (text[i]) {
                    case 'n': b = 10; break;
                    case 'r': b = 13; break;
                    case 't': b = 9; break;
                    case '\\': b = '\\'; break;
                    case '"': b = '"'; break;
                    case '\'': b = '\''; break;
                    case 'a': b = 7; break;
                    case 'b': b = 8; break;
                    case 'f': b = 12; break;
                    case 'v': b = 11; break;
                    default: {
                        const auto digit = [](char c, bool hex) noexcept {
                            return (c >= '0' && c <= (hex ? '9' : '7')) ||
                                (hex && ((c >= 'a' && c <= 'f') ||
                                         (c >= 'A' && c <= 'F')));
                        };
                        const bool hex = text[i] == 'x';
                        if (!hex && !digit(text[i], false))
                            return fail(parser_failure_kind::unsupported,
                                "Unsupported Source string escape");
                        std::size_t begin = hex ? i + 1 : i;
                        std::size_t end = begin;
                        while (end + 1 < text.size() &&
                            digit(text[end], hex) &&
                            (hex || end - begin < 3)) ++end;
                        if (end == begin)
                            return fail(parser_failure_kind::syntax,
                                "Empty hexadecimal string escape");
                        unsigned int code = 0;
                        const auto parsed = std::from_chars(text.data() + begin,
                            text.data() + end, code, hex ? 16 : 8);
                        if (parsed.ec != std::errc{} ||
                            parsed.ptr != text.data() + end || code > 255)
                            return fail(parser_failure_kind::semantic,
                                "String escape exceeds byte range");
                        b = static_cast<unsigned char>(code);
                        i = end - 1;
                        break;
                    }
                    }
                }
                if (bytes.size() + 1 >= bound)
                    return fail(parser_failure_kind::semantic, "String exceeds char array bound");
                try { bytes.push_back(b); } catch (...) { return server_status::io_error; }
            }
            auto status = advance(); if (!succeeded(status)) return status;
        }
        return server_status::success;
    }

    [[nodiscard]] server_status assign_string(const endpoint& target) noexcept {
        derived_type_record array;
        intrinsic_type ch;
        if (!G.derived(strip_cv(G, target.type), array) ||
            array.kind != derived_type_kind::bounded_array || !array.payload ||
            !G.intrinsic(array.child, ch) || ch != intrinsic_type::char_type)
            return fail(parser_failure_kind::semantic,
                "String initializer requires bounded char array");
        std::vector<std::uint8_t> bytes;
        auto status = string_literal(bytes, array.payload);
        if (!succeeded(status)) return status;
        status = expect(token_kind::semicolon, "Expected ';' after string");
        if (!succeeded(status)) return status;
        std::vector<endpoint_path_step> path_steps;
        try {
            path_steps = steps;
            path_steps.push_back({0, endpoint_path_step_kind::array_index, {}});
        } catch (...) { return server_status::io_error; }
#ifdef CW_SOURCE_V2_STRING_PATH_REUSE_01
        std::vector<endpoint_path_handle>* cached_elements = nullptr;
        if (array.payload <= string_path_max_bound) {
            try {
                const string_path_view lookup{target.object_type, target.type,
                    std::span<const endpoint_path_step>{steps}};
                auto found = string_path_cache.find(lookup);
                if (found != string_path_cache.end()) {
                    cached_elements = &found->second;
                } else if (string_path_cache.size() < string_path_max_prefixes) {
                    string_path_key key;
                    key.root = target.object_type;
                    key.array = target.type;
                    key.prefix.assign(steps.begin(), steps.end());
                    std::vector<endpoint_path_handle> handles(
                        static_cast<std::size_t>(array.payload));
                    auto inserted = string_path_cache.try_emplace(
                        std::move(key), std::move(handles));
                    cached_elements = &inserted.first->second;
                }
            } catch (...) {
                // Cache allocation failure: original Graph path remains valid.
                cached_elements = nullptr;
            }
        }
#endif
        bool any_replaced = false;
        for (std::uint64_t i = 0; i < array.payload; ++i) {
            path_steps.back().value = i;
            endpoint_path_handle path;
#ifdef CW_SOURCE_V2_STRING_PATH_REUSE_01
            const auto slot = static_cast<std::size_t>(i);
            if (cached_elements && (*cached_elements)[slot]) {
                path = (*cached_elements)[slot];
            } else {
                status = G.intern_resolved_endpoint_path(target.object_type,
                    path_steps, array.child, path);
                if (!succeeded(status)) return status;
                if (cached_elements) (*cached_elements)[slot] = path;
            }
#else
            status = G.intern_resolved_endpoint_path(target.object_type,
                path_steps, array.child, path);
            if (!succeeded(status)) return status;
#endif
            const object_endpoint element{target.unit.who, endpoint_ref::from_path(path)};
            const auto value = construction_value::constant(construction_kind::unsigned_integer,
                i < bytes.size() ? bytes[static_cast<std::size_t>(i)] : 0);
            const source_construction construction{
                target.unit, element, {}, array.child, value,
                construction_opcode::initialization};
            bool replaced = false;
            link_handle ignored_link;
            status = commit_construction(construction, replaced, ignored_link);
            if (!succeeded(status)) return status;
            any_replaced |= replaced;
            status = sources.add_initialization(element);
            if (!succeeded(status)) return status;
        }
        status = sources.add(target.unit.file, source_data_ref::object(target.unit.who));
        if (!succeeded(status)) return status;
        if (any_replaced) { status = warn_replaced(target);
            if (!succeeded(status)) return status; }
        return advance();
    }

    [[nodiscard]] server_status assignment(identity_ref scope_id,
        source_unit unit) noexcept {
        endpoint target;
        auto status = resolve_endpoint(scope_id, target, unit.who);
        if (!succeeded(status)) return status;
        status = expect(token_kind::assign, "Expected '=' after Source target");
        if (!succeeded(status)) return status;
        status = advance(); if (!succeeded(status)) return status;
        type_ref referent;
        if (reference_referent(target.type, referent)) {
            endpoint source;
            status = resolve_endpoint(scope_id, source);
            if (!succeeded(status)) return status;
            type_ref source_type = source.type;
            type_ref inner;
            if (reference_referent(source_type, inner)) source_type = inner;
            if (referent != source_type) {
                // PARSER-V2-SOURCE-05-TYPE-DIAG: report canonical G type
                // expressions without weakening reference-binding semantics.
                char target_type_text[160];
                char referent_text[160];
                char source_type_text[160];
                char expression_text[160];
                source_v2_type_description(G, identities, strings,
                    target.type, target_type_text, sizeof(target_type_text));
                source_v2_type_description(G, identities, strings,
                    referent, referent_text, sizeof(referent_text));
                source_v2_type_description(G, identities, strings,
                    source.type, source_type_text, sizeof(source_type_text));
                source_v2_type_description(G, identities, strings,
                    source_type, expression_text, sizeof(expression_text));
                // parser_failure.detail is string_view: the buffer MUST outlive
                // this parser instance and the caller's diagnostic formatting.
                static thread_local char message[1000];
                std::snprintf(message, sizeof(message),
                    "Reference link types differ: target=%s[%u], referent=%s[%u], "
                    "source=%s[%u], source-expression=%s[%u]",
                    target_type_text, target.type.value(),
                    referent_text, referent.value(),
                    source_type_text, source.type.value(),
                    expression_text, source_type.value());
                return fail(parser_failure_kind::semantic, message);
            }
            status = expect(token_kind::semicolon, "Expected ';' after Source link");
            if (!succeeded(status)) return status;
            // Resolve endpoints once, then commit typed construction by WHO.
            const source_construction construction{
                target.unit, target.ref, source.ref, target.type, {},
                construction_opcode::reference_binding};
            link_handle link;
            bool replaced = false;
            status = commit_construction(construction, replaced, link);
            if (!succeeded(status)) return fail(parser_failure_kind::semantic,
                "Source reference binding cannot be represented in G");
            status = sources.add(target.unit.file, source_data_ref::link(link));
            if (!succeeded(status)) return status;
            if (telemetry) ++telemetry->source_link_statements;
            if (replaced) {
                status = warn_rebound(target);
                if (!succeeded(status)) return status;
            }
            return advance();
        }
        if (at(token_kind::string_literal)) return assign_string(target);
        if (at(token_kind::identifier))
            return fail(parser_failure_kind::unsupported,
                "EnumType::Value requires an enum declaration in Header Graph");
        construction_value value;
        status = scalar(value);
        if (!succeeded(status)) return status;
        status = expect(token_kind::semicolon, "Expected ';' after Source value");
        if (!succeeded(status)) return status;
        if (!construction_compatible(G, target.type, value))
            return fail(parser_failure_kind::semantic, "Source initializer type mismatch");
        const source_construction construction{
            target.unit, target.ref, {}, target.type, value,
            construction_opcode::initialization};
        bool replaced = false;
        link_handle ignored_link;
        status = commit_construction(construction, replaced, ignored_link);
        if (!succeeded(status)) return status;
        status = sources.add_initialization(target.ref);
        if (!succeeded(status)) return status;
        status = sources.add(target.unit.file, source_data_ref::object(target.unit.who));
        if (!succeeded(status)) return status;
        if (telemetry) ++telemetry->source_value_assignment_statements;
        if (replaced) { status = warn_replaced(target);
            if (!succeeded(status)) return status; }
        return advance();
    }

    [[nodiscard]] server_status scope(identity_ref scope_id, bool close,
        std::size_t depth) noexcept {
        if (depth >= parser_scope_depth_limit)
            return fail(parser_failure_kind::unsupported, "Source namespaces nested too deep");
        for (;;) {
            if (eof()) return close
                ? fail(parser_failure_kind::syntax, "Unclosed Source namespace")
                : server_status::success;
            if (close && at(token_kind::r_brace)) {
#ifdef CW_SOURCE_MODEL_V2
                (void)next_model_opcode();
#endif
                return server_status::success;
            }
#ifdef CW_SOURCE_MODEL_V2
            const auto model_opcode = next_model_opcode();
#else
            constexpr auto model_opcode = source_model_opcode::statement;
#endif
            if (at(token_kind::semicolon)) {
                auto status = advance(); if (!succeeded(status)) return status;
                continue;
            }
            if (at(token_kind::kw_namespace)) {
                auto status = parse_namespace(scope_id, depth);
                if (!succeeded(status)) return status;
                continue;
            }
            if (at(token_kind::kw_extern)) {
                auto status = advance();
                if (!succeeded(status)) return status;
                status = object(scope_id, true);
                if (!succeeded(status)) return status;
                continue;
            }
#ifdef CW_SOURCE_MODEL_DIRECT_V2
            if (model_stream && model_opcode == source_model_opcode::assignment &&
                model_cursor != 0 && model_cursor <= model_operations.size()) {
                bool consumed = false;
                const auto status = model_validate_assignment(
                    scope_id, model_operations[model_cursor - 1], consumed);
                if (!succeeded(status)) return status;
                if (consumed) continue;
            }
#endif
            if (at(token_kind::identifier)) {
                // Syntax-model shape 'object .member', 'object[index]' or
                // 'object = literal' cannot be an ordinary Source declarator.
                // Resolve WHO once; do not repeat type/namespace dispatch.
                if (model_opcode == source_model_opcode::assignment) {
                    const auto who = find_visible(scope_id, current.identifier,
                        identity_kind::object);
                    if (who) {
                        const source_unit unit{current.file, who};
                        const auto status = assignment(scope_id, unit);
                        if (!succeeded(status)) return status;
                        continue;
                    }
                    // Undeclared or invalid syntax: preserve original
                    // semantic/syntax diagnostics through the old dispatch.
                }
                // An existing WHO is an assignment target, except when the
                // same spelling also names a Header type or namespace and the
                // statement has declarator syntax (e.g. `Signal Signal;`).
                const auto target = find_visible(scope_id, current.identifier,
                    identity_kind::object);
                if (target) {
                    const bool also_type =
                        bool(find_visible(scope_id, current.identifier,
                            identity_kind::type)) ||
                        bool(find_visible(scope_id, current.identifier,
                            identity_kind::namespace_scope));
                    if (also_type) {
                        token_kind next = token_kind::invalid;
                        auto status = peek_kind(next);
                        if (!succeeded(status)) return status;
                        if (next != token_kind::dot &&
                            next != token_kind::l_bracket &&
                            next != token_kind::assign) {
                            status = object(scope_id);
                            if (!succeeded(status)) return status;
                            continue;
                        }
                    }
                    const source_unit unit{current.file, target};
                    const auto status = assignment(scope_id, unit);
                    if (!succeeded(status)) return status;
                    continue;
                }
                if (!intrinsic_start() &&
                    !find_visible(scope_id, current.identifier, identity_kind::type) &&
                    !find_visible(scope_id, current.identifier,
                        identity_kind::namespace_scope))
                    return fail(parser_failure_kind::semantic,
                        "Source object must be declared before use (or declared extern)");
            }
            if (at(token_kind::identifier) || at(token_kind::kw_const) ||
                at(token_kind::kw_volatile) || builtin_start(current.kind)) {
                const auto status = object(scope_id);
                if (!succeeded(status)) return status;
                continue;
            }
            return fail(parser_failure_kind::unsupported,
                "Source supports only declared objects, extern declarations, assignments, links and namespaces");
        }
    }

    file_context& files;
    // Must outlive input: semantic_input holds a non-owning telemetry pointer.
    semantic_input_telemetry input_profile{};
    semantic_input input;
    string_table& strings;
    identity_space& identities;
    graph& G;
    source_map& sources;
    parser_failure* failure;
    std::vector<parser_warning>* warnings;
    semantic_parse_telemetry* telemetry;
    const source_predecode_batch* prepared = nullptr;
#ifdef CW_SOURCE_MODEL_V2
    // Raw model is immutable and owned by worker-built source_predecode_batch.
    std::span<const source_model_operation> model_operations{};
    std::span<const source_predecoded_token> model_tokens{};
    std::size_t model_cursor = 0;
#ifdef CW_SOURCE_MODEL_DIRECT_V2
    bool model_stream = false;
    file_id model_root{};
    std::size_t model_token_cursor = 0;
#endif

    // Consult per-file syntactic operation boundaries. On any mismatch the
    // validator simply reverts to the original grammar dispatch, preserving
    // the canonical semantic error path without speculative Graph writes.
    [[nodiscard]] source_model_opcode next_model_opcode() noexcept {
        if (model_cursor >= model_operations.size())
            return source_model_opcode::statement;
        const auto& op = model_operations[model_cursor];
        if (op.token_begin >= model_tokens.size() ||
            model_tokens[op.token_begin].source_offset != current.source_offset) {
            model_operations = {};
            return source_model_opcode::statement;
        }
        ++model_cursor;
        return op.opcode;
    }
#endif
    // Transient, sparse WHO declarations; no per-statement AST or replay.
    std::unordered_map<std::uint32_t, extern_declaration> extern_objects;
    semantic_token current{};
    semantic_token buffered{};
    bool has_buffered = false;
    std::vector<endpoint_path_step> steps;
    // Typed transitions are reused across Source roots; G's Header types are
    // immutable for this pass. Cache/pool are never persisted to compiled.bin.
#ifdef CW_SOURCE_V2_STRING_PATH_REUSE_01
    // Source-pass-local cache: never serialized; no profiler counters.
    string_path_cache_type string_path_cache;
#endif
    std::vector<member_transition> member_transitions;
    std::vector<endpoint_path_step> transition_bases;
    bool transition_allocation_attempted = false;
};

} // namespace

server_status parse_source_roots_v2(
    file_context& files, lexical_generation& lexical,
    std::size_t frontend_root_count,
    const preprocessor_configuration& configuration,
    string_table& strings, identity_space& identities,
    graph& G, source_map& sources, parser_failure* failure,
    std::vector<parser_warning>* warnings,
    semantic_parse_telemetry* telemetry) noexcept {
    using clock = std::chrono::steady_clock;
    const auto started = telemetry ? clock::now() : clock::time_point{};
    source_predecode_batch decoded;
#if defined(CW_SOURCE_PREDECODE_PARALLEL_V2) || defined(CW_SOURCE_MODEL_V2)
    // If a lane, input or allocation fails, the original direct cursor
    // transparently runs instead, preserving diagnostics and semantics.
    // All Header writes have completed; no Source worker mutates strings.
    (void)decoded.prepare(files, lexical, strings, frontend_root_count);
#endif
    const auto replay_started = telemetry ? clock::now() : clock::time_point{};
    if (telemetry && decoded.ready())
        telemetry->source_root_setup_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                replay_started - started).count());
    source_parser_v2 parser{files, lexical, configuration, strings,
        identities, G, sources, failure, warnings, telemetry,
        decoded.ready() ? &decoded : nullptr};
    // Canonical one-pass Source binding: one G, direct commitments.
    // Undeclared forward references fail when encountered.
    for (std::size_t i = 0; i < frontend_root_count; ++i) {
        const file_id root{static_cast<std::uint32_t>(i + 1)};
        if (files.kind(root) != file_kind::source) continue;
        if (telemetry) ++telemetry->source_root_count;
        const auto status = parser.parse(root);
        if (!succeeded(status)) return status;
    }
    const auto externs_valid = parser.validate_externs();
    if (!succeeded(externs_valid)) return externs_valid;
    if (telemetry) {
        parser.export_input_profile(*telemetry);
        if (decoded.ready())
            telemetry->source_replay_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    clock::now() - replay_started).count());
        telemetry->source_ns =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                clock::now() - started).count());
    }
    return server_status::success;
}

} // namespace cw::server
