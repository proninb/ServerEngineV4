#include "project/source/source_map.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace cw::server {
namespace {

struct test_state final {
    int failures = 0;

    bool expect(bool condition, std::string_view name) {

        if (condition) {
            return true;
        }

        ++failures;

        std::cerr << "FAILED: " << name << '\n';

        return false;
    }
};

[[nodiscard]] constexpr source_data_ref data(source_data_kind kind, std::uint32_t slot) noexcept {

    return source_data_ref::from_raw(
        (static_cast<std::uint32_t>(kind) << source_data_ref::kind_shift) | slot);
}

void test_root_ownership_and_file_projection(test_state &tests) {

    string_table strings;
    identity_space identities{strings};
    graph G;
    const auto make_identity = [&](std::string_view name, identity_kind kind) {
        string_id text;
        identity_ref id;
        tests.expect(succeeded(strings.intern(name, text)), "intern fixture identity");
        tests.expect(succeeded(identities.resolve(identities.root(), text, kind, id)),
                     "resolve fixture identity");
        return id;
    };
    const auto type_identity = make_identity("T", identity_kind::type);
    const auto object_identity = make_identity("o", identity_kind::object);
    type_handle type;
    tests.expect(succeeded(G.declare_record(type_identity, graph_record_kind::struct_type, type)),
                 "declare fixture type");
    string_id member_name;
    tests.expect(succeeded(strings.intern("value", member_name)), "intern member");
    const member_record member{
        member_name, G.intrinsic(intrinsic_type::signed_int), graph_member_access::public_access};
    tests.expect(succeeded(G.define_record(type, graph_record_kind::struct_type, {&member, 1})),
                 "define fixture type");
    object_handle obj;
    tests.expect(succeeded(G.add_object(object_identity, G.named(type), obj)),
                 "add fixture object");
    link_handle edge;
    const object_endpoint endpoint{obj, G.find_member(type, member_name)};
    tests.expect(succeeded(G.add_link(endpoint, endpoint, edge)), "add fixture link");
    source_map map;
    const auto declaration = source_data_ref::type_declaration(type_identity);
    const auto definition = source_data_ref::type_definition(type_identity);
    const auto object = source_data_ref::object(object_identity);
    const auto link = source_data_ref::link(edge);

    if (!tests.expect(succeeded(map.reset(3)), "reset") ||
        !tests.expect(succeeded(map.begin_root(file_id{1})), "begin root 1") ||
        !tests.expect(succeeded(map.add(file_id{3}, declaration)), "root 1 declaration") ||
        !tests.expect(succeeded(map.add(file_id{3}, definition)),
                      "root 1 definition dominates declaration") ||
        !tests.expect(succeeded(map.add(file_id{2}, object)), "root 1 object") ||
        !tests.expect(succeeded(map.add_dependency(type)), "root 1 type dependency") ||
        !tests.expect(succeeded(map.add_dependency(type)), "root 1 duplicate type dependency") ||
        !tests.expect(succeeded(map.end_root()), "end root 1") ||
        !tests.expect(succeeded(map.begin_root(file_id{2})), "begin root 2") ||
        !tests.expect(succeeded(map.add(file_id{3}, definition)),
                      "root 2 same physical definition") ||
        !tests.expect(succeeded(map.add(file_id{3}, link)), "root 2 link") ||
        !tests.expect(succeeded(map.add_dependency(type)), "root 2 type dependency") ||
        !tests.expect(succeeded(map.add_dependency(obj)), "root 2 object dependency") ||
        !tests.expect(succeeded(map.end_root()), "end root 2") ||
        !tests.expect(succeeded(map.finalize(3, identities, G)), "finalize")) {

        return;
    }

    const auto root1 = map.root(file_id{1});

    const auto root2 = map.root(file_id{2});

    tests.expect(root1.size() == 2 && root1[0].file == file_id{3} && root1[0].data == definition &&
                     root1[1].file == file_id{2} && root1[1].data == object,
                 "root 1 owns canonical contributions");

    tests.expect(root2.size() == 2 && root2[0].file == file_id{3} && root2[0].data == definition &&
                     root2[1].file == file_id{3} && root2[1].data == link,
                 "root 2 ownership remains independent");

    source_map_file_view file3;

    if (!tests.expect(map.file(file_id{3}, file3), "file 3 view")) {

        return;
    }

    tests.expect(file3.size() == 3, "physical file projection references every root-owned contribution");

    std::uint32_t definition_count = 0;
    bool found_link = false;

    for (std::size_t index = 0; index < file3.size(); ++index) {

        const auto value = file3[index];

        if (value.file != file_id{3}) {

            tests.expect(false, "file projection preserves physical owner");
            continue;
        }

        definition_count += value.data == definition ? 1u : 0u;

        found_link = found_link || value.data == link;
    }

    tests.expect(definition_count == 2 && found_link,
                 "file projection preserves root ownership over shared physical data");

    source_map_file_view file2;

    tests.expect(map.file(file_id{2}, file2) && file2.size() == 1 && file2[0].data == object,
                 "second physical file projection");

    tests.expect(map.root(file_id{3}).empty(), "non-root file has empty root range");
    tests.expect(map.contribution_entries().size() == 4 &&
                     map.file_index_entries().size() == 4,
                 "root-owned contributions are canonical storage");
    tests.expect(map.type_presence_entries()[0] == source_type_presence{2, 2},
                 "presence counts owners, not unique payloads");

    const auto root1_dependencies =
        map.root_dependencies(
            file_id{1});

    const auto root2_dependencies =
        map.root_dependencies(
            file_id{2});

    const auto type_dependents =
        map.dependents(
            source_dependency_ref::type(
                type));

    const auto object_dependents =
        map.dependents(
            source_dependency_ref::object(
                obj));

    tests.expect(
        root1_dependencies.size() == 1 &&
        root1_dependencies[0] ==
            source_dependency_ref::type(type) &&
        root2_dependencies.size() == 2 &&
        type_dependents.size() == 2 &&
        type_dependents[0] == file_id{1} &&
        type_dependents[1] == file_id{2} &&
        object_dependents.size() == 1 &&
        object_dependents[0] == file_id{2},
        "semantic dependencies are root-deduped and reverse-indexed");

    source_map many;
    for (std::uint32_t r = 1; r <= 300; ++r) {
        tests.expect(succeeded(many.begin_root(file_id{r})), "many roots begin");
        tests.expect(succeeded(many.add(file_id{301}, definition)),
                     "shared header discovered beyond initial file count");
        tests.expect(succeeded(many.add(file_id{301}, definition)),
                     "repeat include does not add ownership");
        tests.expect(succeeded(many.end_root()), "many roots end");
    }
    tests.expect(succeeded(many.finalize(301, identities, G)), "final file count includes discovered header");
    tests.expect(many.contribution_entries().size() == 300 &&
                     many.file_index_entries().size() == 300 &&
                     many.type_presence_entries()[0].definitions == 300,
                 "300 roots own 300 contiguous contributions");
    source_map mixed;
    tests.expect(succeeded(mixed.begin_root(file_id{1})) &&
                     succeeded(mixed.add(file_id{3}, declaration)) && succeeded(mixed.end_root()),
                 "declaration-only owner");
    tests.expect(succeeded(mixed.begin_root(file_id{2})) &&
                     succeeded(mixed.add(file_id{3}, declaration)) &&
                     succeeded(mixed.add(file_id{3}, definition)) && succeeded(mixed.end_root()),
                 "definition owner upgrades locally");
    tests.expect(succeeded(mixed.finalize(3, identities, G)) && mixed.contribution_entries().size() == 2 &&
                     mixed.root(file_id{1})[0].data == declaration &&
                     mixed.type_presence_entries()[0] == source_type_presence{2, 1},
                 "no cross-root promotion");
    source_map remaining;
    tests.expect(succeeded(remaining.begin_root(file_id{1})) &&
                     succeeded(remaining.add(file_id{3}, declaration)) &&
                     succeeded(remaining.end_root()) && succeeded(remaining.finalize(3, identities, G)),
                 "rebuild ownership without removed definition root");
    tests.expect(remaining.type_presence_entries()[0] == source_type_presence{1, 0},
                 "removing definition root preserves other declaration");
    source_map invalid;
    tests.expect(succeeded(invalid.begin_root(file_id{1})) &&
                     succeeded(invalid.add(file_id{3}, data(source_data_kind::link, 12345))) &&
                     succeeded(invalid.end_root()) && !succeeded(invalid.finalize(3, identities, G)),
                 "reject missing Graph datum");
    source_map empty;
    tests.expect(succeeded(empty.begin_root(file_id{1})) && succeeded(empty.end_root()) &&
                     !succeeded(empty.begin_root(file_id{1})),
                 "empty roots cannot be reopened");
}

} // namespace
} // namespace cw::server

int main() {
    using namespace cw::server;

    test_state tests;

    test_root_ownership_and_file_projection(tests);

    if (tests.failures != 0) {
        std::cerr << tests.failures << " source_map test(s) failed\n";

        return 1;
    }

    std::cout << "source_map tests passed\n";

    return 0;
}
