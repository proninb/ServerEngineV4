#include "project/frontend/directive_executor.hpp"
#include "project/frontend/lexer.hpp"
#include "project/frontend/lexical_symbols_v2.hpp"
#include "project/frontend/prepared_frontend_v2.hpp"
#include "project/frontend/prepared_include_v2.hpp"
#include "project/frontend/preprocessor_v2.hpp"
#include "project/frontend/lexical_generation.hpp"
#include "project/frontend/semantic_input.hpp"
#include "project/graph/graph.hpp"
#include "project/graph/graph_delta.hpp"
#include "project/parser/header_semantic_space.hpp"
#include "project/parser/parser.hpp"
#include "project/parser/header_parser_v2.hpp"
#include "project/preprocessor/preprocessor.hpp"
#include "project/preprocessor_configuration.hpp"
#include "project/semantic/identity.hpp"
#include "project/source/source_map.hpp"
#include "project/string/string_table.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <array>
#include <bit>
#include <iostream>
#include <vector>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cw::server {
namespace {

class test_state final {
public:
    [[nodiscard]] bool expect(
        bool condition,
        std::string_view name) {

        if (condition) {
            return true;
        }

        ++failures;
        std::cerr << "FAIL: " << name << '\n';
        return false;
    }

    std::size_t failures = 0;
};

class temporary_source final {
public:
    temporary_source(
        std::string_view label,
        std::string_view content) {

        const auto nonce =
            std::chrono::steady_clock::now().
                time_since_epoch().count();

        path_value =
            std::filesystem::temp_directory_path() /
            ("server_engine_v4_" +
             std::string{label} +
             "_" +
             std::to_string(nonce) +
             ".hpp");

        std::ofstream stream{
            path_value,
            std::ios::binary |
                std::ios::trunc};

        if (!stream) {
            throw std::runtime_error{
                "Cannot create temporary frontend source"};
        }

        stream.write(
            content.data(),
            static_cast<std::streamsize>(
                content.size()));

        if (!stream) {
            throw std::runtime_error{
                "Cannot write temporary frontend source"};
        }
    }

    temporary_source(
        const temporary_source&) = delete;

    temporary_source& operator=(
        const temporary_source&) = delete;

    ~temporary_source() {
        std::error_code error;
        std::filesystem::remove(
            path_value,
            error);
    }

    [[nodiscard]] const std::filesystem::path&
    path() const noexcept {
        return path_value;
    }

private:
    std::filesystem::path path_value;
};

[[nodiscard]] bool prepare_root(
    test_state& tests,
    const std::filesystem::path& path,
    file_context& files,
    lexical_generation& lexical,
    file_id& root,
    file_kind kind = file_kind::header) {

    root = {};

    if (!tests.expect(
            succeeded(
                files.resolve(
                    path,
                    kind,
                    root)) &&
                root,
            "resolve frontend root")) {

        return false;
    }

    file_acquire_job job;

    if (!tests.expect(
            succeeded(
                files.prepare_acquire(
                    root,
                    job)),
            "prepare frontend root acquisition")) {

        return false;
    }

    file_acquire_result result;
    file_context::execute_acquire(
        job,
        result);

    bool changed = false;

    if (!tests.expect(
            result.kind ==
                file_acquire_result_kind::present &&
            succeeded(
                files.apply_acquire(
                    result,
                    changed)) &&
            files.content_available(root),
            "materialize frontend root")) {

        return false;
    }

    if (!tests.expect(
            succeeded(
                lexical.reset(
                    files.size(),
                    1)),
            "reset lexical generation")) {

        return false;
    }

    lexical_stream stream;
    lexical_error error;

    if (!tests.expect(
            succeeded(
                lexer::tokenize(
                    root,
                    files.content(root),
                    stream,
                    &error)),
            "tokenize frontend root")) {

        return false;
    }

    return tests.expect(
        succeeded(
            lexical.publish(
                root,
                0,
                stream)),
        "publish frontend root lexical state");
}

[[nodiscard]] bool prepare_all_roots(
    test_state& tests,
    file_context& files,
    lexical_generation& lexical,
    std::span<const file_id> roots) {

    if (!tests.expect(
            succeeded(
                lexical.reset(
                    files.size(),
                    1)),
            "reset multi-root lexical generation")) {

        return false;
    }

    for (const auto root : roots) {
        file_acquire_job job;

        if (!tests.expect(
                succeeded(
                    files.prepare_acquire(
                        root,
                        job)),
                "prepare multi-root acquisition")) {

            return false;
        }

        file_acquire_result result;
        file_context::execute_acquire(
            job,
            result);

        bool changed = false;

        if (!tests.expect(
                result.kind ==
                    file_acquire_result_kind::present &&
                succeeded(
                    files.apply_acquire(
                        result,
                        changed)) &&
                files.content_available(root),
                "materialize multi-root input")) {

            return false;
        }

        lexical_stream stream;
        lexical_error error;

        if (!tests.expect(
                succeeded(
                    lexer::tokenize(
                        root,
                        files.content(root),
                        stream,
                        &error)) &&
                succeeded(
                    lexical.publish(
                        root,
                        0,
                        stream)),
                "publish multi-root lexical state")) {

            return false;
        }
    }

    return true;
}

[[nodiscard]] server_status parse_file(
    test_state& tests,
    const std::filesystem::path& path,
    parser_failure& failure) {

    file_context files;
    lexical_generation lexical;
    file_id root;

    if (!prepare_root(
            tests,
            path,
            files,
            lexical,
            root)) {

        return server_status::
            project_configuration_invalid;
    }

    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;

    return parse_semantic_project(
        files, lexical, 1, configuration, strings, identities, G, sources, &failure);
}



void test_lexical_symbols_v2(
    test_state& tests) {

    constexpr std::string_view first_source =
        "Value Value fR69jFyF int Qfjmj25K;";

    constexpr std::string_view second_source =
        "Output Value Input Qfjmj25K fR69jFyF;";

    lexical_stream first_lexical;
    lexical_stream second_lexical;

    lexical_symbol_stream_v2 first_symbols;
    lexical_symbol_stream_v2 second_symbols;

    lexical_error first_error;
    lexical_error second_error;

    if (!tests.expect(
            succeeded(
                lexer::tokenize(
                    file_id{1},
                    first_source,
                    first_lexical,
                    &first_error,
                    &first_symbols)) &&
            succeeded(
                lexer::tokenize(
                    file_id{2},
                    second_source,
                    second_lexical,
                    &second_error,
                    &second_symbols)),
            "Lexer V2 records file-local symbols without global state")) {

        return;
    }

    const auto collision_left =
        first_symbols.identifier_symbol(2);

    const auto collision_right =
        first_symbols.identifier_symbol(3);

    const auto* collision_left_record =
        first_symbols.symbol(
            collision_left);

    const auto* collision_right_record =
        first_symbols.symbol(
            collision_right);

    tests.expect(
        first_symbols.identifier_count() == 4 &&
        first_symbols.symbol_count() == 3 &&
        first_symbols.identifier_symbol(0) ==
            first_symbols.identifier_symbol(1),
        "Lexer V2 deduplicates repeated identifiers inside one file");

    tests.expect(
        collision_left &&
        collision_right &&
        collision_left != collision_right &&
        collision_left_record != nullptr &&
        collision_right_record != nullptr &&
        collision_left_record->hash ==
            collision_right_record->hash,
        "Lexer V2 keeps exact spellings distinct across an FNV32 collision");

    const std::array<lexical_symbol_source_v2, 2>
        forward{
            lexical_symbol_source_v2{
                &first_symbols,
                first_source},
            lexical_symbol_source_v2{
                &second_symbols,
                second_source},
        };

    string_table forward_strings;

    std::vector<lexical_symbol_resolution_v2>
        forward_resolution;

    if (!tests.expect(
            succeeded(
                merge_lexical_symbols_v2(
                    forward,
                    forward_strings,
                    forward_resolution)) &&
            forward_resolution.size() == 2 &&
            forward_strings.size() == 5,
            "Lexer V2 deterministically merges unique local spellings")) {

        return;
    }

    const auto value =
        forward_strings.find("Value");

    const auto first_collision =
        forward_strings.find("fR69jFyF");

    const auto second_collision =
        forward_strings.find("Qfjmj25K");

    tests.expect(
        value &&
        first_collision &&
        second_collision &&
        first_collision !=
            second_collision &&
        forward_resolution[0].
            resolve_identifier(
                first_symbols,
                0) == value &&
        forward_resolution[0].
            resolve_identifier(
                first_symbols,
                1) == value &&
        forward_resolution[0].
            resolve_identifier(
                first_symbols,
                2) == first_collision &&
        forward_resolution[0].
            resolve_identifier(
                first_symbols,
                3) == second_collision,
        "Lexer V2 resolves identifier occurrences directly through local symbols");

    const std::array<lexical_symbol_source_v2, 2>
        reverse{
            lexical_symbol_source_v2{
                &second_symbols,
                second_source},
            lexical_symbol_source_v2{
                &first_symbols,
                first_source},
        };

    string_table reverse_strings;

    std::vector<lexical_symbol_resolution_v2>
        reverse_resolution;

    if (!tests.expect(
            succeeded(
                merge_lexical_symbols_v2(
                    reverse,
                    reverse_strings,
                    reverse_resolution)) &&
            reverse_resolution.size() == 2 &&
            reverse_strings.size() ==
                forward_strings.size(),
            "Lexer V2 merge accepts reversed file completion order")) {

        return;
    }

    for (const auto spelling : {
             std::string_view{"Value"},
             std::string_view{"Input"},
             std::string_view{"Output"},
             std::string_view{"fR69jFyF"},
             std::string_view{"Qfjmj25K"}}) {

        const auto forward_id =
            forward_strings.find(
                spelling);

        const auto reverse_id =
            reverse_strings.find(
                spelling);

        tests.expect(
            forward_id &&
            reverse_id &&
            forward_id.value() ==
                reverse_id.value(),
            "Lexer V2 global string_id is independent of file completion order");
    }
}


void test_prepared_include_v2(
    test_state& tests) {

    const temporary_source included{
        "prepared_include_leaf",
        "struct Included { int Value; };\n"};

    const auto include_name =
        included.path().
            filename().
            string();

    const std::string root_text =
        "#include \"" +
        include_name +
        "\"\n"
        "struct Root { Included* Value; };\n";

    const temporary_source root_source{
        "prepared_include_root",
        root_text};

    file_context files;
    file_id root;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    root_source.path(),
                    file_kind::header,
                    root)) &&
            root,
            "resolve prepared-include V2 root")) {

        return;
    }

    preprocessor_configuration configuration;
    configuration.root_directory =
        root_source.path().
            parent_path();

    prepared_include_closure_v2 closure;
    prepared_include_failure_v2 failure;

    const std::array<file_id, 1>
        roots{root};

    if (!tests.expect(
            succeeded(
                prepare_header_include_closure_v2(
                    files,
                    roots,
                    configuration,
                    closure,
                    &failure)),
            "prepare existing direct include closure before semantic replay")) {

        return;
    }

    const auto view =
        closure.view();

    const auto* root_file =
        view.file(
            root);

    const auto* include =
        root_file != nullptr &&
            root_file->includes.size() == 1
        ? &root_file->includes[0]
        : nullptr;

    const auto* included_file =
        include != nullptr &&
            include->target
        ? view.file(
            include->target)
        : nullptr;

    tests.expect(
        closure.file_count() == 2 &&
        root_file != nullptr &&
        root_file->symbols != nullptr &&
        include != nullptr &&
        include->state ==
            prepared_include_state_v2::ready &&
        included_file != nullptr &&
        included_file->symbols != nullptr &&
        !included_file->words.empty(),
        "prepared include closure owns lexical/local-symbol state for target");

    tests.expect(
        files.dependencies(
            root).empty() &&
        (include == nullptr ||
         !include->target ||
         files.dependencies(
             include->target).empty()),
        "speculative include preparation publishes no semantic dependency edges");

    const auto nonce =
        std::chrono::steady_clock::now().
            time_since_epoch().
            count();

    const auto missing_name =
        "__server_engine_v4_missing_prepared_v2_" +
        std::to_string(
            nonce) +
        ".hpp";

    const std::string missing_text =
        "#ifdef NEVER\n"
        "#include \"" +
        missing_name +
        "\"\n"
        "#endif\n"
        "struct Keep { int X; };\n";

    const temporary_source missing_root_source{
        "prepared_include_missing",
        missing_text};

    file_context missing_files;
    file_id missing_root;

    if (!tests.expect(
            succeeded(
                missing_files.resolve(
                    missing_root_source.path(),
                    file_kind::header,
                    missing_root)) &&
            missing_root,
            "resolve missing prepared-include V2 root")) {

        return;
    }

    preprocessor_configuration
        missing_configuration;

    missing_configuration.root_directory =
        missing_root_source.path().
            parent_path();

    prepared_include_closure_v2
        missing_closure;

    const std::array<file_id, 1>
        missing_roots{
            missing_root};

    if (!tests.expect(
            succeeded(
                prepare_header_include_closure_v2(
                    missing_files,
                    missing_roots,
                    missing_configuration,
                    missing_closure,
                    &failure)),
            "speculative missing include does not fail physical preparation")) {

        return;
    }

    const auto missing_view =
        missing_closure.view();

    const auto* missing_file =
        missing_view.file(
            missing_root);

    const auto* missing_include =
        missing_file != nullptr &&
            missing_file->includes.size() == 1
        ? &missing_file->includes[0]
        : nullptr;

    tests.expect(
        missing_closure.file_count() == 1 &&
        missing_include != nullptr &&
        missing_include->state ==
            prepared_include_state_v2::missing &&
        !missing_include->target &&
        missing_files.dependencies(
            missing_root).empty(),
        "inactive-capable missing include is recorded without semantic publication");
}


void test_preprocessor_v2_include_execution(
    test_state& tests) {

    // Active include: #define/#ifdef executes, the already-prepared target is
    // canonicalized only when entered, dependency publication is semantic, and
    // Header Parser V2 sees the included declaration before the root resumes.
    const temporary_source included{
        "preprocessor_v2_active_leaf",
        "#pragma once\n"
        "struct Included { int Value; };\n"};

    const auto include_name =
        included.path().
            filename().
            string();

    const std::string active_text =
        "#define ENABLE_INCLUDE\n"
        "#ifdef ENABLE_INCLUDE\n"
        "#include \"" +
        include_name +
        "\"\n"
        "#endif\n"
        "struct Root { Included* Value; };\n";

    const temporary_source active_root_source{
        "preprocessor_v2_active_root",
        active_text};

    file_context active_files;
    file_id active_root;

    if (!tests.expect(
            succeeded(
                active_files.resolve(
                    active_root_source.path(),
                    file_kind::header,
                    active_root)) &&
            active_root,
            "resolve PREPROCESSOR-V2 active root")) {

        return;
    }

    preprocessor_configuration
        active_configuration;

    active_configuration.root_directory =
        active_root_source.path().
            parent_path();

    prepared_include_closure_v2
        active_closure;

    prepared_include_failure_v2
        preparation_failure;

    const std::array<file_id, 1>
        active_roots{
            active_root};

    if (!tests.expect(
            succeeded(
                prepare_header_include_closure_v2(
                    active_files,
                    active_roots,
                    active_configuration,
                    active_closure,
                    &preparation_failure)) &&
            active_closure.file_count() == 2,
            "prepare PREPROCESSOR-V2 active include closure")) {

        return;
    }

    const auto active_view =
        active_closure.view();

    const auto* active_root_file =
        active_view.file(
            active_root);

    const auto* active_include =
        active_root_file != nullptr &&
            active_root_file->includes.size() == 1
        ? &active_root_file->includes[0]
        : nullptr;

    if (!tests.expect(
            active_include != nullptr &&
            active_include->state ==
                prepared_include_state_v2::ready &&
            active_include->target,
            "PREPROCESSOR-V2 active include has prepared target")) {

        return;
    }

    const auto active_target =
        active_include->target;

    string_table active_strings;

    preprocessor_v2_failure
        active_preprocessing_failure;

    semantic_preprocessor_v2
        active_input{
            active_files,
            active_view,
            active_configuration,
            active_strings,
            &active_preprocessing_failure};

    if (!tests.expect(
            succeeded(
                active_input.start(
                    active_root)),
            "start PREPROCESSOR-V2 active include replay")) {

        return;
    }

    identity_space active_identities{
        active_strings};

    graph active_graph;

    parser_v2_failure
        active_parser_failure;

    header_parser_v2 active_parser{
        active_input,
        active_identities,
        active_graph,
        &active_parser_failure};

    if (!tests.expect(
            succeeded(
                active_parser.parse()) &&
            active_input.finished(),
            "PREPROCESSOR-V2 active include reaches Header Parser V2")) {

        return;
    }

    if (!tests.expect(
            succeeded(
                active_files.
                    finalize_dependency_topology()),
            "finalize PREPROCESSOR-V2 active dependency topology")) {

        return;
    }

    const auto active_dependencies =
        active_files.dependencies(
            active_root);

    tests.expect(
        active_dependencies.size() == 1 &&
        active_dependencies[0] ==
            active_target &&
        active_strings.find(
            "Included") &&
        active_strings.find(
            "Root") &&
        active_graph.find_type(
            active_identities.find(
                active_identities.root(),
                active_strings.find(
                    "Included"),
                identity_kind::type)) &&
        active_graph.find_type(
            active_identities.find(
                active_identities.root(),
                active_strings.find(
                    "Root"),
                identity_kind::type)),
        "active include publishes dependency and semantic target");

    // Inactive existing include: physical preparation is allowed, but semantic
    // replay must neither publish the edge nor allocate the target's spellings.
    const temporary_source unused{
        "preprocessor_v2_unused_leaf",
        "struct Unused { int Hidden; };\n"};

    const auto unused_name =
        unused.path().
            filename().
            string();

    const std::string inactive_text =
        "#ifdef NEVER\n"
        "#include \"" +
        unused_name +
        "\"\n"
        "#endif\n"
        "struct Keep { int X; };\n";

    const temporary_source inactive_root_source{
        "preprocessor_v2_inactive_root",
        inactive_text};

    file_context inactive_files;
    file_id inactive_root;

    if (!tests.expect(
            succeeded(
                inactive_files.resolve(
                    inactive_root_source.path(),
                    file_kind::header,
                    inactive_root)) &&
            inactive_root,
            "resolve PREPROCESSOR-V2 inactive root")) {

        return;
    }

    preprocessor_configuration
        inactive_configuration;

    inactive_configuration.root_directory =
        inactive_root_source.path().
            parent_path();

    prepared_include_closure_v2
        inactive_closure;

    const std::array<file_id, 1>
        inactive_roots{
            inactive_root};

    if (!tests.expect(
            succeeded(
                prepare_header_include_closure_v2(
                    inactive_files,
                    inactive_roots,
                    inactive_configuration,
                    inactive_closure,
                    &preparation_failure)) &&
            inactive_closure.file_count() == 2,
            "prepare speculative inactive include target")) {

        return;
    }

    string_table inactive_strings;

    preprocessor_v2_failure
        inactive_preprocessing_failure;

    semantic_preprocessor_v2
        inactive_input{
            inactive_files,
            inactive_closure.view(),
            inactive_configuration,
            inactive_strings,
            &inactive_preprocessing_failure};

    if (!tests.expect(
            succeeded(
                inactive_input.start(
                    inactive_root)),
            "start PREPROCESSOR-V2 inactive replay")) {

        return;
    }

    identity_space inactive_identities{
        inactive_strings};

    graph inactive_graph;

    parser_v2_failure
        inactive_parser_failure;

    header_parser_v2 inactive_parser{
        inactive_input,
        inactive_identities,
        inactive_graph,
        &inactive_parser_failure};

    if (!tests.expect(
            succeeded(
                inactive_parser.parse()) &&
            inactive_input.finished() &&
            succeeded(
                inactive_files.
                    finalize_dependency_topology()),
            "inactive prepared include is skipped semantically")) {

        return;
    }

    tests.expect(
        inactive_files.dependencies(
            inactive_root).empty() &&
        !inactive_strings.find(
            "Unused") &&
        inactive_strings.find(
            "Keep") &&
        inactive_graph.find_type(
            inactive_identities.find(
                inactive_identities.root(),
                inactive_strings.find(
                    "Keep"),
                identity_kind::type)),
        "inactive include publishes no edge and allocates no target strings");

    // Active missing include: physical preparation succeeds, semantic execution
    // is the point where absence becomes a real error.
    const auto nonce =
        std::chrono::steady_clock::now().
            time_since_epoch().
            count();

    const auto missing_name =
        "__server_engine_v4_active_missing_v2_" +
        std::to_string(
            nonce) +
        ".hpp";

    const std::string missing_text =
        "#include \"" +
        missing_name +
        "\"\n"
        "struct NeverReached { int X; };\n";

    const temporary_source missing_root_source{
        "preprocessor_v2_missing_root",
        missing_text};

    file_context missing_files;
    file_id missing_root;

    if (!tests.expect(
            succeeded(
                missing_files.resolve(
                    missing_root_source.path(),
                    file_kind::header,
                    missing_root)) &&
            missing_root,
            "resolve PREPROCESSOR-V2 active missing root")) {

        return;
    }

    preprocessor_configuration
        missing_configuration;

    missing_configuration.root_directory =
        missing_root_source.path().
            parent_path();

    prepared_include_closure_v2
        missing_closure;

    const std::array<file_id, 1>
        missing_roots{
            missing_root};

    if (!tests.expect(
            succeeded(
                prepare_header_include_closure_v2(
                    missing_files,
                    missing_roots,
                    missing_configuration,
                    missing_closure,
                    &preparation_failure)),
            "physical preparation tolerates active-capable missing candidate")) {

        return;
    }

    string_table missing_strings;

    preprocessor_v2_failure
        missing_failure;

    semantic_preprocessor_v2
        missing_input{
            missing_files,
            missing_closure.view(),
            missing_configuration,
            missing_strings,
            &missing_failure};

    tests.expect(
        !succeeded(
            missing_input.start(
                missing_root)) &&
        missing_failure.kind ==
            preprocessor_v2_failure_kind::
                active_include_missing &&
        missing_files.dependencies(
            missing_root).empty(),
        "active missing include fails only during semantic preprocessing");
}


void test_graph_resolved_v2(
    test_state& tests) {

    string_table strings;
    identity_space identities{
        strings};

    string_id type_name;

    if (!tests.expect(
            succeeded(
                strings.intern(
                    "ResolvedIndexedType",
                    type_name)),
            "Graph resolved V2 interns test type name")) {

        return;
    }

    identity_ref type_identity;

    if (!tests.expect(
            succeeded(
                identities.resolve(
                    identities.root(),
                    type_name,
                    identity_kind::type,
                    type_identity)) &&
            type_identity,
            "Graph resolved V2 creates type WHO")) {

        return;
    }

    std::vector<string_id>
        member_names;

    std::vector<member_record>
        members;

    graph G;

    type_handle type;

    if (!tests.expect(
            succeeded(
                G.declare_record(
                    type_identity,
                    graph_record_kind::
                        struct_type,
                    type)) &&
            type,
            "Graph resolved V2 declares test record")) {

        return;
    }

    const auto int_type =
        G.intrinsic(
            intrinsic_type::
                signed_int);

    try {
        member_names.reserve(128);
        members.reserve(128);
    }
    catch (...) {
        (void)tests.expect(
            false,
            "Graph resolved V2 reserves member vectors");
        return;
    }

    for (std::uint32_t index = 0;
         index < 128;
         ++index) {

        const auto spelling =
            std::string{"Member"} +
            std::to_string(index);

        string_id name;

        if (!tests.expect(
                succeeded(
                    strings.intern(
                        spelling,
                        name)) &&
                name,
                "Graph resolved V2 interns member name")) {

            return;
        }

        member_names.push_back(
            name);

        members.push_back({
            name,
            int_type,
            graph_member_access::
                public_access,
            {},
        });
    }

    if (!tests.expect(
            succeeded(
                G.define_resolved_record(
                    type,
                    graph_record_kind::
                        struct_type,
                    members)),
            "Graph resolved V2 commits resolved record")) {

        return;
    }

    const auto first =
        G.find_member(
            type,
            member_names[0]);

    const auto middle =
        G.find_member(
            type,
            member_names[63]);

    const auto last =
        G.find_member(
            type,
            member_names[127]);

    if (!tests.expect(
            first &&
            first.value() == 0 &&
            middle &&
            middle.value() == 63 &&
            last &&
            last.value() == 127,
            "Graph resolved V2 member-name index returns local positions")) {

        return;
    }

    type_ref pointer;

    if (!tests.expect(
            succeeded(
                G.derive_resolved(
                    G.named(type),
                    derived_type_kind::pointer,
                    0,
                    pointer)) &&
            pointer,
            "Graph resolved V2 canonicalizes resolved derived type")) {

        return;
    }

    string_id object_name;
    identity_ref object_identity;
    object_handle object;

    if (!tests.expect(
            succeeded(
                strings.intern(
                    "ResolvedIndexedObject",
                    object_name)) &&
            succeeded(
                identities.resolve(
                    identities.root(),
                    object_name,
                    identity_kind::object,
                    object_identity)) &&
            succeeded(
                G.add_resolved_object(
                    object_identity,
                    G.named(type),
                    object)) &&
            object,
            "Graph resolved V2 commits resolved object")) {

        return;
    }

    graph_delta delta;

    type_handle delta_type;

    if (!tests.expect(
            succeeded(
                delta.declare_record(
                    type_identity,
                    graph_record_kind::
                        struct_type,
                    delta_type)) &&
            delta_type &&
            succeeded(
                delta.define_resolved_record(
                    delta_type,
                    graph_record_kind::
                        struct_type,
                    members)),
            "Graph delta resolved V2 commits record")) {

        return;
    }

    const auto delta_middle =
        delta.find_member(
            delta_type,
            member_names[63]);

    type_ref delta_pointer;
    object_handle delta_object;

    if (!tests.expect(
            delta_middle &&
            delta_middle.value() == 63 &&
            succeeded(
                delta.derive_resolved(
                    delta.named(
                        delta_type),
                    derived_type_kind::pointer,
                    0,
                    delta_pointer)) &&
            delta_pointer &&
            succeeded(
                delta.add_resolved_object(
                    object_identity,
                    delta.named(
                        delta_type),
                    delta_object)) &&
            delta_object,
            "Graph delta resolved V2 uses local index and resolved producers")) {

        return;
    }
}




// HEADER-V2-DIFFERENTIAL-01: test-only independent old/V2 oracle.
// Never compare physical handles, local intern IDs or allocation order.
[[nodiscard]] std::string differential_identity(
    const identity_space& ids, const string_table& strings,
    identity_ref id) {
    if (!id) return "<invalid-identity>";
    if (id == ids.root()) return "::";
    identity_record rec;
    if (!ids.record(id, rec)) return "<unknown-identity>";
    return differential_identity(ids, strings, rec.parent) +
        std::string{strings.get(rec.name)} +
        (id.kind() == identity_kind::namespace_scope ? "::" : "");
}

[[nodiscard]] std::string differential_type(
    const graph& G, const identity_space& ids,
    const string_table& strings, type_ref type, unsigned depth = 0) {
    if (depth > 128) return "<type-depth-limit>";
    intrinsic_type intrinsic;
    if (G.intrinsic(type, intrinsic))
        return "intrinsic(" + std::to_string(static_cast<unsigned>(intrinsic)) + ")";
    if (type.kind() == type_ref_kind::named) {
        // Named type_ref payload is a semantic WHO slot, not a WHERE.
        const auto who = ids.at_slot(type.payload());
        return "named(" + differential_identity(ids, strings, who) + ")";
    }
    derived_type_record derived;
    if (G.derived(type, derived))
        return "derived(" + std::to_string(static_cast<unsigned>(derived.kind)) +
            "," + std::to_string(derived.payload) + "," +
            differential_type(G, ids, strings, derived.child, depth + 1) + ")";
    return "<invalid-type>";
}

[[nodiscard]] std::vector<std::string> differential_projection(
    const graph& G, const identity_space& ids,
    const string_table& strings) {
    std::vector<std::string> result;
    // Semantic identity is the primary key. WHERE allocation of forward
    // declarations differs intentionally between production and V2.
    for (std::uint32_t slot = 2; slot <= ids.size(); ++slot) {
        const auto id = ids.at_slot(slot);
        if (!id || id.kind() != identity_kind::type) continue;
        const auto name = differential_identity(ids, strings, id);
        result.push_back("who:" + name);
        const auto handle = G.find_type(id);
        const auto* entry = G.find(handle);
        if (entry == nullptr || !entry->defined()) continue;
        result.push_back("record:" + name + ":" +
            std::to_string(static_cast<unsigned>(entry->kind)) + ":" +
            std::to_string(static_cast<unsigned>(entry->record_kind)));
        result.push_back("polymorphic:" + name + ":" +
            (entry->polymorphic() ? "1" : "0"));
        for (const auto& base : G.bases(handle)) {
            result.push_back("base:" + name + ":" +
                differential_identity(ids, strings, base.type) + ":" +
                std::to_string(static_cast<unsigned>(base.access)) + ":" +
                std::to_string(static_cast<unsigned>(base.flags)));
        }
        const auto members = G.members(handle);
        for (std::size_t i = 0; i < members.size(); ++i) {
            const auto& member = members[i];
            std::string line = "member:" + name + ":" +
                std::string{strings.get(member.name)} + ":" +
                differential_type(G, ids, strings, member.type) + ":" +
                std::to_string(static_cast<unsigned>(member.access));
            const auto index = G.find_member(handle, member.name);
            const auto* construction = G.construction(handle, index);
            if (construction != nullptr) {
                line += ":init=" +
                    std::to_string(static_cast<unsigned>(construction->kind)) +
                    ":" + std::to_string(construction->bits()) +
                    ":" + std::to_string(construction->operand);
            } else {
                line += ":init=<missing>";
            }
            result.push_back(std::move(line));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

void test_header_v2_differential_01(test_state& tests) {
    struct fixture { const char* id; const char* source; bool success; };
    const fixture fixtures[] = {
        {"plain-record", "struct A { int X; int Y; };", true},
        // HEADER-V2-CONSTRUCTION-05: single token pass, constructor before/after fields.
        {"constructor-before-member", "struct A { A() : X(5) {} int X; };", true},
        {"constructor-after-member", "struct A { int X; A() : X(5) {} };", true},
        {"constructor-override-later-default", "struct A { A() : X(9) {} int X = 3; };", true},
        {"constructor-override-earlier-default", "struct A { int X = 3; A() : X(9) {} };", true},
        {"constructor-body-forward-last-wins", "struct A { A() : X(4) { X = 8; } int X = 1; };", true},
        {"constructor-body-existing-last-wins", "struct A { int X = 1; A() : X(4) { X = 8; } };", true},
        {"constructor-reordered-forward", "struct A { A() : Y(2), X(1) { Y = 3; } int X; int Y; };", true},
        {"constructor-mixed-declaration-order", "struct A { int X; A() : X(1), Y(2) {} int Y; };", true},
        {"namespace", "namespace N { struct A { int X; }; }", true},
        {"class-access", "class C { public: int X; private: int Y; };", true},
        {"array", "struct A { int M[2][3]; int* P; };", true},
        {"forward-definition", "struct A; struct A { int X; };", true},
        // HEADER-V2-PARITY-03: OLD intrinsic spelling combinations.
        {"integers", "struct T { signed S; unsigned U; short A; unsigned short B; long C; unsigned long D; long long E; unsigned long long F; };", true},
        {"signed-order", "struct T { long unsigned A; short signed B; long long unsigned C; };", true},
        {"character-types", "struct T { char A; signed char B; unsigned char C; wchar_t D; char8_t E; char16_t F; char32_t G; };", true},
        {"floating-types", "struct T { float A; double B; long double C; };", true},
        {"bool-and-cv", "struct T { bool A; const unsigned long B; volatile signed char C; };", true},
        {"msvc-int64", "struct T { __int64 A; signed __int64 B; unsigned __int64 C; };", true},
        {"intrinsic-method", "struct T { virtual unsigned long long F(unsigned short); operator unsigned long(); };", true},
        {"signed-unsigned-conflict", "struct T { signed unsigned int A; };", false},
        {"short-long-conflict", "struct T { short long A; };", false},
        {"three-longs", "struct T { long long long A; };", false},
        {"invalid-double-modifier", "struct T { unsigned double A; };", false},
        {"invalid-int64-modifier", "struct T { short __int64 A; };", false},
        {"invalid-char-modifier", "struct T { long char A; };", false},
        // HEADER-V2-PARITY-04: OLD's named operators have no member slots.
        {"operator-assignment", "struct Value { int data; Value& operator=(const Value&) & = default; Value& operator=(int); };", true},
        {"operator-assignment-qualified", "struct Value { Value& Value::operator=(int); };", true},
        {"operator-compound", "struct Value { Value& operator+=(int) & noexcept; Value& operator<<=(int); };", true},
        {"operator-logical-not", "struct Value { bool operator!() const &; bool operator!(void) && = delete; };", true},
        {"operator-subscript", "struct Value { int X; int& operator[](int) const & noexcept; };", true},
        {"operator-comparisons", "struct Value { bool Value::operator==(const Value&) const &; bool operator!=(int); bool operator<(int); bool operator>(int); bool operator<=(int); bool operator>=(int); };", true},
        {"operator-virtual", "struct Base { virtual bool operator==(int) = 0; }; struct Derived : Base { bool operator==(int) override; int X; };", true},
        {"operator-no-params", "struct Value { Value& operator=(); };", false},
        {"operator-multiple-params", "struct Value { bool operator==(int, int); };", false},
        {"operator-no-default-comparison", "struct Value { bool operator==(int) = default; };", false},
        {"operator-invalid-plus", "struct Value { int operator+(int); };", false},
        {"operator-logical-argument", "struct Value { bool operator!(int); };", false},
        {"operator-no-body", "struct Value { bool operator==(int) {} };", false},
        {"virtual-void", "struct A { virtual void Tick(); int X; };", true},
        {"virtual-pure", "struct A { virtual int Value() const noexcept = 0; };", true},
        {"nonvirtual-method", "struct A { void Tick(void); int X; };", true},
        {"parameters", "struct A { int Sum(int a, int b); int X; };", true},
        {"nested-parameter-parens", "struct A { int Apply(int (*callback)(int)); };", true},
        {"virtual-destructor", "struct A { virtual ~A() noexcept; int X; };", true},
        {"inherited-destructor", "struct A { virtual ~A(); }; struct B : A { ~B() override; };", true},
        {"override-method", "struct A { virtual int F(); }; struct B : A { int F() override; };", true},
        {"final-method", "struct A { virtual int F(); }; struct B : A { int F() final; };", true},
        {"override-final", "struct A { virtual int F(); }; struct B : A { int F() override final; };", true},
        {"noexcept-expression", "struct A { virtual int F(int x) noexcept(true); };", true},
        {"conversion-ref", "struct A { operator int() const & noexcept; };", true},
        {"conversion-rvalue-ref", "struct A { virtual operator int() && = 0; };", true},
        {"explicit-conversion", "struct A { explicit operator int() const; };", true},
        {"wrong-destructor", "struct A { ~B(); };", false},
        {"override-no-base", "struct A { int F() override; };", false},
        {"final-no-base", "struct A { int F() final; };", false},
        {"ordinary-ref-qualifier-unsupported", "struct A { int F() &; };", false},
        {"destructor-body", "struct A { ~A() {} };", false},
        {"conversion-parameter", "struct A { operator int(int x); };", false},
        {"conversion-default", "struct A { operator int() = default; };", false},
        {"derived-poly", "struct A { virtual int F(); }; struct B : A { int G(); int X; };", true},
        {"inherited-pure", "struct A { virtual int F(); }; struct B : A { int G() = 0; };", true},
        {"virtual-field", "struct A { virtual int X; };", false},
        {"pure-no-base", "struct A { int F() = 0; };", false},
        {"method-body", "struct A { virtual void F() {} };", false},
        {"duplicate-member", "struct A { int X; int X; };", false},
        {"unknown-type", "struct A { Missing X; };", false},
    };
    for (const auto& f : fixtures) {
        const temporary_source source{"header_v2_diff_01", f.source};
        // OLD context is entirely independent of the V2 context.
        string_table old_strings;
        identity_space old_ids{old_strings};
        graph old_graph;
        file_context old_files;
        lexical_generation old_lexical;
        file_id old_root;
        parser_failure old_failure;
        source_map old_sources;
        preprocessor_configuration old_config;
        old_config.root_directory = source.path().parent_path();
        if (!prepare_root(tests, source.path(), old_files, old_lexical, old_root)) return;
        const auto old_status = parse_semantic_project(
            old_files, old_lexical, 1, old_config, old_strings,
            old_ids, old_graph, old_sources, &old_failure);

        string_table v2_strings;
        identity_space v2_ids{v2_strings};
        graph v2_graph;
        file_context v2_files;
        file_id v2_root;
        parser_v2_failure v2_failure;
        preprocessor_configuration v2_config;
        v2_config.root_directory = source.path().parent_path();
        if (!tests.expect(succeeded(v2_files.resolve(
                source.path(), file_kind::header, v2_root)) && v2_root,
                "DIFF resolve V2 fixture")) return;
        const std::array<file_id, 1> roots{v2_root};
        prepared_include_closure_v2 closure;
        prepared_include_failure_v2 preparation_failure;
        const auto prepared = prepare_header_include_closure_v2(
            v2_files, roots, v2_config, closure, &preparation_failure);
        if (!tests.expect(succeeded(prepared), "DIFF prepare V2 fixture")) return;
        preprocessor_v2_failure preprocessor_failure;
        semantic_preprocessor_v2 input{
            v2_files, closure.view(), v2_config, v2_strings, &preprocessor_failure};
        const auto started = input.start(v2_root);
        if (!tests.expect(succeeded(started), "DIFF start V2 fixture")) return;
        header_parser_v2 parser{input, v2_ids, v2_graph, &v2_failure};
        const auto v2_status = parser.parse();
        const auto context = std::string{"HEADER-V2-DIFFERENTIAL-01/"} + f.id;
        const bool old_ok = succeeded(old_status);
        const bool v2_ok = succeeded(v2_status);
        if (!tests.expect(old_ok == f.success, context + "/old oracle expectation")) continue;
        if (!tests.expect(v2_ok == old_ok, context + "/outcome")) {
            std::cerr << context << " old-kind=" << static_cast<int>(old_failure.kind)
                      << " v2-kind=" << static_cast<int>(v2_failure.kind)
                      << " old-detail=" << old_failure.detail
                      << " v2-detail=" << v2_failure.detail << '\n';
            continue;
        }
        if (!old_ok) {
            // Common syntax/semantic categories are comparable, diagnostic text
            // is not yet part of the production API contract.
            const auto category = [](auto kind) {
                switch (static_cast<unsigned>(kind)) {
                default: return 0u;
                case 3: return 1u; // OLD syntax
                case 4: return 2u; // OLD semantic
                case 5: return 3u; // OLD unsupported
                }
            };
            const unsigned v2_category = static_cast<unsigned>(v2_failure.kind);
            const unsigned old_category = category(old_failure.kind);
            tests.expect(old_category == v2_category, context + "/category");
            tests.expect(old_failure.file == old_root && v2_failure.file == v2_root,
                context + "/source-file");
            tests.expect(old_failure.source.offset == v2_failure.source.offset &&
                old_failure.source.length == v2_failure.source.length,
                context + "/source-span");
            continue;
        }
        const auto old_view = differential_projection(old_graph, old_ids, old_strings);
        const auto v2_view = differential_projection(v2_graph, v2_ids, v2_strings);
        if (old_view != v2_view) {
            std::cerr << context << " Graph semantic mismatch\nOLD:\n";
            for (const auto& row : old_view) std::cerr << row << '\n';
            std::cerr << "V2:\n";
            for (const auto& row : v2_view) std::cerr << row << '\n';
        }
        tests.expect(old_view == v2_view, context + "/semantic-graph");
    }
}

#include "header_v2_differential_02.inc"


// HEADER-MULTI-BASE-01: only existing restricted syntax, now multiple bases.
void test_header_multi_base_01(test_state& tests) {
    const std::string accepted =
        "struct EmptyA {};\n"
        "struct EmptyB {};\n"
        "struct Data { int X; };\n"
        "struct Multi : EmptyA, protected EmptyB, public Data { int Y; };\n"
        "namespace N { struct Other : private EmptyA, Data { int Z; }; }\n";
    const temporary_source source{"header_multi_base_01", accepted};

    // OLD uses the production frontend, with an independent semantic arena.
    file_context old_files;
    lexical_generation old_lexical;
    file_id old_root;
    if (!prepare_root(tests, source.path(), old_files, old_lexical, old_root)) {
        return;
    }
    preprocessor_configuration config;
    config.root_directory = source.path().parent_path();
    string_table old_strings;
    identity_space old_ids{old_strings};
    graph old_graph;
    source_map old_map;
    parser_failure old_error;
    const auto old_status = parse_semantic_project(
        old_files, old_lexical, 1, config,
        old_strings, old_ids, old_graph, old_map, &old_error);

    // V2 uses a new file arena and independently prepared input.
    file_context new_files;
    file_id new_root;
    if (!tests.expect(succeeded(new_files.resolve(
            source.path(), file_kind::header, new_root)) && new_root,
            "MULTI-BASE-01 resolve V2 root")) {
        return;
    }
    prepared_include_closure_v2 closure;
    prepared_include_failure_v2 prepare_error;
    const std::array<file_id, 1> roots{new_root};
    if (!tests.expect(succeeded(prepare_header_include_closure_v2(
            new_files, roots, config, closure, &prepare_error)),
            "MULTI-BASE-01 prepare V2 root")) {
        return;
    }
    string_table new_strings;
    identity_space new_ids{new_strings};
    graph new_graph;
    preprocessor_v2_failure pp_error;
    semantic_preprocessor_v2 input{
        new_files, closure.view(), config, new_strings, &pp_error};
    parser_v2_failure v2_error;
    const auto started = input.start(new_root);
    auto new_status = started;
    if (succeeded(started)) {
        header_parser_v2 parser{input, new_ids, new_graph, &v2_error};
        new_status = parser.parse();
    }
    if (!tests.expect(succeeded(old_status) && succeeded(new_status),
            "MULTI-BASE-01 OLD/V2 accept multiple bases")) {
        std::cerr << "MULTI-BASE-01 old=" << old_error.detail
                  << " new=" << v2_error.detail << '\n';
        return;
    }

    const auto check = [&](const string_table& strings,
                           const identity_space& ids,
                           const graph& G) {
        const auto id = ids.find(ids.root(), strings.find("Multi"), identity_kind::type);
        const auto h = G.find_type(id);
        const auto b = G.bases(h);
        return h && b.size() == 3 &&
            b[0].type == ids.find(ids.root(), strings.find("EmptyA"), identity_kind::type) &&
            b[1].type == ids.find(ids.root(), strings.find("EmptyB"), identity_kind::type) &&
            b[2].type == ids.find(ids.root(), strings.find("Data"), identity_kind::type) &&
            b[0].access == graph_member_access::public_access &&
            b[1].access == graph_member_access::protected_access &&
            b[2].access == graph_member_access::public_access &&
            G.find_member(h, strings.find("Y"));
    };
    tests.expect(check(old_strings, old_ids, old_graph) &&
                 check(new_strings, new_ids, new_graph),
        "MULTI-BASE-01 OLD/V2 canonical WHO, base order and access");

    const auto negative = [&](std::string_view text,
                              std::string_view name) {
        const temporary_source bad{"header_multi_base_negative", text};
        parser_failure old_failure;
        const auto old_result = parse_file(tests, bad.path(), old_failure);
        file_context v2_files;
        file_id root;
        if (!tests.expect(succeeded(v2_files.resolve(
                bad.path(), file_kind::header, root)) && root,
                "MULTI-BASE-01 negative fixture V2 resolve")) {
            return;
        }
        preprocessor_configuration cfg;
        cfg.root_directory = bad.path().parent_path();
        prepared_include_closure_v2 closure;
        prepared_include_failure_v2 preparation_failure;
        const std::array<file_id, 1> roots{root};
        if (!tests.expect(succeeded(prepare_header_include_closure_v2(
                v2_files, roots, cfg, closure, &preparation_failure)),
                "MULTI-BASE-01 negative fixture V2 prepare")) {
            return;
        }
        string_table symbols;
        identity_space ids{symbols};
        graph G;
        preprocessor_v2_failure pp_failure;
        semantic_preprocessor_v2 input{
            v2_files, closure.view(), cfg, symbols, &pp_failure};
        parser_v2_failure v2_failure;
        auto new_result = input.start(root);
        if (succeeded(new_result)) {
            header_parser_v2 parser{input, ids, G, &v2_failure};
            new_result = parser.parse();
        }
        const auto old_kind = old_failure.kind;
        const bool same_kind =
            (old_kind == parser_failure_kind::semantic &&
                v2_failure.kind == parser_v2_failure_kind::semantic) ||
            (old_kind == parser_failure_kind::unsupported &&
                v2_failure.kind == parser_v2_failure_kind::unsupported) ||
            (old_kind == parser_failure_kind::syntax &&
                v2_failure.kind == parser_v2_failure_kind::syntax);
        tests.expect(!succeeded(old_result) && !succeeded(new_result) &&
                     same_kind, name);
    };
    negative("struct A {}; struct X : A, A {};",
        "MULTI-BASE-01 OLD rejects duplicate direct bases");
    negative("struct A; struct X : A, A {};",
        "MULTI-BASE-01 OLD rejects incomplete bases");
    negative("struct A {}; struct X : virtual A {};",
        "MULTI-BASE-01 virtual bases remain unsupported");
}

void test_header_parser_v2_parity_02(
    test_state& tests) {

    const auto parse_v2 =
        [&](const std::string& text,
            string_table& strings,
            identity_space& identities,
            graph& G,
            parser_v2_failure& failure) {

            const temporary_source source{
                "header_v2_parity_02",
                text};

            file_context files;
            file_id root;

            if (!succeeded(
                    files.resolve(
                        source.path(),
                        file_kind::header,
                        root)) ||
                !root) {

                return server_status::io_error;
            }

            preprocessor_configuration
                configuration;

            configuration.root_directory =
                source.path().
                    parent_path();

            prepared_include_closure_v2
                closure;

            prepared_include_failure_v2
                preparation_failure;

            const std::array<file_id, 1>
                roots{root};

            const auto prepared =
                prepare_header_include_closure_v2(
                    files,
                    roots,
                    configuration,
                    closure,
                    &preparation_failure);

            if (!succeeded(prepared)) {
                return prepared;
            }

            preprocessor_v2_failure
                preprocessing_failure;

            semantic_preprocessor_v2 input{
                files,
                closure.view(),
                configuration,
                strings,
                &preprocessing_failure};

            const auto started =
                input.start(
                    root);

            if (!succeeded(started)) {
                return started;
            }

            header_parser_v2 parser{
                input,
                identities,
                G,
                &failure};

            return parser.parse();
        };

    {
        string_table strings;
        identity_space identities{
            strings};
        graph G;
        parser_v2_failure failure;

        const auto status =
            parse_v2(
                "class C {\n"
                "public:\n"
                "    const int Value = 7;\n"
                "private:\n"
                "    volatile int Hidden{};\n"
                "};\n"
                "union U { int A; int B; };\n",
                strings,
                identities,
                G,
                failure);

        const auto c =
            G.find_type(
                identities.find(
                    identities.root(),
                    strings.find("C"),
                    identity_kind::type));

        const auto u =
            G.find_type(
                identities.find(
                    identities.root(),
                    strings.find("U"),
                    identity_kind::type));

        const auto* c_entry =
            G.find(c);

        const auto* u_entry =
            G.find(u);

        const auto value =
            G.find_member(
                c,
                strings.find("Value"));

        const auto hidden =
            G.find_member(
                c,
                strings.find("Hidden"));

        const auto a =
            G.find_member(
                u,
                strings.find("A"));

        const auto* value_member =
            G.member(
                c,
                value);

        const auto* hidden_member =
            G.member(
                c,
                hidden);

        const auto* union_member =
            G.member(
                u,
                a);

        const auto* value_initial =
            G.construction(
                c,
                value);

        const auto* hidden_initial =
            G.construction(
                c,
                hidden);

        tests.expect(
            succeeded(status) &&
            c_entry != nullptr &&
            c_entry->record_kind ==
                graph_record_kind::class_type &&
            u_entry != nullptr &&
            u_entry->record_kind ==
                graph_record_kind::union_type &&
            value_member != nullptr &&
            value_member->access ==
                graph_member_access::public_access &&
            hidden_member != nullptr &&
            hidden_member->access ==
                graph_member_access::private_access &&
            union_member != nullptr &&
            union_member->access ==
                graph_member_access::public_access &&
            value_initial != nullptr &&
            value_initial->kind ==
                construction_kind::unsigned_integer &&
            value_initial->bits() == 7 &&
            hidden_initial != nullptr &&
            hidden_initial->kind ==
                construction_kind::zero,
            "Header V2 class/union access, cv and integer member defaults");
    }

    {
        string_table strings;
        identity_space identities{
            strings};
        graph G;
        parser_v2_failure failure;

        const auto status =
            parse_v2(
                "struct Arrays {\n"
                "    int Matrix[2][3];\n"
                "    int* Pointers[4];\n"
                "};\n",
                strings,
                identities,
                G,
                failure);

        const auto type =
            G.find_type(
                identities.find(
                    identities.root(),
                    strings.find("Arrays"),
                    identity_kind::type));

        const auto matrix =
            G.find_member(
                type,
                strings.find("Matrix"));

        const auto pointers =
            G.find_member(
                type,
                strings.find("Pointers"));

        const auto* matrix_member =
            G.member(
                type,
                matrix);

        const auto* pointers_member =
            G.member(
                type,
                pointers);

        derived_type_record
            matrix_outer;
        derived_type_record
            matrix_inner;
        derived_type_record
            pointer_array;
        derived_type_record
            pointer_element;

        tests.expect(
            succeeded(status) &&
            matrix_member != nullptr &&
            G.derived(
                matrix_member->type,
                matrix_outer) &&
            matrix_outer.kind ==
                derived_type_kind::bounded_array &&
            matrix_outer.payload == 2 &&
            G.derived(
                matrix_outer.child,
                matrix_inner) &&
            matrix_inner.kind ==
                derived_type_kind::bounded_array &&
            matrix_inner.payload == 3 &&
            pointers_member != nullptr &&
            G.derived(
                pointers_member->type,
                pointer_array) &&
            pointer_array.kind ==
                derived_type_kind::bounded_array &&
            pointer_array.payload == 4 &&
            G.derived(
                pointer_array.child,
                pointer_element) &&
            pointer_element.kind ==
                derived_type_kind::pointer,
            "Header V2 prepared numeric sidecar drives bounded array declarators");
    }

    {
        string_table strings;
        identity_space identities{
            strings};
        graph G;
        parser_v2_failure failure;

        const auto status =
            parse_v2(
                "struct Managed {\n"
                "    int X = 1;\n"
                "    int Y{2};\n"
                "    Managed() : X(7) { Y = 9; }\n"
                "};\n",
                strings,
                identities,
                G,
                failure);

        const auto type =
            G.find_type(
                identities.find(
                    identities.root(),
                    strings.find("Managed"),
                    identity_kind::type));

        const auto x =
            G.find_member(
                type,
                strings.find("X"));

        const auto y =
            G.find_member(
                type,
                strings.find("Y"));

        const auto* x_initial =
            G.construction(
                type,
                x);

        const auto* y_initial =
            G.construction(
                type,
                y);

        tests.expect(
            succeeded(status) &&
            x_initial != nullptr &&
            x_initial->kind ==
                construction_kind::unsigned_integer &&
            x_initial->bits() == 7 &&
            y_initial != nullptr &&
            y_initial->kind ==
                construction_kind::unsigned_integer &&
            y_initial->bits() == 9,
            "Header V2 basic managed constructor overrides member defaults");
    }

    {
        string_table strings;
        identity_space identities{
            strings};
        graph G;
        parser_v2_failure failure;

        const auto status =
            parse_v2(
                "class Forward;\n"
                "struct Forward { int X; };\n",
                strings,
                identities,
                G,
                failure);

        const auto type =
            G.find_type(
                identities.find(
                    identities.root(),
                    strings.find("Forward"),
                    identity_kind::type));

        tests.expect(
            succeeded(status) &&
            type,
            "Header V2 class/struct forward declarations share the non-union record family");
    }

    {
        string_table strings;
        identity_space identities{
            strings};
        graph G;
        parser_v2_failure failure;

        const auto status =
            parse_v2(
                "union Conflict;\n"
                "struct Conflict { int X; };\n",
                strings,
                identities,
                G,
                failure);

        const auto identity =
            identities.find(
                identities.root(),
                strings.find("Conflict"),
                identity_kind::type);

        tests.expect(
            status ==
                server_status::
                    project_configuration_invalid &&
            failure.kind ==
                parser_v2_failure_kind::semantic &&
            failure.detail ==
                "Header Parser V2 record key conflicts with prior declaration" &&
            identity &&
            !G.find_type(identity),
            "Header V2 rejects union/non-union key conflict before WHERE materialization");
    }

    {
        string_table strings;
        identity_space identities{
            strings};
        graph G;
        parser_v2_failure failure;

        const auto status =
            parse_v2(
                "struct Invalid { int A, B; };\n",
                strings,
                identities,
                G,
                failure);

        tests.expect(
            status ==
                server_status::
                    project_configuration_invalid &&
            failure.kind ==
                parser_v2_failure_kind::unsupported &&
            failure.detail ==
                "Header Parser V2 multiple data declarators are not part of the current Header subset",
            "Header V2 does not silently expand beyond the old single-declarator member subset");
    }
}

void test_header_parser_v2_parity_01(
    test_state& tests) {

    const auto parse_v2 =
        [&](const std::string& text,
            string_table& strings,
            identity_space& identities,
            graph& G,
            parser_v2_failure& failure) {

            const temporary_source source{
                "header_v2_parity_01",
                text};

            file_context files;
            file_id root;

            if (!succeeded(
                    files.resolve(
                        source.path(),
                        file_kind::header,
                        root)) ||
                !root) {

                return server_status::io_error;
            }

            preprocessor_configuration
                configuration;

            configuration.root_directory =
                source.path().
                    parent_path();

            prepared_include_closure_v2
                closure;

            prepared_include_failure_v2
                preparation_failure;

            const std::array<file_id, 1>
                roots{root};

            const auto prepared =
                prepare_header_include_closure_v2(
                    files,
                    roots,
                    configuration,
                    closure,
                    &preparation_failure);

            if (!succeeded(prepared)) {
                return prepared;
            }

            preprocessor_v2_failure
                preprocessing_failure;

            semantic_preprocessor_v2 input{
                files,
                closure.view(),
                configuration,
                strings,
                &preprocessing_failure};

            const auto started =
                input.start(
                    root);

            if (!succeeded(started)) {
                return started;
            }

            header_parser_v2 parser{
                input,
                identities,
                G,
                &failure};

            return parser.parse();
        };

    // Forward declaration establishes WHO only. A definition appears before B,
    // therefore stable WHERE order must be A then B.
    {
        string_table strings;
        identity_space identities{
            strings};
        graph G;
        parser_v2_failure failure;

        const auto status =
            parse_v2(
                "struct B;\n"
                "struct A { B* Value; };\n"
                "struct B { int X; };\n",
                strings,
                identities,
                G,
                failure);

        const auto a_identity =
            identities.find(
                identities.root(),
                strings.find("A"),
                identity_kind::type);

        const auto b_identity =
            identities.find(
                identities.root(),
                strings.find("B"),
                identity_kind::type);

        const auto a =
            G.find_type(
                a_identity);

        const auto b =
            G.find_type(
                b_identity);

        const auto value =
            G.find_member(
                a,
                strings.find(
                    "Value"));

        const auto* value_record =
            G.member(
                a,
                value);

        derived_type_record
            pointer;

        type_handle referent;

        tests.expect(
            succeeded(status) &&
            a_identity &&
            b_identity &&
            a &&
            b &&
            a.value() <
                b.value() &&
            value_record != nullptr &&
            G.derived(
                value_record->type,
                pointer) &&
            pointer.kind ==
                derived_type_kind::pointer &&
            G.named(
                pointer.child,
                referent) &&
            referent == b,
            "Header V2 forward declaration preserves WHO without early WHERE");
    }

    // WHO exists, but direct storage still requires a defined WHERE.
    {
        string_table strings;
        identity_space identities{
            strings};
        graph G;
        parser_v2_failure failure;

        const auto status =
            parse_v2(
                "struct B;\n"
                "struct A { B Value; };\n",
                strings,
                identities,
                G,
                failure);

        const auto b_identity =
            identities.find(
                identities.root(),
                strings.find("B"),
                identity_kind::type);

        tests.expect(
            status ==
                server_status::
                    project_configuration_invalid &&
            failure.kind ==
                parser_v2_failure_kind::
                    semantic &&
            failure.detail ==
                "Header Parser V2 incomplete type cannot be stored by value" &&
            b_identity &&
            !G.find_type(
                b_identity),
            "Header V2 rejects incomplete by-value use without materializing B WHERE");
    }

    // A use before any declaration has neither semantic WHO nor WHERE.
    {
        string_table strings;
        identity_space identities{
            strings};
        graph G;
        parser_v2_failure failure;

        const auto status =
            parse_v2(
                "struct A { B Value; };\n"
                "struct B { int X; };\n",
                strings,
                identities,
                G,
                failure);

        tests.expect(
            status ==
                server_status::
                    project_configuration_invalid &&
            failure.kind ==
                parser_v2_failure_kind::
                    semantic &&
            failure.detail ==
                "Header Parser V2 named type is unknown",
            "Header V2 rejects named type use before WHO declaration");
    }

    // Exercise both duplicate-detector modes: the 33rd member promotes the
    // record-local scan to an ephemeral hash set, then a late duplicate must
    // still be rejected exactly.
    {
        std::string text{
            "struct Many {\n"};

        for (std::size_t index = 0;
             index < 40;
             ++index) {

            text +=
                "int M" +
                std::to_string(index) +
                ";\n";
        }

        text +=
            "int M17;\n"
            "};\n";

        string_table strings;
        identity_space identities{
            strings};
        graph G;
        parser_v2_failure failure;

        const auto status =
            parse_v2(
                text,
                strings,
                identities,
                G,
                failure);

        tests.expect(
            status ==
                server_status::
                    project_configuration_invalid &&
            failure.kind ==
                parser_v2_failure_kind::
                    semantic &&
            failure.detail ==
                "Header Parser V2 data-member name is duplicated",
            "Header V2 record-local member set rejects duplicate after hash promotion");
    }

    // A large unique record must survive the same promotion with no false hit.
    {
        std::string text{
            "struct Wide {\n"};

        for (std::size_t index = 0;
             index < 80;
             ++index) {

            text +=
                "int M" +
                std::to_string(index) +
                ";\n";
        }

        text +=
            "};\n";

        string_table strings;
        identity_space identities{
            strings};
        graph G;
        parser_v2_failure failure;

        const auto status =
            parse_v2(
                text,
                strings,
                identities,
                G,
                failure);

        const auto wide =
            G.find_type(
                identities.find(
                    identities.root(),
                    strings.find("Wide"),
                    identity_kind::type));

        tests.expect(
            succeeded(status) &&
            wide &&
            G.members(
                wide).size() == 80,
            "Header V2 record-local member set accepts unique wide record");
    }
}

void test_header_parser_v2_minimal(
    test_state& tests) {

    const temporary_source header{
        "header_parser_v2_minimal",
        "namespace A { struct B { int X; B* Next; }; }\n"};

    file_context files;
    file_id root;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    header.path(),
                    file_kind::header,
                    root)) &&
            root,
            "resolve Header Parser V2 minimal root")) {

        return;
    }

    preprocessor_configuration configuration;
    configuration.root_directory =
        header.path().
            parent_path();

    prepared_include_closure_v2 closure;
    prepared_include_failure_v2
        preparation_failure;

    const std::array<file_id, 1>
        roots{root};

    if (!tests.expect(
            succeeded(
                prepare_header_include_closure_v2(
                    files,
                    roots,
                    configuration,
                    closure,
                    &preparation_failure)),
            "prepare Header Parser V2 minimal physical input")) {

        return;
    }

    string_table strings;
    preprocessor_v2_failure
        preprocessing_failure;

    semantic_preprocessor_v2 input{
        files,
        closure.view(),
        configuration,
        strings,
        &preprocessing_failure};

    if (!tests.expect(
            succeeded(
                input.start(
                    root)),
            "start Header Parser V2 semantic preprocessing")) {

        return;
    }

    const auto string_count_after_start =
        strings.size();

    identity_space identities{
        strings};

    graph G;

    parser_v2_failure failure;

    header_parser_v2 parser{
        input,
        identities,
        G,
        &failure};

    if (!tests.expect(
            succeeded(
                parser.parse()) &&
            input.finished() &&
            strings.size() ==
                string_count_after_start,
            "Header Parser V2 consumes prepared preprocessing stream")) {

        return;
    }

    const auto namespace_name =
        strings.find("A");

    const auto record_name =
        strings.find("B");

    const auto first_member_name =
        strings.find("X");

    const auto second_member_name =
        strings.find("Next");

    const auto namespace_identity =
        identities.find(
            identities.root(),
            namespace_name,
            identity_kind::
                namespace_scope);

    const auto type_identity =
        identities.find(
            namespace_identity,
            record_name,
            identity_kind::type);

    const auto type =
        G.find_type(
            type_identity);

    const auto members =
        G.members(
            type);

    derived_type_record pointer;
    type_handle referent;

    tests.expect(
        namespace_name &&
        record_name &&
        first_member_name &&
        second_member_name &&
        namespace_identity &&
        type_identity &&
        type &&
        members.size() == 2 &&
        members[0].name ==
            first_member_name &&
        members[0].type ==
            G.intrinsic(
                intrinsic_type::signed_int) &&
        members[1].name ==
            second_member_name &&
        G.derived(
            members[1].type,
            pointer) &&
        pointer.kind ==
            derived_type_kind::pointer &&
        G.named(
            pointer.child,
            referent) &&
        referent == type,
        "Header Parser V2 preprocessing path preserves expected WHO/WHERE Graph");
}

void test_conversion_operators(test_state& tests) {
    const temporary_source header{"conversion_operators",
        "struct Target {};\n"
        "struct Value { int data; Value() : data(7) {}\n"
        "operator char*(); operator const char*() const;\n"
        "explicit operator bool() const noexcept;\n"
        "operator int&() &; operator double&&() &&;\n"
        "operator int* const*() const; operator Target() const;\n"
        "operator unsigned long(void) const; operator float() = delete; };\n"
        "struct Base { virtual operator bool() const = 0; };\n"
        "struct Derived : Base { operator bool() const override; };\n"};
    file_context files;
    lexical_generation lexical;
    file_id root;
    if (!prepare_root(tests, header.path(), files, lexical, root)) {
        return;
    }
    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;
    const auto status = parse_semantic_project(
        files, lexical, 1, configuration, strings, identities, G, sources, &failure);
    (void)tests.expect(succeeded(status), "parse conversion operator declarations");
    (void)tests.expect(G.type_count() == 4 && G.member_count() == 1,
        "conversion operators do not introduce data members");

    for (const auto text : {
        "struct Bad { operator int(int argument); };",
        "struct Bad { operator Unknown(); };",
        "struct Bad { operator int[2](); };",
        "struct Bad { operator void&(); };",
        "struct Bad { operator int&*(); };",
        "struct Bad { operator int&&&(); };",
        "struct Bad { operator int() = default; };",
        "struct Bad { operator int() { return 1; } };",
        "struct Bad { operator int() override; };"}) {
        const temporary_source invalid{"invalid_conversion", text};
        (void)tests.expect(!succeeded(parse_file(tests, invalid.path(), failure)),
            "reject invalid or unsupported conversion declarations");
    }
}

void test_assignment_operators(test_state& tests) {
    const temporary_source header{"assignment_operators",
        "struct Value { int data; Value() : data(7) {}\n"
        "Value& operator=(const char* value);\n"
        "Value& operator=(const Value&) & = default;\n"
        "Value& operator=(Value&&) noexcept = delete;\n"
        "void operator=(int); };\n"
        "struct Base { virtual Base& operator=(int) = 0; };\n"
        "struct Derived : Base { Base& operator=(int) override; };\n"};
    file_context files;
    lexical_generation lexical;
    file_id root;
    if (!prepare_root(tests, header.path(), files, lexical, root)) {
        return;
    }
    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;
    const auto status = parse_semantic_project(
        files, lexical, 1, configuration, strings, identities, G, sources, &failure);
    (void)tests.expect(succeeded(status), "parse assignment operator declarations");
    (void)tests.expect(G.type_count() == 3 && G.member_count() == 1,
        "assignment declarations do not add data members");
    for (const auto text : {
        "struct Bad { Bad& operator=(); };",
        "struct Bad { Bad& operator=(void); };",
        "struct Bad { Bad& operator=(int, int); };",
        "struct Bad { Bad& operator=(int x = 1); };",
        "struct Bad { Bad& operator=(...); };",
        "struct Bad { Bad& operator=; };",
        "struct Bad { static Bad& operator=(int); };",
        "struct Bad { Bad& operator=(int) { return *this; } };",
        "struct Bad {}; Bad& operator=(int);",
        "struct Bad { Bad& operator+(int); };"}) {
        const temporary_source invalid{"invalid_assignment", text};
        (void)tests.expect(!succeeded(parse_file(tests, invalid.path(), failure)),
            "reject invalid or unsupported assignment declarations");
    }
}

void test_compound_assignment_operators(test_state& tests) {
    for (const auto op : {"+=", "-=", "*=", "/=", "%=", "^=", "&=", "|=", "<<=", ">>="}) {
        const std::string declaration = std::string{"Value& operator"} + op;
        const temporary_source header{"compound_assignment",
            "struct Value { int data; " + declaration + "(const Value&) & noexcept; };\n"
            "struct Base { virtual int operator" + op + "(int) = 0; };\n"
            "struct Derived : Base { int operator" + op + "(int) override; };\n"};
        file_context files;
        lexical_generation lexical;
        file_id root;
        if (!prepare_root(tests, header.path(), files, lexical, root)) {
            return;
        }
        preprocessor_configuration configuration;
        string_table strings;
        identity_space identities{strings};
        graph G;
        source_map sources;
        parser_failure failure;
        const auto status = parse_semantic_project(
            files, lexical, 1, configuration, strings, identities, G, sources, &failure);
        (void)tests.expect(succeeded(status), "parse compound assignment declaration");
        (void)tests.expect(G.type_count() == 3 && G.member_count() == 1,
            "compound operators do not add data members");
        const temporary_source deleted{"deleted_compound",
            "struct Value { " + declaration + "(int) = delete; };"};
        (void)tests.expect(succeeded(parse_file(tests, deleted.path(), failure)),
            "deleted compound declaration accepted");
        for (const auto suffix : {"();", "(void);", "(int, int);", "(int x = 0);",
                                  "(...);", "(int) = default;", "(int) { return *this; }"}) {
            const temporary_source invalid{"invalid_compound",
                "struct Value { " + declaration + suffix + " };"};
            (void)tests.expect(!succeeded(parse_file(tests, invalid.path(), failure)),
                "reject invalid or unsupported compound declarations");
        }
    }
}

void test_logical_not_operator(test_state& tests) {
    const temporary_source header{"logical_not_operator",
        "struct Value { int data; Value& operator!(); };\n"
        "struct ConstValue { bool operator!() const & noexcept; };\n"
        "struct Rvalue { bool operator!(void) && = delete; };\n"
        "struct Base { virtual bool operator!() const = 0; };\n"
        "struct Derived : Base { bool operator!() const override; };\n"};
    file_context files;
    lexical_generation lexical;
    file_id root;
    if (!prepare_root(tests, header.path(), files, lexical, root)) {
        return;
    }
    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;
    const auto status = parse_semantic_project(
        files, lexical, 1, configuration, strings, identities, G, sources, &failure);
    (void)tests.expect(succeeded(status), "parse logical-not declarations including reference return");
    (void)tests.expect(G.type_count() == 5 && G.member_count() == 1,
        "logical-not declarations add no data members");
    const auto base = G.find_type(identities.find(
        identities.root(), strings.find("Base"), identity_kind::type));
    const auto derived = G.find_type(identities.find(
        identities.root(), strings.find("Derived"), identity_kind::type));
    (void)tests.expect(base && derived && G.polymorphic(base) && G.polymorphic(derived),
        "virtual logical-not participates in polymorphic layout");
    for (const auto text : {
        "struct Bad { bool operator!(int); };",
        "struct Bad { bool operator!(void, int); };",
        "struct Bad { bool operator!(...); };",
        "struct Bad { bool operator!; };",
        "struct Bad { bool operator!() = default; };",
        "struct Bad { bool operator!() { return false; } };",
        "struct Bad { static bool operator!(); };",
        "struct Bad { bool operator!() override; };",
        "struct Bad { bool operator!() = 0; };",
        "bool operator!();"}) {
        const temporary_source invalid{"invalid_logical_not", text};
        (void)tests.expect(!succeeded(parse_file(tests, invalid.path(), failure)),
            "reject invalid or unsupported logical-not declarations");
    }
}

void test_comparison_operators(test_state& tests) {
    for (const auto op : {"==", "!=", "<", ">", "<=", ">="}) {
        const std::string declaration = std::string{"bool Value::operator"} + op;
        const temporary_source header{"comparison_operator",
            "struct Value { int data; " + declaration + "(const Value&) const & noexcept; };\n"
            "struct Base { virtual bool operator" + op + "(int) const = 0; };\n"
            "struct Derived : Base { bool Derived::operator" + op + "(int) const override; };\n"};
        file_context files;
        lexical_generation lexical;
        file_id root;
        if (!prepare_root(tests, header.path(), files, lexical, root)) {
            return;
        }
        preprocessor_configuration configuration;
        string_table strings;
        identity_space identities{strings};
        graph G;
        source_map sources;
        parser_failure failure;
        const auto status = parse_semantic_project(
            files, lexical, 1, configuration, strings, identities, G, sources, &failure);
        (void)tests.expect(succeeded(status), "parse qualified and unqualified comparisons");
        (void)tests.expect(G.type_count() == 3 && G.member_count() == 1,
            "comparison declarations do not add data members");
        const auto derived = G.find_type(identities.find(
            identities.root(), strings.find("Derived"), identity_kind::type));
        (void)tests.expect(derived && G.polymorphic(derived), "qualified comparison retains virtual semantics");
        for (const auto suffix : {"();", "(void);", "(int, int);", "(int x = 0);",
                                  "(...);", "(int) = default;", "(int) { return true; }"}) {
            const temporary_source invalid{"invalid_comparison",
                "struct Value { " + declaration + suffix + " };"};
            (void)tests.expect(!succeeded(parse_file(tests, invalid.path(), failure)),
                "reject unsupported comparison declaration");
        }
    }
    parser_failure failure;
    for (const auto text : {
        "struct Value { bool Other::operator==(int); };",
        "struct Value { bool Value::Other::operator==(int); };",
        "struct Value { bool Value::method(int); };",
        "struct Value { static bool operator==(int); };",
        "struct Value {}; bool Value::operator==(int);"}) {
        const temporary_source invalid{"invalid_operator_qualifier", text};
        (void)tests.expect(!succeeded(parse_file(tests, invalid.path(), failure)),
            "reject unrelated qualifier or unsupported operator scope");
    }
    const temporary_source compatible{"qualified_existing_operators",
        "struct Value { int data; Value& Value::operator=(int);\n"
        "int& Value::operator[](int); bool Value::operator!();\n"
        "Value& Value::operator&=(int); bool operator==(int) = delete; };"};
    (void)tests.expect(succeeded(parse_file(tests, compatible.path(), failure)),
        "same-record qualification works for previously supported named operators");
}

void test_subscript_operators(test_state& tests) {
    const temporary_source header{"subscript_operators",
        "struct Value { short data[32];\n"
        "short& operator[](int index);\n"
        "const short& operator[](int) const & noexcept;\n"
        "short operator[](unsigned int) && = delete; };\n"
        "struct Base { virtual short& operator[](int) = 0; };\n"
        "struct Derived : Base { short& operator[](int) override; };\n"};
    file_context files;
    lexical_generation lexical;
    file_id root;
    if (!prepare_root(tests, header.path(), files, lexical, root)) {
        return;
    }
    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;
    const auto status = parse_semantic_project(
        files, lexical, 1, configuration, strings, identities, G, sources, &failure);
    (void)tests.expect(succeeded(status), "parse subscript operator declarations");
    (void)tests.expect(G.type_count() == 3 && G.member_count() == 1,
        "subscript declarations do not add instance data members");
    for (const auto text : {
        "struct Bad { int operator[(); };",
        "struct Bad { int operator[1](int); };",
        "struct Bad { int operator[]; };",
        "struct Bad { int operator[](); };",
        "struct Bad { int operator[](void); };",
        "struct Bad { int operator[](int, int); };",
        "struct Bad { int operator[](int x = 0); };",
        "struct Bad { int operator[](...) ; };",
        "struct Bad { int operator[](int) = default; };",
        "struct Bad { int operator[](int) { return 0; } };",
        "struct Bad { static int operator[](int); };",
        "int operator[](int);"}) {
        const temporary_source invalid{"invalid_subscript", text};
        (void)tests.expect(!succeeded(parse_file(tests, invalid.path(), failure)),
            "reject malformed or unsupported subscript declarations");
    }
}

void test_microsoft_int64(test_state& tests) {
    const temporary_source header{"microsoft_int64",
        "struct Wide { __int64 a; signed __int64 b; unsigned __int64 c;\n"
        "operator __int64() const; operator unsigned __int64() const; };\n"};
    file_context files;
    lexical_generation lexical;
    file_id root;
    if (!prepare_root(tests, header.path(), files, lexical, root)) {
        return;
    }
    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;
    const auto status = parse_semantic_project(
        files, lexical, 1, configuration, strings, identities, G, sources, &failure);
    (void)tests.expect(succeeded(status), "parse Microsoft int64 declarations and conversions");
    (void)tests.expect(G.type_count() == 1 && G.member_count() == 3,
        "Microsoft int64 members retained");
    const auto type = G.find_type(identities.find(identities.root(), strings.find("Wide"), identity_kind::type));
    if (type) {
        for (const auto name : {"a", "b", "c"}) {
            const auto member = G.find_member(type, strings.find(name));
            intrinsic_type intrinsic = intrinsic_type::none;
            const auto* record = G.member(type, member);
            (void)tests.expect(record && G.intrinsic(record->type, intrinsic) &&
                intrinsic == (std::string_view{name} == "c"
                    ? intrinsic_type::unsigned_long_long : intrinsic_type::signed_long_long),
                "Microsoft int64 maps to the correct signedness and width");
        }
    }
    for (const auto text : {
        "struct Bad { short __int64 value; };",
        "struct Bad { long __int64 value; };",
        "struct Bad { unsigned signed __int64 value; };",
        "struct Bad { __int64 int value; };"}) {
        const temporary_source invalid{"invalid_int64", text};
        (void)tests.expect(!succeeded(parse_file(tests, invalid.path(), failure)),
            "reject invalid Microsoft int64 type combinations");
    }
}

void test_global_include_directories(test_state& tests) {
    const temporary_source shared{"global_header", "struct GlobalType {};\n"};
    const auto directory = shared.path().parent_path() / (shared.path().stem().string() + "_nested");
    std::filesystem::create_directory(directory);
    const auto root_path = directory / "root.h";
    {
        std::ofstream stream{root_path};
        stream << "#include <" << shared.path().filename().string() << ">\n";
    }
    file_context files;
    lexical_generation lexical;
    file_id root;
    if (prepare_root(tests, root_path, files, lexical, root)) {
        preprocessor_configuration configuration;
        configuration.root_directory = shared.path().parent_path();
        configuration.include_directories = {"missing-directory", "."};
        string_table strings;
        identity_space identities{strings};
        graph G;
        source_map sources;
        parser_failure failure;
        (void)tests.expect(succeeded(parse_semantic_project(
            files, lexical, 1, configuration, strings, identities, G, sources, &failure)) &&
            G.type_count() == 1,
            "nested header resolves relative global include directories from root Project base");
    }
    std::filesystem::remove(root_path);
    std::filesystem::remove(directory);
}

void test_header_name_diagnostics(
    test_state& tests) {

    const auto check =
        [&](std::string_view source,
            std::uint32_t expected_offset,
            std::uint32_t expected_length,
            std::string_view name) {

            lexical_stream stream;
            lexical_error error;

            const auto status =
                lexer::tokenize(
                    file_id{1},
                    source,
                    stream,
                    &error);

            tests.expect(
                status ==
                    server_status::
                        project_configuration_invalid,
                std::string{name} + " status");

            tests.expect(
                error.reason ==
                    lexical_error_reason::
                        unterminated_header_name,
                std::string{name} + " reason");

            tests.expect(
                error.offset ==
                    expected_offset &&
                error.length ==
                    expected_length,
                std::string{name} + " range");
        };

    check(
        "#include \"bad\n",
        9,
        4,
        "quoted unterminated include");

    check(
        "#include <bad\r\n",
        9,
        4,
        "angled unterminated include");
}

void test_conditional_entry_floor(
    test_state& tests) {

    string_table strings;
    preprocessor state{strings};
    preprocessor_configuration configuration;

    tests.expect(
        succeeded(
            initialize_preprocessor(
                configuration,
                strings,
                state)),
        "initialize directive executor");

    directive_executor executor{
        strings,
        state};

    const file_id file{1};

    tests.expect(
        succeeded(
            executor.enter_file(file)),
        "enter outer directive file");

    preprocessing_directive opening;
    opening.kind =
        directive_kind::ifndef;
    opening.range.file = file;
    opening.range.source = {
        0,
        5,
    };
    opening.identifier.name = {
        0,
        5,
    };

    directive_execution_result result;
    directive_execution_error error;

    tests.expect(
        succeeded(
            executor.execute(
                opening,
                "A_HPP",
                result,
                &error)),
        "open outer conditional");

    tests.expect(
        succeeded(
            executor.enter_file(file)),
        "enter recursive same-file directive entry");

    preprocessing_directive closing;
    closing.kind =
        directive_kind::endif;
    closing.range.file = file;
    closing.range.source = {
        0,
        5,
    };

    tests.expect(
        executor.execute(
            closing,
            "A_HPP",
            result,
            &error) ==
            server_status::
                project_configuration_invalid &&
        error.kind ==
            directive_execution_error_kind::
                unmatched_endif,
        "inner endif cannot close parent entry conditional");
}

[[nodiscard]] std::string nested_namespaces(
    std::size_t depth) {

    std::string source;

    for (std::size_t index = 0;
         index < depth;
         ++index) {

        source +=
            "namespace n" +
            std::to_string(index) +
            " {\n";
    }

    for (std::size_t index = 0;
         index < depth;
         ++index) {

        source += "}\n";
    }

    return source;
}

void test_parallel_include_preparation(test_state& tests) {
    std::vector<std::unique_ptr<temporary_source>> children;
    std::string text;
    for (int i = 0; i < 8; ++i) {
        children.push_back(std::make_unique<temporary_source>(
            "parallel_include_child", std::string{"#pragma once\n"} +
            (i == 0 ? "#define ENABLE_PREPARED\n" : "") + "struct Name" + std::to_string(i) + " {};\n"));
        if (i == 1) text += "#ifdef ENABLE_PREPARED\n";
        text += "#include \"" + children.back()->path().filename().string() + "\"\n";
        if (i == 1) text += "#endif\n";
    }
    const temporary_source invalid{"parallel_inactive_invalid", "/* unterminated"};
    text += "#ifdef NEVER_PREPARED\n#include \"" + invalid.path().filename().string() +
        "\"\n#include \"parallel_include_missing.h\"\n#endif\n";
    const temporary_source root{"parallel_include_root", text};
    file_context files;
    lexical_generation lexical;
    file_id root_id;
    if (!prepare_root(tests, root.path(), files, lexical, root_id)) return;
    preprocessor_configuration configuration;
    string_table strings;
    semantic_input_telemetry preparation;
    semantic_input input{files, lexical, configuration, strings, &preparation};
    for (int replay = 0; replay < 2; ++replay) {
        auto status = input.start(root_id, semantic_input_mode::header);
        std::size_t count = 0;
        while (succeeded(status) && !input.finished()) {
            semantic_token token;
            status = input.next(token);
            if (succeeded(status) && token.kind == token_kind::identifier) {
                tests.expect(strings.get(token.identifier) == "Name" + std::to_string(count),
                    "prepared includes preserve order and included macro effects");
                ++count;
            }
        }
        tests.expect(succeeded(status) && count == 8,
            "parallel preparation ignores inactive lexical errors and missing includes");
        tests.expect(files.size() == 9, "speculative includes do not create file identities");
        tests.expect(preparation.prepared_include_count == 8,
            "parallel snapshots are adopted once and reused on root replay");
    }
    parser_failure failure;
    const temporary_source active_invalid{"parallel_active_invalid",
        text + "#include \"" + invalid.path().filename().string() + "\"\n"};
    tests.expect(!succeeded(parse_file(tests, active_invalid.path(), failure)),
        "active speculative lexical failure remains an error");
    tests.expect(failure.kind == parser_failure_kind::lexical,
        "active speculative invalid bytes retain lexical diagnostics");
    const temporary_source active_missing{"parallel_active_missing",
        text + "#include \"parallel_include_missing.h\"\n"};
    tests.expect(!succeeded(parse_file(tests, active_missing.path(), failure)),
        "active speculative missing include remains an error");
    tests.expect(failure.kind == parser_failure_kind::preprocessing,
        "active speculative missing file retains preprocessing diagnostics");
}

void test_pragma_once_and_angled_includes(test_state& tests) {
    const temporary_source child{"pragma_once_child",
        "#pragma once // physical file guard\nVALUE\n"};
    const auto filename = child.path().filename().string();
    const temporary_source root{"pragma_once_root",
        "#include <" + filename + ">\n#include \"./" + filename + "\"\n"};

    file_context files;
    lexical_generation lexical;
    file_id root_id;
    if (!prepare_root(tests, root.path(), files, lexical, root_id)) {
        return;
    }
    preprocessor_configuration configuration;
    configuration.predefines.push_back({"VALUE", "ConfiguredValue"});
    string_table strings;
    string_id expected;
    tests.expect(succeeded(strings.intern("ConfiguredValue", expected)), "intern configured alias");
    semantic_input input{files, lexical, configuration, strings};
    for (int replay = 0; replay < 2; ++replay) {
        tests.expect(succeeded(input.start(root_id, semantic_input_mode::header)), "start once replay");
        std::size_t identifiers = 0;
        server_status status = server_status::success;
        while (!input.finished() && succeeded(status)) {
            semantic_token token;
            status = input.next(token);
            if (succeeded(status) && token.kind == token_kind::identifier) {
                ++identifiers;
                tests.expect(token.identifier == expected, "predefine applies inside angled include");
            }
        }
        tests.expect(succeeded(status) && identifiers == 1,
            "once deduplicates mixed include spellings and resets for next root replay");
    }

    const temporary_source inactive{"inactive_once",
        "#ifdef NEVER_DEFINED\n#pragma once\n#endif\nVALUE\n"};
    const auto inactive_name = inactive.path().filename().string();
    const temporary_source inactive_root{"inactive_once_root",
        "#include <" + inactive_name + ">\n#include <" + inactive_name + ">\n"};
    file_context inactive_files;
    lexical_generation inactive_lexical;
    file_id inactive_id;
    if (prepare_root(tests, inactive_root.path(), inactive_files, inactive_lexical, inactive_id)) {
        semantic_input inactive_input{inactive_files, inactive_lexical, configuration, strings};
        tests.expect(succeeded(inactive_input.start(inactive_id, semantic_input_mode::header)),
            "start inactive once replay");
        std::size_t identifiers = 0;
        server_status status = server_status::success;
        while (!inactive_input.finished() && succeeded(status)) {
            semantic_token token;
            status = inactive_input.next(token);
            identifiers += succeeded(status) && token.kind == token_kind::identifier ? 1 : 0;
        }
        tests.expect(succeeded(status) && identifiers == 2, "inactive pragma once does not suppress inclusion");
    }

    const temporary_source self{"self_once", ""};
    {
        std::ofstream stream{self.path(), std::ios::binary | std::ios::trunc};
        stream << "#pragma once\n#include <" << self.path().filename().string()
               << ">\nstruct SelfOnce {};\n";
    }
    parser_failure failure;
    tests.expect(succeeded(parse_file(tests, self.path(), failure)), "pragma once stops self inclusion");
    for (const auto text : {"#pragma once extra\n", "#pragma unknown\n", "#include <>\n"}) {
        const temporary_source invalid{"invalid_once_include", text};
        tests.expect(!succeeded(parse_file(tests, invalid.path(), failure)),
            "unsupported pragma and empty include fail closed");
    }
}

void test_self_include_guard(
    test_state& tests) {

    const auto nonce =
        std::chrono::steady_clock::now().
            time_since_epoch().count();

    const auto path =
        std::filesystem::temp_directory_path() /
        ("server_engine_v4_self_include_" +
         std::to_string(nonce) +
         ".hpp");

    const auto filename =
        path.filename().string();

    const std::string text =
        "#ifndef A_HPP\n"
        "#define A_HPP\n"
        "#include \"" +
        filename +
        "\"\n"
        "#endif\n";

    {
        std::ofstream stream{
            path,
            std::ios::binary |
                std::ios::trunc};

        if (!stream) {
            throw std::runtime_error{
                "Cannot create self-include regression source"};
        }

        stream.write(
            text.data(),
            static_cast<std::streamsize>(
                text.size()));

        if (!stream) {
            throw std::runtime_error{
                "Cannot write self-include regression source"};
        }
    }

    parser_failure failure;

    tests.expect(
        succeeded(
            parse_file(
                tests,
                path,
                failure)),
        "guarded self-include completes without unterminated conditional");

    std::error_code error;
    std::filesystem::remove(
        path,
        error);
}

void test_parser_scope_limit(
    test_state& tests) {

    {
        const auto text =
            nested_namespaces(
                parser_scope_depth_limit);

        const temporary_source source{
            "namespace_limit_ok",
            text};

        parser_failure failure;

        tests.expect(
            succeeded(
                parse_file(
                    tests,
                    source.path(),
                    failure)),
            "parser accepts configured maximum namespace depth");
    }

    {
        const auto text =
            nested_namespaces(
                parser_scope_depth_limit + 1);

        const temporary_source source{
            "namespace_limit_fail",
            text};

        parser_failure failure;

        const auto status =
            parse_file(
                tests,
                source.path(),
                failure);

        tests.expect(
            status ==
                server_status::
                    project_configuration_invalid,
            "parser rejects namespace depth above limit");

        tests.expect(
            failure.kind ==
                parser_failure_kind::unsupported &&
            failure.detail ==
                "Namespace nesting exceeds the supported parser scope depth",
            "parser reports namespace depth reason");
    }
}

void test_header_source_semantic_split(
    test_state& tests) {

    const temporary_source source{
        "source_before_header",
        "A instance;\n"
        "int value = 7;\n"};

    const temporary_source header{
        "header_after_source",
        "struct A { int field; };\n"};

    file_context files;
    lexical_generation lexical;

    file_id source_id;
    file_id header_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    source.path(),
                    file_kind::source,
                    source_id)) &&
            succeeded(
                files.resolve(
                    header.path(),
                    file_kind::header,
                    header_id)) &&
            source_id.value() == 1 &&
            header_id.value() == 2,
            "Source may precede Header in Project order")) {

        return;
    }

    const std::array<file_id, 2> roots{
        source_id,
        header_id};

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;

    if (!tests.expect(
            succeeded(
                parse_semantic_project(
                    files,
                    lexical,
                    roots.size(),
                    configuration,
                    strings,
                    identities,
                    G,
                    sources,
                    &failure)),
            "Header semantic pass precedes Source regardless of file_id order")) {

        return;
    }

    const auto type_name =
        strings.find("A");

    const auto instance_name =
        strings.find("instance");

    const auto value_name =
        strings.find("value");

    const auto type_identity =
        identities.find(
            identities.root(),
            type_name,
            identity_kind::type);

    const auto instance_identity =
        identities.find(
            identities.root(),
            instance_name,
            identity_kind::object);

    const auto value_identity =
        identities.find(
            identities.root(),
            value_name,
            identity_kind::object);

    const auto instance =
        G.find_object(
            instance_identity);

    const auto value =
        G.find_object(
            value_identity);

    construction_value value_initial;

    const auto type_handle_value =
        G.find_type(
            type_identity);

    const auto source_dependencies =
        sources.root_dependencies(
            source_id);

    tests.expect(
        type_identity &&
        type_handle_value &&
        instance &&
        value &&
        G.construction(
            value,
            value_initial) &&
        value_initial.kind ==
            construction_kind::unsigned_integer &&
        value_initial.bits() == 7,
        "Source consumes completed Header types and retains object initialization");

    tests.expect(
        source_dependencies.size() == 1 &&
        source_dependencies[0] ==
            source_dependency_ref::type(
                type_identity),
        "Source semantic root records dependency on global Header type");
}

void test_sparse_semantic_replay_order(
    test_state& tests) {

    const temporary_source source{
        "sparse_replay_source_first",
        "A instance;\n"
        "instance.field = 7;\n"};

    const temporary_source header{
        "sparse_replay_header_second",
        "struct A { int field; };\n"};

    file_context files;
    lexical_generation lexical;

    file_id source_id;
    file_id header_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    source.path(),
                    file_kind::source,
                    source_id)) &&
            succeeded(
                files.resolve(
                    header.path(),
                    file_kind::header,
                    header_id)) &&
            source_id.value() <
                header_id.value(),
            "prepare sparse replay roots with Source file_id before Header")) {

        return;
    }

    const std::array<file_id, 2>
        roots{
            source_id,
            header_id,
        };

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{
        strings};
    graph_delta G;
    source_map_delta sources;
    parser_failure failure;
    std::vector<parser_warning> warnings;

    if (!tests.expect(
            succeeded(
                parse_semantic_roots(
                    files,
                    lexical,
                    roots,
                    configuration,
                    strings,
                    identities,
                    G,
                    sources,
                    &failure,
                    &warnings)),
            "sparse replay executes Header domain before Source domain")) {

        return;
    }

    const auto type =
        G.find_type(
            identities.find(
                identities.root(),
                strings.find("A"),
                identity_kind::type));

    const auto object =
        G.find_object(
            identities.find(
                identities.root(),
                strings.find("instance"),
                identity_kind::object));

    const auto member =
        G.find_member(
            type,
            strings.find("field"));

    object_initialization_record
        initialization;

    const auto replay_roots =
        sources.root_entries();

    tests.expect(
        type &&
        object &&
        member &&
        G.initialization(
            {
                G.identity(object),
                endpoint_ref{member},
            },
            initialization) &&
        initialization.value.kind ==
            construction_kind::
                unsigned_integer &&
        initialization.value.bits() == 7,
        "sparse replay produces Graph delta with Source initialization");

    tests.expect(
        replay_roots.size() == 2 &&
        replay_roots[0].root ==
            header_id &&
        replay_roots[1].root ==
            source_id &&
        replay_roots[0].
            initializations.count == 0 &&
        replay_roots[1].
            initializations.count == 1 &&
        sources.
            initialization_target_entries().
                size() == 1 &&
        warnings.empty(),
        "BUILD provenance retains canonical Header then Source replay order");
}


void test_flat_source_cursor(test_state& tests) {
    const std::string text = "alpha;" + std::string(70000, ' ') +
        "alpha = \"" + std::string(400, 'x') + "\";";
    const temporary_source source{"flat_source_cursor", text};
    file_context files;
    lexical_generation lexical;
    file_id root;
    if (!prepare_root(tests, source.path(), files, lexical, root, file_kind::source)) return;
    string_table strings;
    string_id alpha;
    tests.expect(succeeded(strings.intern("alpha", alpha)), "intern Source name");
    preprocessor_configuration configuration;
    configuration.predefines.push_back({"alpha", "replacement"});
    semantic_input_telemetry telemetry;
    semantic_input source_input{files, lexical, configuration, strings, &telemetry};
    for (int replay = 0; replay != 2; ++replay) {
        frontend_input reference{lexical};
        tests.expect(succeeded(reference.start(root)), "start stack cursor reference");
        tests.expect(succeeded(source_input.start(root, semantic_input_mode::source)), "start flat Source cursor");
        while (!reference.finished()) {
            frontend_token expected;
            semantic_token actual;
            if (!tests.expect(succeeded(reference.next(expected)) && succeeded(source_input.next(actual)),
                "decode Source with extended delta and length")) return;
            tests.expect(actual.file == expected.file && actual.kind == expected.kind &&
                actual.source_offset == expected.source_offset && actual.source_length == expected.source_length,
                "flat Source matches stack cursor positions");
            if (actual.kind == token_kind::identifier)
                tests.expect(actual.identifier == alpha, "Source ignores Header predefines and preserves identifier");
            else tests.expect(!actual.identifier, "nonidentifier has no stale name");
        }
        semantic_token eof;
        tests.expect(succeeded(source_input.next(eof)) && source_input.finished() &&
            eof.kind == token_kind::invalid && !eof.file, "flat Source EOF and replay reset");
        tests.expect(!succeeded(source_input.next(eof)), "Source rejects read after EOF");
    }
    tests.expect(telemetry.source_identifier_count == 4 && telemetry.source_token_count == 12,
        "Source cursor counts successful tokens across replays");

    // Encoded descriptors use the same decoder without borrowing a native arena.
    const std::array<std::uint32_t, 3> words{
        lexical_token::make(token_kind::string_literal, lexical_token::extended_delta,
            lexical_token::extended_length).value(), 70000, 402};
    const auto encoded = lexical_word_view::from_encoded(std::as_bytes(std::span{words}));
    std::uint32_t position = 0, offset = 0;
    semantic_token token;
    tests.expect(succeeded(decode_frontend_token(encoded, root, position, offset, token)) &&
        position == 3 && token.source_offset == 70000 && token.source_length == 402,
        "Source decoder handles encoded extended fields");
    position = 0; offset = 0;
    tests.expect(!succeeded(decode_frontend_token(std::span{words}.first(2), root, position, offset, token)) &&
        position == 0 && offset == 0, "truncated extension leaves cursor position unchanged");
}

void test_source_preprocessor_rejected(
    test_state& tests) {

    const temporary_source source{
        "source_preprocessor_rejected",
        "#define X int\n"
        "X value;\n"};

    file_context files;
    lexical_generation lexical;
    file_id root;

    if (!prepare_root(
            tests,
            source.path(),
            files,
            lexical,
            root,
            file_kind::source)) {

        return;
    }

    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;

    const auto status =
        parse_semantic_project(
            files,
            lexical,
            1,
            configuration,
            strings,
            identities,
            G,
            sources,
            &failure);

    tests.expect(
        status ==
            server_status::project_configuration_invalid &&
        failure.kind ==
            parser_failure_kind::preprocessing &&
        failure.detail ==
            "C++ preprocessing directives are not supported in Source inputs",
        "Source rejects C++ preprocessing");
}

void test_nested_constructor_defaults(test_state& tests) {
    for (const bool invalid : {false, true}) {
        const temporary_source source{"nested_constructor_defaults",
            invalid ? "struct Leaf { int value; }; struct A { Leaf p; A() { p.missing = 2; } };"
            : "struct Leaf { int value; }; struct Mid { Leaf leaf; }; "
              "struct A { Mid p; A() { p.leaf.value = 1; p.leaf.value = 42; } }; "
              "struct B { Mid p; B() { p.leaf.value = 9; } };"};
        file_context files;
        lexical_generation lexical;
        file_id root;
        if (!prepare_root(tests, source.path(), files, lexical, root)) return;
        preprocessor_configuration configuration;
        string_table strings;
        identity_space identities{strings};
        graph G;
        source_map sources;
        parser_failure failure;
        const auto status = parse_semantic_project(files, lexical, 1, configuration,
            strings, identities, G, sources, &failure);
        tests.expect(succeeded(status) != invalid, "nested constructor path validation");
        if (!invalid && succeeded(status)) {
            const auto entries = G.constructor_defaults.entries();
            tests.expect(entries.size() == 2 && entries[0].value.bits() == 42 &&
                entries[1].value.bits() == 9 && entries[0].owner != entries[1].owner &&
                entries[0].path == strings.find("p.leaf.value"),
                "nested defaults are type-local and the last assignment wins");
        }
    }
}

void test_constructor_aggregate_defaults(test_state& tests) {
    for (const auto init : {"{.name = \"hello\", .value = 42, .scale = 1.5}",
        "{\"hello\", 42, 1.5}", "{.value = 42, .name = \"hello\"}", "{\"hello\", .value = 42}"}) {
        const bool valid = std::string_view{init} == "{.name = \"hello\", .value = 42, .scale = 1.5}" ||
            std::string_view{init} == "{\"hello\", 42, 1.5}";
        const temporary_source source{"aggregate_constructor", std::string{
            "struct A { char name[8]; int value; double scale; }; struct B { A a; B() : a"} + init + " {} };"};
        file_context files;
        lexical_generation lexical;
        file_id root;
        if (!prepare_root(tests, source.path(), files, lexical, root)) return;
        preprocessor_configuration configuration;
        string_table strings;
        identity_space identities{strings};
        graph G;
        source_map sources;
        parser_failure failure;
        const auto status = parse_semantic_project(files, lexical, 1, configuration,
            strings, identities, G, sources, &failure);
        tests.expect(succeeded(status) == valid, "C++ aggregate constructor designated and positional forms");
        if (valid && succeeded(status)) {
            const auto entries = G.constructor_defaults.entries();
            tests.expect(entries.size() == 10, "aggregate constructor stores every char and scalar value");
            for (const auto& entry : entries) {
                const auto path = strings.get(entry.path);
                if (path == "a.value") tests.expect(entry.value.bits() == 42, "aggregate integer value");
                if (path == "a.scale") tests.expect(std::bit_cast<double>(entry.value.bits()) == 1.5, "aggregate real value");
                if (path == "a.name[0]") tests.expect(entry.value.bits() == 'h', "aggregate string value");
                if (path == "a.name[5]") tests.expect(entry.value.bits() == 0, "aggregate string terminator");
            }
        }
    }
}

void test_constructor_array_defaults(test_state& tests) {
    for (const auto target : {"a[0][1]", "items[1].value", "a[2][0]", "a[0][3]",
        "a[-1][0]", "a[18446744073709551616][0]", "a[1+0][0]", "items[0].value[0]"}) {
        const auto valid = std::string_view{target} == "a[0][1]" || std::string_view{target} == "items[1].value";
        const std::string text = std::string{"struct Leaf { int value; }; struct A { int a[2][3]; Leaf items[2]; A() { "} +
            target + " = 1; " + target + " = 42; } };";
        const temporary_source source{"constructor_array_defaults", text};
        file_context files;
        lexical_generation lexical;
        file_id root;
        if (!prepare_root(tests, source.path(), files, lexical, root)) return;
        preprocessor_configuration configuration;
        string_table strings;
        identity_space identities{strings};
        graph G;
        source_map sources;
        parser_failure failure;
        const auto status = parse_semantic_project(files, lexical, 1, configuration,
            strings, identities, G, sources, &failure);
        tests.expect(succeeded(status) == valid, "constructor array indices validate shape and bounds");
        if (valid && succeeded(status)) {
            const auto entries = G.constructor_defaults.entries();
            tests.expect(entries.size() == 1 && entries[0].path == strings.find(target) &&
                entries[0].value.bits() == 42, "constructor array assignments retain the last value");
        }
    }
}

void test_class_abi_semantics(
    test_state& tests) {

    const temporary_source source{
        "class_abi_semantics",
        "struct A {\n"
        "    virtual void f();\n"
        "    int a;\n"
        "};\n"
        "struct B : public A {\n"
        "    void f() override;\n"
        "    int b;\n"
        "};\n"
        "struct C : A {\n"
        "    virtual ~C();\n"
        "    int c;\n"
        "};\n"
        "struct Plain {\n"
        "    int value;\n"
        "};\n"};

    file_context files;
    lexical_generation lexical;
    file_id root;

    if (!prepare_root(
            tests,
            source.path(),
            files,
            lexical,
            root)) {

        return;
    }

    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;

    if (!tests.expect(
            succeeded(
                parse_semantic_project(
                    files,
                    lexical,
                    1,
                    configuration,
                    strings,
                    identities,
                    G,
                    sources,
                    &failure)),
            "parse class ABI semantic declarations")) {

        return;
    }

    const auto type =
        [&](std::string_view name) {
            return G.find_type(
                identities.find(
                    identities.root(),
                    strings.find(name),
                    identity_kind::type));
        };

    const auto a = type("A");
    const auto b = type("B");
    const auto c = type("C");
    const auto plain = type("Plain");

    const auto* a_entry = G.find(a);
    const auto* b_entry = G.find(b);
    const auto* c_entry = G.find(c);
    const auto* plain_entry = G.find(plain);

    const auto b_bases =
        G.bases(b);

    const auto c_bases =
        G.bases(c);

    tests.expect(
        a_entry != nullptr &&
        b_entry != nullptr &&
        c_entry != nullptr &&
        plain_entry != nullptr &&
        a_entry->polymorphic() &&
        b_entry->polymorphic() &&
        c_entry->polymorphic() &&
        !plain_entry->polymorphic() &&
        G.polymorphic(a) &&
        G.polymorphic(b) &&
        G.polymorphic(c) &&
        !G.polymorphic(plain),
        "virtual declarations and inheritance normalize to type polymorphic flag");

    tests.expect(
        b_bases.size() == 1 &&
        b_bases[0].type == G.identity(a) &&
        b_bases[0].access ==
            graph_member_access::
                public_access &&
        !b_bases[0].virtual_base() &&
        c_bases.size() == 1 &&
        c_bases[0].type == G.identity(a),
        "single non-virtual base relation retained directly by type_entry");

    const temporary_source virtual_base{
        "class_abi_virtual_base",
        "struct A {};\n"
        "struct B : virtual A {};\n"};

    parser_failure virtual_failure;

    tests.expect(
        parse_file(
            tests,
            virtual_base.path(),
            virtual_failure) ==
                server_status::
                    project_configuration_invalid &&
        virtual_failure.kind ==
            parser_failure_kind::unsupported &&
        virtual_failure.detail ==
            "Virtual base classes are not implemented in CXX-CLASS-ABI-V1",
        "virtual inheritance fails closed before Runtime ABI layout");

    const temporary_source multiple_base{
        "class_abi_multiple_base",
        "struct A {};\n"
        "struct B {};\n"
        "struct C : A, B {};\n"};

    parser_failure multiple_failure;

    tests.expect(
        succeeded(parse_file(
            tests,
            multiple_base.path(),
            multiple_failure)),
        "multiple nonvirtual inheritance is accepted by the Header semantic slice");
}

void test_record_scratch_isolation(test_state& tests) {
    const temporary_source source{
        "record_scratch_isolation",
        "struct A { int x = 7; int y = 9; A() : x(11) {} };\n"
        "struct B { int x; };\n"
        "struct Empty {};\n"
        "struct Forward;\n"
        "struct C { int x = 3; int y; int z = 5; };\n"
        "struct D { int x; D() : x(13) {} };\n"};

    file_context files;
    lexical_generation lexical;
    file_id root;
    if (!prepare_root(tests, source.path(), files, lexical, root)) {
        return;
    }
    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;
    if (!tests.expect(succeeded(parse_semantic_project(
            files, lexical, 1, configuration, strings, identities, G, sources,
            &failure)), "parse records with growing/shrinking scratch storage")) {
        return;
    }
    const auto check = [&](std::string_view name, std::size_t count,
                           std::string_view member, std::uint64_t value) {
        const auto type = G.find_type(identities.find(
            identities.root(), strings.find(name), identity_kind::type));
        const auto* initial = G.construction(type, G.find_member(type, strings.find(member)));
        (void)tests.expect(type && G.members(type).size() == count && initial != nullptr &&
                     initial->kind == (value == 0 ? construction_kind::zero :
                                                  construction_kind::unsigned_integer) &&
                     initial->bits() == value,
                     "record members and initialization do not leak between definitions");
    };
    check("A", 2, "x", 11);
    check("A", 2, "y", 9);
    check("B", 1, "x", 0);
    check("C", 3, "x", 3);
    check("C", 3, "y", 0);
    check("C", 3, "z", 5);
    check("D", 1, "x", 13);

    const auto forward_identity =
        identities.find(
            identities.root(),
            strings.find("Forward"),
            identity_kind::type);

    tests.expect(
        forward_identity &&
        !G.find_type(forward_identity),
        "declaration-only Header WHO has no Graph WHERE");
}

void test_header_streaming_order(
    test_state& tests) {

    // Valid: B is defined before A uses B by value.
    {
        const temporary_source first{
            "header_stream_b",
            "struct B { int X; };\n"};

        const temporary_source second{
            "header_stream_a",
            "struct A { B Value; };\n"};

        file_context files;
        lexical_generation lexical;
        file_id first_id;
        file_id second_id;

        if (!tests.expect(
                succeeded(
                    files.resolve(
                        first.path(),
                        file_kind::header,
                        first_id)) &&
                succeeded(
                    files.resolve(
                        second.path(),
                        file_kind::header,
                        second_id)),
                "resolve ordered Header streaming roots")) {

            return;
        }

        const std::array<file_id, 2> roots{
            first_id,
            second_id};

        if (!prepare_all_roots(
                tests,
                files,
                lexical,
                roots)) {

            return;
        }

        preprocessor_configuration configuration;
        string_table strings;
        identity_space identities{strings};
        graph G;
        source_map sources;
        parser_failure failure;

        if (!tests.expect(
                succeeded(
                    parse_semantic_project(
                        files,
                        lexical,
                        roots.size(),
                        configuration,
                        strings,
                        identities,
                        G,
                        sources,
                        &failure)),
                "Header streaming follows declaration order")) {

            return;
        }

        const auto a_identity =
            identities.find(
                identities.root(),
                strings.find("A"),
                identity_kind::type);

        const auto b_identity =
            identities.find(
                identities.root(),
                strings.find("B"),
                identity_kind::type);

        const auto a_type =
            G.find_type(
                a_identity);

        const auto b_type =
            G.find_type(
                b_identity);

        const auto value =
            G.find_member(
                a_type,
                strings.find("Value"));

        const auto* member =
            G.member(
                a_type,
                value);

        type_handle resolved;

        tests.expect(
            a_identity &&
            b_identity &&
            a_type &&
            b_type &&
            member != nullptr &&
            G.named(
                member->type,
                resolved) &&
            resolved ==
                b_type,
            "Header streaming preserves named member type");
    }

    // Invalid: a later declaration must not make an earlier by-value use valid.
    {
        const temporary_source source{
            "header_stream_reverse",
            "struct A { B Value; };\n"
            "struct B { int X; };\n"};

        parser_failure failure;

        const auto status =
            parse_file(
                tests,
                source.path(),
                failure);

        tests.expect(
            status ==
                server_status::
                    project_configuration_invalid,
            "Header streaming rejects by-value use before declaration");

        tests.expect(
            failure.kind ==
                parser_failure_kind::semantic &&
            failure.detail ==
                "Named type is not declared in the visible semantic scope",
            "Header streaming reports undeclared earlier type");
    }

    // Valid: a prior declaration establishes WHO for pointer use without
    // allocating WHERE. A is defined before B, so WHERE(A) must precede
    // WHERE(B) after B is finally defined.
    {
        const temporary_source source{
            "header_stream_forward_pointer",
            "struct B;\n"
            "struct A { B* Value; };\n"
            "struct B { int X; };\n"};

        file_context files;
        lexical_generation lexical;
        file_id root;

        if (!prepare_root(
                tests,
                source.path(),
                files,
                lexical,
                root)) {

            return;
        }

        preprocessor_configuration configuration;
        string_table strings;
        identity_space identities{strings};
        graph G;
        source_map sources;
        parser_failure failure;

        if (!tests.expect(
                succeeded(
                    parse_semantic_project(
                        files,
                        lexical,
                        1,
                        configuration,
                        strings,
                        identities,
                        G,
                        sources,
                        &failure)),
                "Header streaming accepts prior WHO for pointer type")) {

            return;
        }

        const auto a_identity =
            identities.find(
                identities.root(),
                strings.find("A"),
                identity_kind::type);

        const auto b_identity =
            identities.find(
                identities.root(),
                strings.find("B"),
                identity_kind::type);

        const auto a_type =
            G.find_type(
                a_identity);

        const auto b_type =
            G.find_type(
                b_identity);

        const auto value =
            G.find_member(
                a_type,
                strings.find("Value"));

        const auto* member =
            G.member(
                a_type,
                value);

        derived_type_record pointer;
        type_handle resolved;

        tests.expect(
            a_identity &&
            b_identity &&
            a_type &&
            b_type &&
            a_type.value() <
                b_type.value(),
            "Forward declaration does not allocate Graph WHERE");

        tests.expect(
            member != nullptr &&
            G.derived(
                member->type,
                pointer) &&
            pointer.kind ==
                derived_type_kind::pointer &&
            G.named(
                pointer.child,
                resolved) &&
            resolved ==
                b_type,
            "Forward pointer retains named WHO through later materialization");
    }

    // A forward WHO is not a complete by-value object type.
    {
        const temporary_source source{
            "header_stream_forward_by_value",
            "struct B;\n"
            "struct A { B Value; };\n"
            "struct B { int X; };\n"};

        parser_failure failure;

        tests.expect(
            parse_file(
                tests,
                source.path(),
                failure) ==
                server_status::
                    project_configuration_invalid,
            "Forward declaration is incomplete for by-value member storage");
    }
}


void test_header_static_constructor_binding(
    test_state& tests) {

    const temporary_source first{
        "header_static_a",
        "static int a = 5;\n"
        "struct A {\n"
        "    int& b;\n"
        "    A() : b(a) {}\n"
        "};\n"};

    const temporary_source second{
        "header_static_b",
        "static int a = 7;\n"
        "struct B {\n"
        "    int& b;\n"
        "    B() : b(a) {}\n"
        "};\n"};

    file_context files;
    lexical_generation lexical;

    file_id first_id;
    file_id second_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    first.path(),
                    file_kind::header,
                    first_id)) &&
            succeeded(
                files.resolve(
                    second.path(),
                    file_kind::header,
                    second_id)),
            "resolve Header static roots")) {

        return;
    }

    const std::array<file_id, 2> roots{
        first_id,
        second_id};

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;

    if (!tests.expect(
            succeeded(
                parse_semantic_project(
                    files,
                    lexical,
                    roots.size(),
                    configuration,
                    strings,
                    identities,
                    G,
                    sources,
                    &failure)),
            "parse Header static constructor bindings")) {

        return;
    }

    tests.expect(
        G.object_count() == 2,
        "internal static object is distinct per semantic root");

    const auto first_static =
        G.object_at(0);

    const auto second_static =
        G.object_at(1);

    const auto* first_object =
        G.find(first_static);

    const auto* second_object =
        G.find(second_static);

    construction_value first_initial;
    construction_value second_initial;

    tests.expect(
        first_object != nullptr &&
        second_object != nullptr &&
        first_object->internal_static() &&
        second_object->internal_static() &&
        G.construction(
            first_static,
            first_initial) &&
        G.construction(
            second_static,
            second_initial) &&
        first_initial.kind ==
            construction_kind::unsigned_integer &&
        second_initial.kind ==
            construction_kind::unsigned_integer &&
        first_initial.bits() == 5 &&
        second_initial.bits() == 7,
        "Header static storage and initialization retained in G");

    const auto a_name =
        strings.find("A");

    const auto b_name =
        strings.find("B");

    const auto member_name =
        strings.find("b");

    const auto a_type =
        G.find_type(
            identities.find(
                identities.root(),
                a_name,
                identity_kind::type));

    const auto b_type =
        G.find_type(
            identities.find(
                identities.root(),
                b_name,
                identity_kind::type));

    const auto a_member =
        G.find_member(
            a_type,
            member_name);

    const auto b_member =
        G.find_member(
            b_type,
            member_name);

    const auto* a_initial =
        G.construction(
            a_type,
            a_member);

    const auto* b_initial =
        G.construction(
            b_type,
            b_member);

    tests.expect(
        a_initial != nullptr &&
        b_initial != nullptr &&
        a_initial->kind ==
            construction_kind::object_binding &&
        b_initial->kind ==
            construction_kind::object_binding &&
        a_initial->operand ==
            G.identity(
                first_static).value() &&
        b_initial->operand ==
            G.identity(
                second_static).value(),
        "constructor reference binds to root-local Header static object");

    const auto static_name =
        strings.find("a");

    tests.expect(
        !identities.find(
            identities.root(),
            static_name,
            identity_kind::object),
        "Header internal static is not visible as a Source Project object");
}


void test_source_string_assignment(test_state& tests) {
    for (const auto literal : {"\"0.0.3\"", "\"\"", "\"A\\n\\x42\\103\"", "\"ab\" \"cd\"",
        "\"12345678\"", "\"\\x100\"", "L\"wide\""}) {
        const bool valid = std::string_view{literal} != "\"12345678\"" &&
            std::string_view{literal} != "\"\\x100\"" && std::string_view{literal} != "L\"wide\"";
        const temporary_source header{"string_header", "typedef unsigned char byte; typedef byte octet; struct Base { char VAL[8]; }; struct Text : Base {}; struct T { Text text; int& ref; byte code; };"};
        const temporary_source source{"string_source", std::string{"octet flag; int value; T a; a.ref = value; a.text.VAL = "} + literal + ";"};
        file_context files;
        lexical_generation lexical;
        file_id h, s;
        (void)files.resolve(header.path(), file_kind::header, h);
        (void)files.resolve(source.path(), file_kind::source, s);
        const std::array<file_id, 2> roots{h, s};
        if (!prepare_all_roots(tests, files, lexical, roots)) return;
        string_table strings;
        identity_space identities{strings};
        graph G;
        source_map sources;
        preprocessor_configuration configuration;
        parser_failure failure;
        const auto status = parse_semantic_project(files, lexical, roots.size(), configuration,
            strings, identities, G, sources, &failure);
        tests.expect(succeeded(status) == valid, "Source string bounds, escapes and literal encoding");
        if (valid && succeeded(status)) {
            const auto entries = G.initialization_entries();
            const std::string expected = std::string_view{literal} == "\"0.0.3\"" ? "0.0.3" :
                std::string_view{literal} == "\"\"" ? "" : std::string_view{literal} == "\"ab\" \"cd\"" ? "abcd" : "A\nBC";
            tests.expect(entries.size() == 8 && G.link_entries().size() == 1, "inherited string and whole-object binding normalize");
            for (const auto& entry : entries) {
                const auto steps = G.endpoint_path_steps(entry.target.member.path());
                const auto index = steps.back().value;
                tests.expect(entry.value.bits() == (index < expected.size() ? static_cast<unsigned char>(expected[index]) : 0),
                    "Source string bytes and terminator are preserved");
            }
        }
    }
}

void test_source_value_initialization(
    test_state& tests) {

    const temporary_source header{
        "source_value_initialization_header",
        "struct T { int param; int& in; int out; };\n"};

    const temporary_source source{
        "source_value_initialization_source",
        "T a;\n"
        "T b;\n"
        "a.param = 5;\n"
        "a.in = b.out;\n"};

    file_context files;
    lexical_generation lexical;

    file_id header_id;
    file_id source_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    header.path(),
                    file_kind::header,
                    header_id)) &&
            succeeded(
                files.resolve(
                    source.path(),
                    file_kind::source,
                    source_id)),
            "resolve Source value initialization roots")) {

        return;
    }

    const std::array<file_id, 2> roots{
        header_id,
        source_id};

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    preprocessor_configuration configuration;
    parser_failure failure;

    if (!tests.expect(
            succeeded(
                parse_semantic_project(
                    files,
                    lexical,
                    roots.size(),
                    configuration,
                    strings,
                    identities,
                    G,
                    sources,
                    &failure)),
            "parse Source scalar initialization and reference link")) {

        return;
    }

    const auto a =
        G.find_object(
            identities.find(
                identities.root(),
                strings.find("a"),
                identity_kind::object));

    const auto type =
        G.find_type(
            identities.find(
                identities.root(),
                strings.find("T"),
                identity_kind::type));

    const auto param =
        G.find_member(
            type,
            strings.find("param"));

    const auto initializations =
        G.initialization_entries();

    tests.expect(
        a &&
        param &&
        initializations.size() == 1 &&
        initializations[0].target.object ==
            G.identity(a) &&
        initializations[0].target.member ==
            endpoint_ref{param} &&
        initializations[0].value.kind ==
            construction_kind::unsigned_integer &&
        initializations[0].value.bits() == 5 &&
        G.link_count() == 1,
        "Source keeps scalar initialization separate from reference link");

    const temporary_source invalid_source{
        "source_value_copy_rejected",
        "T a;\n"
        "T b;\n"
        "a.param = b.out;\n"};

    file_context invalid_files;
    lexical_generation invalid_lexical;

    file_id invalid_header_id;
    file_id invalid_source_id;

    if (!tests.expect(
            succeeded(
                invalid_files.resolve(
                    header.path(),
                    file_kind::header,
                    invalid_header_id)) &&
            succeeded(
                invalid_files.resolve(
                    invalid_source.path(),
                    file_kind::source,
                    invalid_source_id)),
            "resolve invalid Source value-copy roots")) {

        return;
    }

    const std::array<file_id, 2> invalid_roots{
        invalid_header_id,
        invalid_source_id};

    if (!prepare_all_roots(
            tests,
            invalid_files,
            invalid_lexical,
            invalid_roots)) {

        return;
    }

    string_table invalid_strings;
    identity_space invalid_identities{
        invalid_strings};
    graph invalid_G;
    source_map invalid_sources;
    preprocessor_configuration
        invalid_configuration;
    parser_failure invalid_failure;

    const auto invalid_status =
        parse_semantic_project(
            invalid_files,
            invalid_lexical,
            invalid_roots.size(),
            invalid_configuration,
            invalid_strings,
            invalid_identities,
            invalid_G,
            invalid_sources,
            &invalid_failure);

    tests.expect(
        invalid_status ==
            server_status::
                project_configuration_invalid &&
        invalid_failure.kind ==
            parser_failure_kind::unsupported &&
        invalid_G.initialization_count() == 0,
        "Source rejects value-to-value assignment");
}


void test_source_value_initialization_last_wins(
    test_state& tests) {

    const temporary_source header{
        "source_value_last_wins_header",
        "struct T { int param = 1; };\n"};

    const std::string first_text{
        "T a;\n"
        "a.param = 5;\n"};

    const std::string second_text{
        "a.param = 7;\n"};

    const temporary_source first{
        "source_value_last_wins_first",
        first_text};

    const temporary_source second{
        "source_value_last_wins_second",
        second_text};

    file_context files;
    lexical_generation lexical;

    file_id header_id;
    file_id first_id;
    file_id second_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    header.path(),
                    file_kind::header,
                    header_id)) &&
            succeeded(
                files.resolve(
                    first.path(),
                    file_kind::source,
                    first_id)) &&
            succeeded(
                files.resolve(
                    second.path(),
                    file_kind::source,
                    second_id)),
            "resolve Source last-wins initialization roots")) {

        return;
    }

    const std::array<file_id, 3> roots{
        header_id,
        first_id,
        second_id};

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    preprocessor_configuration configuration;
    parser_failure failure;
    std::vector<parser_warning> warnings;

    if (!tests.expect(
            succeeded(
                parse_semantic_project(
                    files,
                    lexical,
                    roots.size(),
                    configuration,
                    strings,
                    identities,
                    G,
                    sources,
                    &failure,
                    &warnings)),
            "parse Source last-wins initialization")) {

        return;
    }

    const auto type =
        G.find_type(
            identities.find(
                identities.root(),
                strings.find("T"),
                identity_kind::type));

    const auto object_identity =
        identities.find(
            identities.root(),
            strings.find("a"),
            identity_kind::object);

    const auto object =
        G.find_object(
            object_identity);

    const auto member =
        G.find_member(
            type,
            strings.find("param"));

    const auto* default_value =
        G.construction(
            type,
            member);

    object_initialization_record init;

    const bool has_init =
        G.initialization(
            {
                object_identity,
                endpoint_ref{member},
            },
            init);

    tests.expect(
        default_value != nullptr &&
        default_value->kind ==
            construction_kind::unsigned_integer &&
        default_value->bits() == 1 &&
        has_init &&
        init.value.kind ==
            construction_kind::unsigned_integer &&
        init.value.bits() == 7 &&
        G.initialization_count() == 1,
        "type default and object init remain distinct; last init wins");

    tests.expect(
        warnings.size() == 1 &&
        warnings[0].kind ==
            parser_warning_kind::
                duplicate_initialization &&
        warnings[0].file ==
            second_id &&
        warnings[0].source.offset ==
            second_text.find("param") &&
        warnings[0].source.length == 5,
        "duplicate object init emits warning at replacing target");

    const auto first_initializations =
        sources.root_initializations(
            first_id);

    const auto second_initializations =
        sources.root_initializations(
            second_id);

    tests.expect(
        first_initializations.size() == 1 &&
        second_initializations.size() == 1 &&
        first_initializations[0] ==
            object_endpoint{
                object_identity,
                endpoint_ref{member}} &&
        second_initializations[0] ==
            object_endpoint{
                object_identity,
                endpoint_ref{member}},
        "BUILD provenance keeps every root that produces the canonical init target");

    const auto second_contributions =
        sources.root(
            second_id);

    tests.expect(
        second_contributions.size() == 1 &&
        second_contributions[0].file ==
            second_id &&
        second_contributions[0].data.kind() ==
            source_data_kind::object &&
        second_contributions[0].data.slot() ==
            object_identity.slot(),
        "object init is provenance of its owning object");
}


void test_source_link_semantic_dependencies(
    test_state& tests) {

    const temporary_source header{
        "source_link_dependency_header",
        "struct T { int& in; int out; };\n"};

    const temporary_source source{
        "source_link_dependency_source",
        "T x;\n"
        "T y;\n"
        "x.in = y.out;\n"};

    file_context files;
    lexical_generation lexical;

    file_id header_id;
    file_id source_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    header.path(),
                    file_kind::header,
                    header_id)) &&
            succeeded(
                files.resolve(
                    source.path(),
                    file_kind::source,
                    source_id)),
            "resolve Source link dependency roots")) {

        return;
    }

    const std::array<file_id, 2> roots{
        header_id,
        source_id};

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    preprocessor_configuration configuration;
    parser_failure failure;

    if (!tests.expect(
            succeeded(
                parse_semantic_project(
                    files,
                    lexical,
                    roots.size(),
                    configuration,
                    strings,
                    identities,
                    G,
                    sources,
                    &failure)),
            "parse Source link dependency fixture")) {

        return;
    }

    const auto type_identity =
        identities.find(
            identities.root(),
            strings.find("T"),
            identity_kind::type);

    const auto x_identity =
        identities.find(
            identities.root(),
            strings.find("x"),
            identity_kind::object);

    const auto y_identity =
        identities.find(
            identities.root(),
            strings.find("y"),
            identity_kind::object);

    const auto type =
        G.find_type(
            type_identity);

    const auto x =
        G.find_object(
            x_identity);

    const auto y =
        G.find_object(
            y_identity);

    const auto dependencies =
        sources.root_dependencies(
            source_id);

    const auto type_dependents =
        sources.dependents(
            source_dependency_ref::type(
                type_identity));

    const auto x_dependents =
        sources.dependents(
            source_dependency_ref::object(
                x_identity));

    const auto y_dependents =
        sources.dependents(
            source_dependency_ref::object(
                y_identity));

    tests.expect(
        type &&
        x &&
        y &&
        dependencies.size() == 3 &&
        dependencies[0] ==
            source_dependency_ref::type(
                type_identity) &&
        dependencies[1] ==
            source_dependency_ref::object(
                x_identity) &&
        dependencies[2] ==
            source_dependency_ref::object(
                y_identity) &&
        type_dependents.size() == 1 &&
        type_dependents[0] == source_id &&
        x_dependents.size() == 1 &&
        x_dependents[0] == source_id &&
        y_dependents.size() == 1 &&
        y_dependents[0] == source_id,
        "Source link records record-layout and object dependencies");
}

void test_source_link_failure_provenance(
    test_state& tests) {

    const temporary_source header{
        "source_link_failure_header",
        "struct T { int& in; int a; int b; };\n"};

    const temporary_source source{
        "source_link_failure_source",
        "T x;\n"
        "T y;\n"
        "x.in = y.a;\n"
        "x.in = y.b;\n"};

    file_context files;
    lexical_generation lexical;

    file_id header_id;
    file_id source_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    header.path(),
                    file_kind::header,
                    header_id)) &&
            succeeded(
                files.resolve(
                    source.path(),
                    file_kind::source,
                    source_id)),
            "resolve Source link provenance roots")) {

        return;
    }

    const std::array<file_id, 2> roots{
        header_id,
        source_id};

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map provenance;
    preprocessor_configuration configuration;
    parser_failure failure;

    tests.expect(
        !succeeded(
            parse_semantic_project(
                files,
                lexical,
                roots.size(),
                configuration,
                strings,
                identities,
                G,
                provenance,
                &failure)),
        "reject conflicting Source link");

    const auto contributions =
        provenance.contribution_entries();

    tests.expect(
        !provenance.finalized() &&
        G.link_count() == 1 &&
        contributions.size() == 4 &&
        contributions[0].file == header_id &&
        contributions[1].file == source_id &&
        contributions[2].file == source_id &&
        contributions[3].file == source_id,
        "failed Source link operation adds no provenance");
}


void test_declarator_arrays(
    test_state& tests) {

    const std::string header_text{
        "struct T {\n"
        "    int values[2][3];\n"
        "    int* pointers[4];\n"
        "    int (*row)[3];\n"
        "    int (&view)[2][3] = values;\n"
        "    int out[4];\n"
        "    int (&in)[4] = out;\n"
        "};\n"};

    const std::string source_text{
        "T a;\n"
        "T b;\n"
        "T objects[2];\n"
        "b.in = a.out;\n"};

    const temporary_source header{
        "declarator_array_header",
        header_text};

    const temporary_source source{
        "declarator_array_source",
        source_text};

    file_context files;
    lexical_generation lexical;

    file_id header_id;
    file_id source_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    header.path(),
                    file_kind::header,
                    header_id)) &&
            succeeded(
                files.resolve(
                    source.path(),
                    file_kind::source,
                    source_id)),
            "resolve declarator-array roots")) {

        return;
    }

    const std::array<file_id, 2> roots{
        header_id,
        source_id};

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;

    if (!tests.expect(
            succeeded(
                parse_semantic_project(
                    files,
                    lexical,
                    roots.size(),
                    configuration,
                    strings,
                    identities,
                    G,
                    sources,
                    &failure)),
            "parse bounded arrays and parenthesized declarators")) {

        return;
    }

    const auto type =
        G.find_type(
            identities.find(
                identities.root(),
                strings.find("T"),
                identity_kind::type));

    if (!tests.expect(
            static_cast<bool>(
                type),
            "resolve declarator-array record")) {

        return;
    }

    const auto member_type =
        [&](std::string_view name,
            type_ref& output,
            member_index& member) {

            member =
                G.find_member(
                    type,
                    strings.find(name));

            const auto* value =
                G.member(
                    type,
                    member);

            if (value == nullptr) {
                return false;
            }

            output = value->type;
            return true;
        };

    const auto derived_is =
        [&](type_ref value,
            derived_type_kind kind,
            std::uint64_t payload,
            type_ref& child) {

            derived_type_record derived;

            if (!G.derived(
                    value,
                    derived) ||
                derived.kind != kind ||
                derived.payload != payload) {

                return false;
            }

            child = derived.child;
            return true;
        };

    const auto is_int =
        [&](type_ref value) {

            intrinsic_type intrinsic;

            return G.intrinsic(
                       value,
                       intrinsic) &&
                intrinsic ==
                    intrinsic_type::signed_int;
        };

    type_ref values_type;
    type_ref pointers_type;
    type_ref row_type;
    type_ref view_type;
    type_ref out_type;
    type_ref in_type;

    member_index values_member;
    member_index pointers_member;
    member_index row_member;
    member_index view_member;
    member_index out_member;
    member_index in_member;

    if (!tests.expect(
            member_type(
                "values",
                values_type,
                values_member) &&
            member_type(
                "pointers",
                pointers_type,
                pointers_member) &&
            member_type(
                "row",
                row_type,
                row_member) &&
            member_type(
                "view",
                view_type,
                view_member) &&
            member_type(
                "out",
                out_type,
                out_member) &&
            member_type(
                "in",
                in_type,
                in_member),
            "resolve declarator-array members")) {

        return;
    }

    type_ref child;
    type_ref leaf;

    tests.expect(
        derived_is(
            values_type,
            derived_type_kind::bounded_array,
            2,
            child) &&
        derived_is(
            child,
            derived_type_kind::bounded_array,
            3,
            leaf) &&
        is_int(leaf),
        "multidimensional array preserves C++ bound order");

    tests.expect(
        derived_is(
            pointers_type,
            derived_type_kind::bounded_array,
            4,
            child) &&
        derived_is(
            child,
            derived_type_kind::pointer,
            0,
            leaf) &&
        is_int(leaf),
        "array of pointers binds suffix outside pointer prefix");

    tests.expect(
        derived_is(
            row_type,
            derived_type_kind::pointer,
            0,
            child) &&
        derived_is(
            child,
            derived_type_kind::bounded_array,
            3,
            leaf) &&
        is_int(leaf),
        "parenthesized declarator distinguishes pointer to array");

    type_ref view_array;
    type_ref view_inner;

    tests.expect(
        derived_is(
            view_type,
            derived_type_kind::lvalue_reference,
            0,
            view_array) &&
        derived_is(
            view_array,
            derived_type_kind::bounded_array,
            2,
            view_inner) &&
        derived_is(
            view_inner,
            derived_type_kind::bounded_array,
            3,
            leaf) &&
        is_int(leaf),
        "reference to multidimensional array preserves native type shape");

    type_ref in_array;

    tests.expect(
        derived_is(
            out_type,
            derived_type_kind::bounded_array,
            4,
            child) &&
        is_int(child) &&
        derived_is(
            in_type,
            derived_type_kind::lvalue_reference,
            0,
            in_array) &&
        in_array == out_type,
        "whole-array reference uses exact array TypeRef");

    const auto* view_binding =
        G.construction(
            type,
            view_member);

    const auto* in_binding =
        G.construction(
            type,
            in_member);

    tests.expect(
        view_binding != nullptr &&
        view_binding->kind ==
            construction_kind::member_binding &&
        view_binding->operand ==
            values_member.value() + 1 &&
        in_binding != nullptr &&
        in_binding->kind ==
            construction_kind::member_binding &&
        in_binding->operand ==
            out_member.value() + 1,
        "array-reference defaults normalize to member bindings");

    const auto objects =
        G.find_object(
            identities.find(
                identities.root(),
                strings.find("objects"),
                identity_kind::object));

    const auto* object =
        G.find(objects);

    type_ref object_element;
    type_handle object_record;

    tests.expect(
        object != nullptr &&
        derived_is(
            object->type,
            derived_type_kind::bounded_array,
            2,
            object_element) &&
        G.named(
            object_element,
            object_record) &&
        object_record == type,
        "Project object may be a bounded array of records");

    tests.expect(
        G.link_count() == 1,
        "whole-array static link binds reference-to-array target");
}

void test_invalid_array_declarators(
    test_state& tests) {

    const auto reject =
        [&](std::string_view label,
            std::string text,
            parser_failure_kind expected_kind,
            std::string_view expected_detail,
            std::string_view marker) {

            const temporary_source source{
                label,
                text};

            parser_failure failure;

            const auto status =
                parse_file(
                    tests,
                    source.path(),
                    failure);

            tests.expect(
                status ==
                    server_status::
                        project_configuration_invalid &&
                failure.kind ==
                    expected_kind &&
                failure.file &&
                failure.source.offset ==
                    text.find(marker) &&
                failure.detail ==
                    expected_detail,
                label);
        };

    reject(
        "array_of_references",
        "struct Bad { int& values[4]; };\n",
        parser_failure_kind::semantic,
        "Arrays of references are not valid C++",
        "[");

    reject(
        "unbounded_array",
        "struct Bad { int values[]; };\n",
        parser_failure_kind::unsupported,
        "Unbounded arrays are not implemented",
        "[");

    reject(
        "zero_array_bound",
        "struct Bad { int values[0]; };\n",
        parser_failure_kind::semantic,
        "Array bound must be greater than zero",
        "0");

    reject(
        "array_bound_expression",
        "struct Bad { int values[2 + 2]; };\n",
        parser_failure_kind::unsupported,
        "Array bound expressions are not implemented",
        "+");
}


void test_subobject_link_endpoints(
    test_state& tests) {

    const std::string header_text{
        "struct T {\n"
        "    int values[2][3];\n"
        "    int& in;\n"
        "};\n"};

    const std::string source_text{
        "T a;\n"
        "T objects[2];\n"
        "objects[1].in = a.values[1][2];\n"
        "a.in = objects[0].values[0][1];\n"};

    const temporary_source header{
        "subobject_link_header",
        header_text};

    const temporary_source source{
        "subobject_link_source",
        source_text};

    file_context files;
    lexical_generation lexical;

    file_id header_id;
    file_id source_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    header.path(),
                    file_kind::header,
                    header_id)) &&
            succeeded(
                files.resolve(
                    source.path(),
                    file_kind::source,
                    source_id)),
            "resolve subobject-link roots")) {

        return;
    }

    const std::array<file_id, 2> roots{
        header_id,
        source_id};

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;

    if (!tests.expect(
            succeeded(
                parse_semantic_project(
                    files,
                    lexical,
                    roots.size(),
                    configuration,
                    strings,
                    identities,
                    G,
                    sources,
                    &failure)),
            "parse indexed subobject static links")) {

        return;
    }

    tests.expect(
        G.link_count() == 2 &&
        G.endpoint_path_count() == 3,
        "indexed endpoints intern only complex canonical paths");

    const auto links =
        G.link_entries();

    if (!tests.expect(
            links.size() == 2,
            "resolve indexed subobject links from dense G storage")) {

        return;
    }

    const auto& first_link =
        links[0];

    const auto& second_link =
        links[1];

    if (!tests.expect(
            first_link.target.member.is_path() &&
            first_link.source.member.is_path() &&
            second_link.target.member.is_member() &&
            second_link.source.member.is_path(),
            "direct and path endpoints preserve compact link representation")) {

        return;
    }

    endpoint_path_record target_path;
    endpoint_path_record source_path;

    const auto target_steps =
        G.endpoint_path_steps(
            first_link.target.member.path());

    const auto source_steps =
        G.endpoint_path_steps(
            first_link.source.member.path());

    tests.expect(
        G.endpoint_path(
            first_link.target.member.path(),
            target_path) &&
        G.endpoint_path(
            first_link.source.member.path(),
            source_path) &&
        target_steps.size() == 2 &&
        target_steps[0].kind ==
            endpoint_path_step_kind::array_index &&
        target_steps[0].value == 1 &&
        target_steps[1].kind ==
            endpoint_path_step_kind::member &&
        source_steps.size() == 3 &&
        source_steps[0].kind ==
            endpoint_path_step_kind::member &&
        source_steps[1].kind ==
            endpoint_path_step_kind::array_index &&
        source_steps[1].value == 1 &&
        source_steps[2].kind ==
            endpoint_path_step_kind::array_index &&
        source_steps[2].value == 2,
        "subobject path order matches source spelling");
}

void test_subobject_link_diagnostics(
    test_state& tests) {

    const std::string header_text{
        "struct T { int a[2]; int& in; };\n"};

    const auto reject =
        [&](std::string_view label,
            std::string source_text,
            parser_failure_kind kind,
            std::string_view detail,
            std::string_view marker) {

            const temporary_source header{
                std::string{label} +
                    "_header",
                header_text};

            const temporary_source source{
                std::string{label} +
                    "_source",
                source_text};

            file_context files;
            lexical_generation lexical;

            file_id header_id;
            file_id source_id;

            if (!tests.expect(
                    succeeded(
                        files.resolve(
                            header.path(),
                            file_kind::header,
                            header_id)) &&
                    succeeded(
                        files.resolve(
                            source.path(),
                            file_kind::source,
                            source_id)),
                    std::string{label} +
                        " resolve roots")) {

                return;
            }

            const std::array<file_id, 2> roots{
                header_id,
                source_id};

            if (!prepare_all_roots(
                    tests,
                    files,
                    lexical,
                    roots)) {

                return;
            }

            preprocessor_configuration configuration;
            string_table strings;
            identity_space identities{strings};
            graph G;
            source_map sources;
            parser_failure failure;

            const auto status =
                parse_semantic_project(
                    files,
                    lexical,
                    roots.size(),
                    configuration,
                    strings,
                    identities,
                    G,
                    sources,
                    &failure);

            tests.expect(
                status ==
                    server_status::
                        project_configuration_invalid &&
                failure.kind ==
                    kind &&
                failure.file ==
                    source_id &&
                failure.source.offset ==
                    source_text.find(marker) &&
                failure.detail ==
                    detail,
                label);
        };

    reject(
        "subobject_link_out_of_bounds",
        "T x;\nx.in = x.a[2];\n",
        parser_failure_kind::semantic,
        "Array link index is outside the declared bound",
        "2]");

    reject(
        "subobject_link_index_expression",
        "T x;\nx.in = x.a[0 + 1];\n",
        parser_failure_kind::unsupported,
        "Array link index expressions are not implemented",
        "+");
}

void test_semantic_type_diagnostics(
    test_state& tests) {

    {
        const std::string text{
            "static Missing value;\n"};

        const temporary_source source{
            "undeclared_object_type",
            text};

        parser_failure failure;

        const auto status =
            parse_file(
                tests,
                source.path(),
                failure);

        tests.expect(
            status ==
                server_status::project_configuration_invalid &&
            failure.kind ==
                parser_failure_kind::semantic &&
            failure.file &&
            failure.source.offset ==
                text.find("Missing") &&
            failure.source.length == 7 &&
            failure.detail ==
                "Named type is not declared in the visible semantic scope",
            "undeclared object type keeps precise source range");
    }

    {
        const std::string text{
            "struct A {\n"
            "    int out;\n"
            "    double& in = out;\n"
            "};\n"};

        const temporary_source source{
            "reference_member_type_mismatch",
            text};

        parser_failure failure;

        const auto status =
            parse_file(
                tests,
                source.path(),
                failure);

        tests.expect(
            status ==
                server_status::project_configuration_invalid &&
            failure.kind ==
                parser_failure_kind::semantic &&
            failure.file &&
            failure.source.offset ==
                text.rfind("out") &&
            failure.source.length == 3 &&
            failure.detail ==
                "Reference binding type does not match bound member type",
            "reference mismatch reports bound member token");
    }

    {
        const std::string text{
            "static int source;\n"
            "struct A {\n"
            "    double& in;\n"
            "    A() : in(source) {}\n"
            "};\n"};

        const temporary_source source{
            "constructor_reference_type_mismatch",
            text};

        parser_failure failure;

        const auto status =
            parse_file(
                tests,
                source.path(),
                failure);

        tests.expect(
            status ==
                server_status::project_configuration_invalid &&
            failure.kind ==
                parser_failure_kind::semantic &&
            failure.file &&
            failure.source.offset ==
                text.rfind("source") &&
            failure.source.length == 6 &&
            failure.detail ==
                "Reference binding type does not match bound object type",
            "constructor mismatch reports source token");
    }

{
    const std::string text{
        "struct T { int value; };\n"
        "static T x = 7;\n"};

    const temporary_source source{
        "record_object_scalar_initializer",
        text};

    parser_failure failure;

    const auto status =
        parse_file(
            tests,
            source.path(),
            failure);

    tests.expect(
        status ==
            server_status::project_configuration_invalid &&
        failure.kind ==
            parser_failure_kind::semantic &&
        failure.file &&
        failure.source.offset ==
            text.rfind("7") &&
        failure.source.length == 1 &&
        failure.detail ==
            "Object initializer is not compatible with target type",
        "record object rejects scalar initializer at literal");
}

{
    const std::string text{
        "struct T { int value = 1.5; };\n"};

    const temporary_source source{
        "integral_member_real_initializer",
        text};

    parser_failure failure;

    const auto status =
        parse_file(
            tests,
            source.path(),
            failure);

    tests.expect(
        status ==
            server_status::project_configuration_invalid &&
        failure.kind ==
            parser_failure_kind::semantic &&
        failure.file &&
        failure.source.offset ==
            text.find("1.5") &&
        failure.source.length == 3 &&
        failure.detail ==
            "Initializer is not compatible with target type",
        "integral member rejects real initializer at literal");
}

    {
        const std::string header_text{
            "struct T { double& in; int out; };\n"};

        const std::string source_text{
            "T x;\n"
            "T y;\n"
            "x.in = y.out;\n"};

        const temporary_source header{
            "link_type_mismatch_header",
            header_text};

        const temporary_source source{
            "link_type_mismatch_source",
            source_text};

        file_context files;
        lexical_generation lexical;
        file_id header_id;
        file_id source_id;

        if (!tests.expect(
                succeeded(
                    files.resolve(
                        header.path(),
                        file_kind::header,
                        header_id)) &&
                succeeded(
                    files.resolve(
                        source.path(),
                        file_kind::source,
                        source_id)),
                "resolve link mismatch roots")) {

            return;
        }

        const std::array<file_id, 2> roots{
            header_id,
            source_id};

        if (!prepare_all_roots(
                tests,
                files,
                lexical,
                roots)) {

            return;
        }

        preprocessor_configuration configuration;
        string_table strings;
        identity_space identities{strings};
        graph G;
        source_map sources;
        parser_failure failure;

        const auto status =
            parse_semantic_project(
                files,
                lexical,
                roots.size(),
                configuration,
                strings,
                identities,
                G,
                sources,
                &failure);

        tests.expect(
            status ==
                server_status::project_configuration_invalid &&
            failure.kind ==
                parser_failure_kind::semantic &&
            failure.file == source_id &&
            failure.source.offset ==
                source_text.rfind("out") &&
            failure.source.length == 3 &&
            failure.detail ==
                "Link source type does not match target reference type",
            "link mismatch reports source member token");
    }

{
    const std::string header_text{
        "struct T { int out; int& in = out; };\n"};

    const std::string source_text{
        "T x;\n"
        "T y;\n"
        "x.in = y.out;\n"};

    const temporary_source header{
        "link_default_override_header",
        header_text};

    const temporary_source source{
        "link_default_override_source",
        source_text};

    file_context files;
    lexical_generation lexical;
    file_id header_id;
    file_id source_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    header.path(),
                    file_kind::header,
                    header_id)) &&
            succeeded(
                files.resolve(
                    source.path(),
                    file_kind::source,
                    source_id)),
            "resolve default-override link roots")) {

        return;
    }

    const std::array<file_id, 2> roots{
        header_id,
        source_id};

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;

    const auto status =
        parse_semantic_project(
            files,
            lexical,
            roots.size(),
            configuration,
            strings,
            identities,
            G,
            sources,
            &failure);

    const auto type =
        G.find_type(
            identities.find(
                identities.root(),
                strings.find("T"),
                identity_kind::type));

    const auto input =
        G.find_member(
            type,
            strings.find("in"));

    const auto* default_binding =
        G.construction(
            type,
            input);

    tests.expect(
        succeeded(status) &&
        G.link_count() == 1 &&
        default_binding != nullptr &&
        default_binding->kind ==
            construction_kind::member_binding,
        "per-object link preserves and overrides the type-level default binding");
}

    {
        const std::string header_text{
            "struct T { int value; };\n"};

        const std::string source_text{
            "T x;\n"
            "T y;\n"
            "x.value = y.value;\n"};

        const temporary_source header{
            "link_target_value_header",
            header_text};

        const temporary_source source{
            "link_target_value_source",
            source_text};

        file_context files;
        lexical_generation lexical;
        file_id header_id;
        file_id source_id;

        if (!tests.expect(
                succeeded(
                    files.resolve(
                        header.path(),
                        file_kind::header,
                        header_id)) &&
                succeeded(
                    files.resolve(
                        source.path(),
                        file_kind::source,
                        source_id)),
                "resolve non-reference link roots")) {

            return;
        }

        const std::array<file_id, 2> roots{
            header_id,
            source_id};

        if (!prepare_all_roots(
                tests,
                files,
                lexical,
                roots)) {

            return;
        }

        preprocessor_configuration configuration;
        string_table strings;
        identity_space identities{strings};
        graph G;
        source_map sources;
        parser_failure failure;

        const auto status =
            parse_semantic_project(
                files,
                lexical,
                roots.size(),
                configuration,
                strings,
                identities,
                G,
                sources,
                &failure);

        tests.expect(
            status ==
                server_status::project_configuration_invalid &&
            failure.kind ==
                parser_failure_kind::unsupported &&
            failure.file == source_id &&
            failure.source.offset ==
                source_text.find("y.value") &&
            failure.source.length == 1 &&
            failure.detail ==
                "Source value initialization requires a scalar constant",
            "Source value initialization rejects value-to-value copy");
    }
}


// HEADER-SEMANTIC-SPACE-01 test
void test_header_semantic_space(test_state& tests) {
    tests.expect(
        classify_header_scope_symbol(token_kind::kw_struct, false) ==
            header_scope_symbol::record_declaration &&
        classify_header_scope_symbol(token_kind::kw_class, false) ==
            header_scope_symbol::record_declaration &&
        classify_header_scope_symbol(token_kind::kw_union, false) ==
            header_scope_symbol::record_declaration,
        "Header semantic space classifies record declarations");

    tests.expect(
        classify_header_scope_symbol(token_kind::kw_int, true) ==
            header_scope_symbol::object_declaration &&
        classify_header_scope_symbol(token_kind::kw_const, true) ==
            header_scope_symbol::object_declaration &&
        classify_header_scope_symbol(token_kind::identifier, true) ==
            header_scope_symbol::object_declaration,
        "Header semantic space collapses object declaration starts");

    tests.expect(
        header_scope_transition(
            false,
            header_scope_symbol::end_of_stream) ==
            header_scope_action::finish_root &&
        header_scope_transition(
            true,
            header_scope_symbol::end_of_stream) ==
            header_scope_action::fail_unclosed_scope &&
        header_scope_transition(
            true,
            header_scope_symbol::close_scope) ==
            header_scope_action::close_scope &&
        header_scope_transition(
            false,
            header_scope_symbol::close_scope) ==
            header_scope_action::reject,
        "Header semantic space preserves scope boundary semantics");
}

void test_parser_provenance(test_state &tests) {
    const temporary_source common{"common_provenance", "struct Shared { int field; };"};
    const auto include = "#include \"" + common.path().filename().string() + "\"\n";
    const temporary_source first{"first_provenance", include};
    const temporary_source second{"second_provenance", include};
    file_context files;
    lexical_generation lexical;
    file_id a, b;
    if (!prepare_root(tests, first.path(), files, lexical, a) ||
        !prepare_root(tests, second.path(), files, lexical, b))
        return;
    lexical_stream stream;
    if (!tests.expect(succeeded(lexer::tokenize(a, files.content(a), stream)) &&
                          succeeded(lexical.publish(a, 0, stream)),
                      "restore first root tokens"))
        return;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    preprocessor_configuration configuration;
    parser_failure failure;
    tests.expect(succeeded(parse_semantic_project(
                     files, lexical, 2, configuration, strings, identities, G, sources, &failure)),
                 "parse shared discovered include");
    tests.expect(files.size() == 3 && sources.finalized() && sources.file_entries().size() == 3 &&
                     sources.contribution_entries().size() == 2 &&
                     sources.file_index_entries().size() == 2 &&
                     sources.root(file_id{1}).size() == 1 &&
                     sources.root(file_id{2}).size() == 1 &&
                     sources.contribution_entries()[0].file == file_id{3} &&
                     sources.contribution_entries()[1].file == file_id{3} &&
                     sources.type_presence_entries()[0] == source_type_presence{2, 2},
                 "parser preserves root-owned provenance for a shared physical include");
    const auto rejected = [&](std::string_view initial, std::string_view conflict,
                              std::size_t expected_contributions) {
        const temporary_source child{"conflict_provenance", conflict};
        const auto text =
            std::string{initial} + "\n#include \"" + child.path().filename().string() + "\"\n";
        const temporary_source root{"conflicting_root", text};
        file_context inputs;
        lexical_generation tokens;
        file_id id;
        if (!prepare_root(tests, root.path(), inputs, tokens, id))
            return;
        string_table atoms;
        identity_space names{atoms};
        graph candidate;
        source_map provenance;
        parser_failure error;
        tests.expect(
            !succeeded(parse_semantic_project(
                inputs, tokens, 1, configuration, atoms, names, candidate, provenance, &error)),
            "reject semantic conflict");
        bool only_root = true;
        for (const auto &entry : provenance.contribution_entries())
            only_root = only_root && entry.file == id;
        tests.expect(!provenance.finalized() &&
                         provenance.contribution_entries().size() == expected_contributions && only_root,
                     "failed G operation adds no child provenance");
    };
    rejected("struct T { int a; };", "struct T { double a; };", 1);
    rejected("static int value;", "static double value;", 1);
}
}
}

int main() {
    using namespace cw::server;

    try {
        test_state tests;

        test_lexical_symbols_v2(
            tests);

        test_prepared_include_v2(
            tests);

        test_preprocessor_v2_include_execution(
            tests);

        test_graph_resolved_v2(
            tests);

        test_header_parser_v2_parity_01(
            tests);

        test_header_parser_v2_parity_02(
            tests);

        test_header_multi_base_01(
            tests);

        test_header_v2_differential_01(tests);

        test_header_v2_differential_02(tests);

        test_header_parser_v2_minimal(
            tests);

        test_header_source_semantic_split(
            tests);

        test_header_semantic_space(tests);

        test_sparse_semantic_replay_order(
            tests);

        test_source_preprocessor_rejected(
            tests);

        test_header_streaming_order(
            tests);

        test_header_static_constructor_binding(
            tests);

        test_record_scratch_isolation(tests);
        test_nested_constructor_defaults(tests);
        test_constructor_array_defaults(tests);
        test_constructor_aggregate_defaults(tests);

        test_class_abi_semantics(
            tests);

        test_source_string_assignment(tests);
        test_source_value_initialization(
            tests);

        test_flat_source_cursor(tests);
        test_source_value_initialization_last_wins(
            tests);

        test_source_link_semantic_dependencies(
            tests);

        test_source_link_failure_provenance(
            tests);

        test_declarator_arrays(
            tests);

        test_invalid_array_declarators(
            tests);

        test_subobject_link_endpoints(
            tests);

        test_subobject_link_diagnostics(
            tests);

        test_semantic_type_diagnostics(
            tests);

        test_parser_provenance(tests);

        test_header_name_diagnostics(
            tests);

        test_conversion_operators(tests);
        test_assignment_operators(tests);
        test_compound_assignment_operators(tests);
        test_logical_not_operator(tests);
        test_comparison_operators(tests);
        test_global_include_directories(tests);
        test_subscript_operators(tests);
        test_microsoft_int64(tests);

        test_conditional_entry_floor(
            tests);

        test_pragma_once_and_angled_includes(tests);
        test_parallel_include_preparation(tests);
        test_self_include_guard(
            tests);

        test_parser_scope_limit(
            tests);

        if (tests.failures != 0) {
            std::cerr
                << tests.failures
                << " frontend regression test(s) failed\n";
            return 1;
        }

        std::cout
            << "frontend regression tests passed\n";

        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "UNEXPECTED EXCEPTION: "
            << error.what()
            << '\n';
        return 1;
    }
    catch (...) {
        std::cerr
            << "UNEXPECTED NON-STANDARD EXCEPTION\n";
        return 1;
    }
}
