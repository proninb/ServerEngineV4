// HEADER-V2-DIFFERENTIAL-02 — multifile oracle, test-only.
// Include inside the existing frontend_regression_tests.cpp anonymous namespace.
// A single fixture owns its physical sources; OLD and V2 use separate semantic
// worlds. Compare WHO projections and semantic include edges, never file_id,
// string_id, WHERE, or preallocated physical-closure cardinalities.

struct differential02_files final {
    std::vector<std::unique_ptr<temporary_source>> owned;
    std::vector<const temporary_source*> roots;

    [[nodiscard]] const temporary_source* add(
        std::string_view label, const std::string& content,
        bool semantic_root = false) {

        owned.push_back(std::make_unique<temporary_source>(label, content));
        const auto* result = owned.back().get();
        if (semantic_root) roots.push_back(result);
        return result;
    }
};

[[nodiscard]] std::string differential02_filename(
    const file_context& files, file_id file) {

    if (!file) return "<invalid-file>";
    const auto path = files.path(file);
    if (path.empty()) return "<unknown-file>";
    return std::filesystem::path{
        std::basic_string<file_path_char>{path.begin(), path.end()}}
        .filename().string();
}

[[nodiscard]] std::vector<std::string> differential02_edges(
    const file_context& files) {

    std::vector<std::string> edges;
    for (std::uint32_t slot = 1; slot <= files.size(); ++slot) {
        const file_id source{slot};
        for (const auto target : files.dependencies(source)) {
            edges.push_back(differential02_filename(files, source) + " -> " +
                differential02_filename(files, target));
        }
    }
    std::sort(edges.begin(), edges.end());
    return edges;
}

void differential02_dump(
    const std::string& context,
    const std::vector<std::string>& old_rows,
    const std::vector<std::string>& new_rows) {

    std::cerr << context << "\nOLD:\n";
    for (const auto& row : old_rows) std::cerr << "  " << row << '\n';
    std::cerr << "V2:\n";
    for (const auto& row : new_rows) std::cerr << "  " << row << '\n';
}

// Test registration/physical-acquisition order without changing the semantic
// sequence in which the Header roots are parsed.
[[nodiscard]] std::vector<std::string> differential02_run(
    test_state& tests,
    const differential02_files& input_files,
    std::string_view fixture_name,
    bool expect_success,
    std::size_t expected_edges,
    const std::vector<std::string>& required_who,
    std::string_view inactive_name = {},
    const temporary_source* expected_error_file = nullptr,
    bool reverse_v2_registration = false,
    std::string_view first_root_forward_who = {}) {

    const std::string context =
        "HEADER-V2-DIFFERENTIAL-02/" + std::string{fixture_name} +
        (reverse_v2_registration ? "/reversed-registration" : "");

    if (!tests.expect(!input_files.roots.empty(), context + "/fixture-roots"))
        return {};

    const auto directory = input_files.roots.front()->path().parent_path();

    // OLD: prepare and execute the real production semantic project frontend.
    file_context old_files;
    lexical_generation old_lexical;
    std::vector<file_id> old_roots;
    for (const auto* root : input_files.roots) {
        file_id id;
        if (!tests.expect(
                succeeded(old_files.resolve(root->path(), file_kind::header, id)) && id,
                context + "/old-register-root")) return {};
        old_roots.push_back(id);
    }
    if (!prepare_all_roots(tests, old_files, old_lexical, old_roots)) return {};

    preprocessor_configuration old_config;
    old_config.root_directory = directory;
    string_table old_strings;
    identity_space old_ids{old_strings};
    graph old_graph;
    source_map old_sources;
    parser_failure old_failure;
    const auto old_status = parse_semantic_project(
        old_files, old_lexical, old_roots.size(), old_config, old_strings,
        old_ids, old_graph, old_sources, &old_failure);

    // V2: same source text, independent physical and semantic contexts.
    file_context v2_files;
    std::vector<file_id> v2_roots(input_files.roots.size());
    for (std::size_t i = 0; i < input_files.roots.size(); ++i) {
        const auto index = reverse_v2_registration
            ? input_files.roots.size() - i - 1 : i;
        if (!tests.expect(
                succeeded(v2_files.resolve(input_files.roots[index]->path(),
                    file_kind::header, v2_roots[index])) && v2_roots[index],
                context + "/v2-register-root")) return {};
    }
    preprocessor_configuration v2_config;
    v2_config.root_directory = directory;
    prepared_include_closure_v2 closure;
    prepared_include_failure_v2 preparation_failure;
    if (!tests.expect(succeeded(prepare_header_include_closure_v2(
            v2_files, v2_roots, v2_config, closure, &preparation_failure)),
            context + "/prepare-closure")) return {};

    string_table v2_strings;
    identity_space v2_ids{v2_strings};
    graph v2_graph;
    preprocessor_v2_failure preprocessor_failure;
    parser_v2_failure v2_failure;
    semantic_preprocessor_v2 preprocessor{
        v2_files, closure.view(), v2_config, v2_strings, &preprocessor_failure};
    auto v2_status = server_status::success;
    for (std::size_t root_index = 0; root_index < v2_roots.size(); ++root_index) {
        const auto root = v2_roots[root_index];
        v2_status = preprocessor.start(root);
        if (!succeeded(v2_status)) break;
        header_parser_v2 parser{preprocessor, v2_ids, v2_graph, &v2_failure};
        v2_status = parser.parse();
        if (!succeeded(v2_status)) break;
        if (!tests.expect(preprocessor.finished(), context + "/v2-finished-root"))
            return {};
        if (root_index == 0 && !first_root_forward_who.empty()) {
            // Parser V2 declarations may create WHO but must not allocate WHERE.
            bool declared_without_where = false;
            for (std::uint32_t slot = 2; slot <= v2_ids.size(); ++slot) {
                const auto who = v2_ids.at_slot(slot);
                if (who && who.kind() == identity_kind::type &&
                    differential_identity(v2_ids, v2_strings, who) == first_root_forward_who) {
                    declared_without_where = !v2_graph.find_type(who);
                    break;
                }
            }
            tests.expect(declared_without_where, context + "/forward-is-who-only");
        }
    }

    const bool old_ok = succeeded(old_status);
    const bool v2_ok = succeeded(v2_status);
    if (!tests.expect(old_ok == expect_success, context + "/old-expectation")) {
        std::cerr << context << " OLD failure=" << old_failure.detail << '\n';
        return {};
    }
    if (!tests.expect(v2_ok == old_ok, context + "/outcome")) {
        std::cerr << context << " old-kind=" << static_cast<int>(old_failure.kind)
                  << " old-detail=" << old_failure.detail
                  << " v2-kind=" << static_cast<int>(v2_failure.kind)
                  << " v2-detail=" << v2_failure.detail
                  << " preprocessor-kind=" << static_cast<int>(preprocessor_failure.kind)
                  << " preprocessor-detail=" << preprocessor_failure.detail << '\n';
        return {};
    }
    if (!old_ok) {
        // Old lexical/preprocessor categories are intentionally not mapped to
        // parser-only V2 failures. These fixtures target semantic diagnostics.
        unsigned old_category = 0;
        switch (old_failure.kind) {
        case parser_failure_kind::syntax: old_category = 1; break;
        case parser_failure_kind::semantic: old_category = 2; break;
        case parser_failure_kind::unsupported: old_category = 3; break;
        default: break;
        }
        tests.expect(old_category == static_cast<unsigned>(v2_failure.kind),
            context + "/category");
        const auto old_file = differential02_filename(old_files, old_failure.file);
        const auto v2_file = differential02_filename(v2_files, v2_failure.file);
        if (!tests.expect(old_file == v2_file &&
                (expected_error_file == nullptr ||
                    old_file == expected_error_file->path().filename().string()),
                context + "/diagnostic-file")) {
            std::cerr << context << " OLD file=" << old_file
                      << " V2 file=" << v2_file << '\n';
        }
        if (!tests.expect(old_failure.source.offset == v2_failure.source.offset &&
                old_failure.source.length == v2_failure.source.length,
                context + "/diagnostic-span")) {
            std::cerr << context << " OLD span=" << old_failure.source.offset << ":"
                      << old_failure.source.length << " V2 span="
                      << v2_failure.source.offset << ":"
                      << v2_failure.source.length << '\n';
        }
        return {};
    }

    const auto old_view = differential_projection(old_graph, old_ids, old_strings);
    const auto v2_view = differential_projection(v2_graph, v2_ids, v2_strings);
    if (old_view != v2_view)
        differential02_dump(context + "/semantic-graph", old_view, v2_view);
    tests.expect(old_view == v2_view, context + "/semantic-graph");

    for (const auto& who : required_who) {
        tests.expect(std::binary_search(old_view.begin(), old_view.end(), "who:" + who) &&
            std::binary_search(v2_view.begin(), v2_view.end(), "who:" + who),
            context + "/required-who/" + who);
    }
    if (!inactive_name.empty()) {
        // The speculative V2 physical read must not publish identity or
        // intern even the *spelling* of an inactive included declaration.
        tests.expect(!old_strings.find(inactive_name) &&
            !v2_strings.find(inactive_name), context + "/inactive-spelling");
        tests.expect(std::none_of(old_view.begin(), old_view.end(), [&](const auto& s) {
                return s.find(std::string{inactive_name}) != std::string::npos;
            }) &&
            std::none_of(v2_view.begin(), v2_view.end(), [&](const auto& s) {
                return s.find(std::string{inactive_name}) != std::string::npos;
            }), context + "/inactive-who");
    }

    if (!old_files.dependency_topology_finalized())
        tests.expect(succeeded(old_files.finalize_dependency_topology()),
            context + "/old-finalize-dependencies");
    if (!v2_files.dependency_topology_finalized())
        tests.expect(succeeded(v2_files.finalize_dependency_topology()),
            context + "/v2-finalize-dependencies");
    const auto old_edges = differential02_edges(old_files);
    const auto v2_edges = differential02_edges(v2_files);
    if (old_edges != v2_edges)
        differential02_dump(context + "/semantic-dependencies", old_edges, v2_edges);
    tests.expect(old_edges == v2_edges, context + "/dependency-parity");
    tests.expect(old_edges.size() == expected_edges &&
        v2_edges.size() == expected_edges, context + "/expected-dependencies");
    return v2_view;
}

void test_header_v2_differential_02(test_state& tests) {
    {
        differential02_files files;
        files.add("d02_multi_a", "namespace N { struct A { int X; }; }\n", true);
        files.add("d02_multi_b", "namespace N { struct B { A* Ptr; }; }\n", true);
        const std::vector<std::string> expected{"::N::A", "::N::B"};
        const auto first = differential02_run(
            tests, files, "multi-roots", true, 0, expected);
        const auto reversed = differential02_run(
            tests, files, "multi-roots", true, 0, expected, {}, nullptr, true);
        tests.expect(!first.empty() && first == reversed,
            "HEADER-V2-DIFFERENTIAL-02/multi-roots/registration-order-invariance");
    }
    {
        differential02_files files;
        files.add("d02_forward_1", "namespace N { struct Leaf; }\n", true);
        files.add("d02_forward_2",
            "namespace N { struct Node { Leaf* Next; }; struct Leaf { int X; }; }\n", true);
        const std::vector<std::string> expected{"::N::Leaf", "::N::Node"};
        const auto first = differential02_run(
            tests, files, "forward-across-roots", true, 0, expected,
            {}, nullptr, false, "::N::Leaf");
        const auto reversed = differential02_run(
            tests, files, "forward-across-roots", true, 0, expected,
            {}, nullptr, true, "::N::Leaf");
        tests.expect(!first.empty() && first == reversed,
            "HEADER-V2-DIFFERENTIAL-02/forward-across-roots/registration-order-invariance");
    }
    {
        differential02_files files;
        const auto* leaf = files.add("d02_nested_leaf",
            "#pragma once\nnamespace N { struct Leaf { int X; }; }\n");
        const auto* middle = files.add("d02_nested_middle",
            "#pragma once\n#include \"" + leaf->path().filename().string() +
            "\"\nnamespace N { struct Mid { Leaf* P; }; }\n");
        files.add("d02_nested_root",
            "#include \"" + middle->path().filename().string() +
            "\"\nnamespace N { struct Root { Mid* P; }; }\n", true);
        (void)differential02_run(tests, files, "nested-includes", true, 2,
            {"::N::Leaf", "::N::Mid", "::N::Root"});
    }
    {
        differential02_files files;
        const auto* unused = files.add("d02_inactive_leaf",
            "struct InactiveOnly { int Hidden; };\n");
        files.add("d02_inactive_root",
            "#ifdef NEVER\n#include \"" + unused->path().filename().string() +
            "\"\n#endif\nstruct Keep { int X; };\n", true);
        (void)differential02_run(tests, files, "inactive-include", true, 0,
            {"::Keep"}, "InactiveOnly");
    }
    {
        differential02_files files;
        const auto* leaf = files.add("d02_error_leaf",
            "struct Broken { int X; int X; };\n");
        files.add("d02_error_root",
            "#include \"" + leaf->path().filename().string() + "\"\n", true);
        (void)differential02_run(tests, files, "diagnostic-in-include", false, 1,
            {}, {}, leaf);
    }
}
