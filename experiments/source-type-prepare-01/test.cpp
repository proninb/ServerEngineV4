#include "project/parser/source_type_prepare_policy.hpp"
#include <cstdlib>
#define main frontend_regression_main
#include "../../tests/frontend_regression_tests.cpp"
#undef main
namespace cw::server {
namespace {
void test_type_preparation(test_state& tests) {
    string_table strings;
    identity_space identities{strings};
    graph G;
    std::vector<type_ref> types;
    std::vector<type_handle> records;
    for (std::size_t i = 0; i < 5000; ++i) {
        string_id name;
        identity_ref who;
        type_handle record;
        if (!tests.expect(succeeded(strings.intern("Prepare" + std::to_string(i), name)) &&
            succeeded(identities.resolve(identities.root(), name, identity_kind::type, who)) &&
            succeeded(G.declare_record(who, graph_record_kind::struct_type, record)),
            "prepare collision catalog")) return;
        types.push_back(G.named(record));
        records.push_back(record);
    }
    source_endpoint_resolver resolver{G};
    // More live inputs than cache slots; second and reverse passes must survive replacement.
    for (int round = 0; round < 3; ++round) for (std::size_t j = 0; j < types.size(); ++j) {
        const auto i = round == 2 ? types.size() - 1 - j : j;
        type_handle actual;
        bool dereference = true;
        tests.expect(resolver.record_type(types[i], actual, dereference) &&
            actual == records[i] && !dereference, "exact type survives descriptor cache collisions");
    }
    auto qualify = [&](type_ref child, derived_type_kind kind) {
        type_ref result;
        tests.expect(succeeded(G.derive(child, kind, 0, result)), "create qualified preparation input");
        return result;
    };
    const auto cv = qualify(qualify(types[0], derived_type_kind::const_qualified),
        derived_type_kind::volatile_qualified);
    const auto lref = qualify(cv, derived_type_kind::lvalue_reference);
    const auto rref = qualify(cv, derived_type_kind::rvalue_reference);
    const auto scalar = G.intrinsic(intrinsic_type::signed_int);
    const auto scalar_ref = qualify(scalar, derived_type_kind::lvalue_reference);
    for (int round = 0; round < 3; ++round) for (const auto type : {cv, lref, rref}) {
        source_typed_endpoint output;
        output.type = type;
        output.path.push_back({2, endpoint_path_step_kind::array_index, {}});
        type_handle actual;
        source_endpoint_resolver::failure error;
        tests.expect(succeeded(resolver.prepare_member(output, actual, error)) &&
            actual == records[0] && error == source_endpoint_resolver::failure::none &&
            output.path.size() == (type == cv ? 1u : 2u) &&
            output.path.front().value == 2 &&
            (type == cv || output.path.back().kind == endpoint_path_step_kind::dereference),
            "cached CV/reference preparation preserves caller prefix and dereference");
    }
    for (const auto type : {type_ref{}, scalar, scalar_ref}) {
        for (int round = 0; round < 2; ++round) {
            source_typed_endpoint output;
            output.type = type;
            type_handle actual = records[0];
            source_endpoint_resolver::failure error;
            tests.expect(!succeeded(resolver.prepare_member(output, actual, error)) &&
                !actual && error == source_endpoint_resolver::failure::record_required &&
                output.path.size() == (type == scalar_ref ? 1u : 0u),
                "invalid preparation resets outputs and preserves error-side dereference");
        }
    }
    string_id late_name;
    identity_ref late_who;
    (void)strings.intern("Late", late_name);
    (void)identities.resolve(identities.root(), late_name, identity_kind::type, late_who);
    const auto late = G.named(late_who);
    type_handle actual;
    bool dereference = false;
    tests.expect(!resolver.record_type(late, actual, dereference), "missing named type is not prepared");
    type_handle late_record;
    (void)G.declare_record(late_who, graph_record_kind::struct_type, late_record);
    tests.expect(resolver.record_type(late, actual, dereference) && actual == late_record,
        "failed preparation is not cached across catalog append");
}
#ifdef CW_SOURCE_TYPE_PREPARE_AB
void test_error_order(test_state& tests) {
    const auto selected = detail::source_type_prepare_cache;
    const temporary_source header{"prepare_errors_header",
        "struct T { int value; T& next; int values[2]; };"};
    for (const char* body : {
        "a.value = 1; a.value.missing = 0; a.missing = 1;",
        "a.next.value = 1; a.next.missing = 0; a.value = 2;",
        "a.values[0] = 1; a.values[2] = 0; a.missing = 1;",
        "a.value = 1; a.value. = 0;",
        "a.value = 1; a.missing = 0;",
        "a.value = 1; a.next.value = ;",
        "a.value = 1; a.values[0].missing = 0;"}) {
        const temporary_source source{"prepare_errors_source", std::string{"T a; "} + body};
        struct result {
            server_status status;
            parser_failure_kind kind;
            file_id file;
            source_range source;
            std::string detail;
        } values[2];
        for (int mode = 0; mode < 2; ++mode) {
            detail::source_type_prepare_cache = mode != 0;
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
                strings, identities, G, sources, &failure, nullptr, nullptr, true);
            values[mode] = {status, failure.kind, failure.file, failure.source, std::string{failure.detail}};
        }
        tests.expect(!succeeded(values[0].status) && values[0].status == values[1].status &&
            values[0].kind == values[1].kind && values[0].file == values[1].file &&
            values[0].source.offset == values[1].source.offset &&
            values[0].source.length == values[1].source.length && values[0].detail == values[1].detail,
            "type preparation preserves exact first error and physical origin");
    }
    detail::source_type_prepare_cache = selected;
}
#endif
}
}
int main() {
#ifdef CW_SOURCE_TYPE_PREPARE_AB
    const auto* value = std::getenv("CW_SOURCE_TYPE_PREPARE");
    cw::server::detail::source_type_prepare_cache = value && std::string_view{value} == "1";
#ifdef CW_SOURCE_TYPE_PREPARE_TEST
    const auto* fail = std::getenv("CW_SOURCE_TYPE_PREPARE_UNAVAILABLE");
    cw::server::detail::source_type_prepare_force_unavailable = fail && std::string_view{fail} == "1";
#endif
#endif
    cw::server::test_state tests;
    cw::server::test_type_preparation(tests);
#ifdef CW_SOURCE_TYPE_PREPARE_AB
    cw::server::test_error_order(tests);
#endif
    if (tests.failures) return 1;
    return frontend_regression_main();
}
