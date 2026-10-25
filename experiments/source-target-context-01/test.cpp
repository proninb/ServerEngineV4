#include "project/parser/source_target_context_policy.hpp"
#include <cstdlib>
#define main frontend_regression_main
#include "../../tests/frontend_regression_tests.cpp"
#undef main

namespace cw::server {
namespace {
struct target_observation {
    server_status status{};
    parser_failure_kind failure{};
    std::uint32_t file = 0, offset = 0, length = 0;
    std::string detail;
    std::vector<std::uint64_t> state;
    std::vector<std::string> warnings;
    bool operator==(const target_observation&) const = default;
};

target_observation observe_targets(test_state& tests, const temporary_source& header,
    const temporary_source& first, const temporary_source& second, bool expected_success) {
    file_context files;
    lexical_generation lexical;
    std::array<file_id, 3> roots;
    (void)files.resolve(header.path(), file_kind::header, roots[0]);
    (void)files.resolve(first.path(), file_kind::source, roots[1]);
    (void)files.resolve(second.path(), file_kind::source, roots[2]);
    if (!prepare_all_roots(tests, files, lexical, roots)) return {};
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    preprocessor_configuration configuration;
    parser_failure failure;
    std::vector<parser_warning> warnings;
    const auto status = parse_semantic_project(files, lexical, roots.size(), configuration,
        strings, identities, G, sources, &failure, &warnings, nullptr, true);
    (void)tests.expect(succeeded(status) == expected_success, "target-context fixture status");
    target_observation result{status, failure.kind, failure.file.value(),
        failure.source.offset, failure.source.length, std::string{failure.detail}};
    result.state = {G.object_count(), G.link_count(), G.initialization_count()};
    for (const auto& entry : G.initialization_entries()) {
        result.state.insert(result.state.end(), {entry.target.object.value(), entry.target.member.value(),
            entry.value.bits(), static_cast<std::uint64_t>(entry.value.kind), entry.value.operand});
    }
    for (const auto& entry : G.link_entries())
        result.state.insert(result.state.end(), {entry.source.object.value(), entry.source.member.value(),
            entry.target.object.value(), entry.target.member.value()});
    for (const auto& entry : sources.contribution_entries())
        result.state.insert(result.state.end(), {entry.file.value(), entry.data.raw()});
    for (const auto& entry : sources.root_entries())
        result.state.insert(result.state.end(), {entry.begin, entry.count});
    for (const auto& entry : sources.file_entries())
        result.state.insert(result.state.end(), {entry.begin, entry.count});
    for (const auto& entry : sources.file_index_entries()) result.state.push_back(entry);
    for (const auto root : roots) {
        for (const auto dep : sources.root_dependencies(root)) result.state.push_back(dep.raw());
        for (const auto target : sources.root_initializations(root))
            result.state.insert(result.state.end(), {target.object.value(), target.member.value()});
    }
    for (const auto& warning : warnings) {
        result.state.insert(result.state.end(), {static_cast<std::uint64_t>(warning.kind), warning.file.value(),
            warning.source.offset, warning.source.length});
        result.warnings.emplace_back(warning.detail);
    }
    if (expected_success) {
        const auto who = identities.find(identities.root(), strings.find("a"), identity_kind::object);
        const auto type = G.find_type(identities.find(identities.root(), strings.find("T"), identity_kind::type));
        const auto member = G.find_member(type, strings.find("P1"));
        object_initialization_record value;
        (void)tests.expect(G.initialization({who, endpoint_ref::from_raw(member.value())}, value) &&
            value.value.bits() == 7, "RHS, namespace and root changes preserve final outer target value");
        const auto scope = identities.find(identities.root(), strings.find("N"), identity_kind::namespace_scope);
        const auto inner = identities.find(scope, strings.find("a"), identity_kind::object);
        (void)tests.expect(G.initialization({inner, endpoint_ref::from_raw(member.value())}, value) &&
            value.value.bits() == 4, "shadowed target retains its separate WHO");
    }
    return result;
}

void target_context_contract(test_state& tests) {
    const temporary_source header{"target_context_header",
        "struct T { char N[8]; int P1; int& IN; int OUT; }; "
        "struct U { double P1; }; struct Signal { int value; };"};
    const temporary_source first{"target_context_first",
        "T a; T b; Signal Signal; Signal.value = 10; Signal other; other.value = 11; "
        "a.P1 = 1; a.N = \"longer\"; a.IN = b.OUT; a.P1 = 2; "
        "namespace N { T a; a.P1 = 3; a.IN = b.OUT; a.P1 = 4; } a.P1 = 5;"};
    const temporary_source second{"target_context_second",
        "extern T a; extern T b; a.P1 = 6; a.N = \"\"; a.IN = b.OUT; a.P1 = 7;"};
#ifdef CW_SOURCE_TARGET_CONTEXT_AB
    const auto selected = detail::source_target_context;
    detail::source_target_context = false;
    const auto reference = observe_targets(tests, header, first, second, true);
    detail::source_target_context = true;
    const auto candidate = observe_targets(tests, header, first, second, true);
    (void)tests.expect(reference == candidate,
        "target cache preserves G, SourceMap, warnings and origins across visibility boundaries");
    for (const char* program : {
        "T a; a.P1 = 1; T a;",
        "T a; a.P1 = 1; missing.P1 = 2;",
        "T a; a.P1 = 1; a. = 2;",
        "T a; a.P1 = 1; a.P1.missing = 2; a.OUT = 3;",
        "extern T a; a.P1 = 1; extern U a;",
        "T a; a.P1 = 1; a.N = \"12345678\";",
        "T a; T b; a.IN = b.OUT; a.P1 = ;",
        "const bool k; k = true;",
        "T a; a.P1 = 1; namespace N { T a; a.P1 = 2; a.missing = 3; }",
        "Signal Signal; Signal.value = 1; Signal missing; missing.wrong = 2;"}) {
        const temporary_source bad{"target_context_bad", program};
        const temporary_source empty{"target_context_empty", ""};
        detail::source_target_context = false;
        const auto ref = observe_targets(tests, header, bad, empty, false);
        detail::source_target_context = true;
        const auto value = observe_targets(tests, header, bad, empty, false);
        (void)tests.expect(ref == value, "target cache preserves exact first diagnostic and committed prefix");
    }
    detail::source_target_context = selected;
#else
    (void)observe_targets(tests, header, first, second, true);
#endif
}
}
}

int main() {
#ifdef CW_SOURCE_TARGET_CONTEXT_AB
    const auto* value = std::getenv("CW_SOURCE_TARGET_CONTEXT");
    cw::server::detail::source_target_context = value && std::string_view{value} == "1";
#endif
    cw::server::test_state tests;
    cw::server::target_context_contract(tests);
    if (tests.failures) return 1;
    return frontend_regression_main();
}
