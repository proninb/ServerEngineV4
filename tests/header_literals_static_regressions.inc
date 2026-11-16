void test_header_literals_static(test_state& tests) {
    struct sample { const char* name; const char* text; bool accepted; };
    const sample samples[] = {
        {"pointer", "struct C { const char* P=\"aaa\"; };",true},
        {"array", "struct C { char B[4]=\"aaa\"; };",true},
        {"array-padding", "struct C { char B[8]=\"aaa\"; };",true},
        {"array-short", "struct C { char B[3]=\"aaa\"; };",false},
        {"empty", "struct C { const char* P=\"\"; char B[1]=\"\"; };",true},
        {"escapes", "struct C { const char* P=\"a\\n\" \"b\"; char B[8]=\"a\\0b\"; };",true},
        {"numeric-escapes", "struct C { char B[4]=\"\\101\\x42\"; };",true},
        {"byte-escape", "struct C { const char* P=\"\\xff\"; char B[2]=\"\\xff\"; };",true},
        {"bad-escape", "struct C { char B[8]=\"\\q\"; };",false},
        {"overflow-escape", "struct C { char B[8]=\"\\x100\"; };",false},
        {"mutable-pointer", "struct C { char* P=\"aaa\"; };",false},
        {"wrong-pointer", "struct C { const int* P=\"aaa\"; };",false},
        {"wide", "struct C { const char* P=L\"aaa\"; };",false},
        {"braced", "struct C { const char* P{\"aaa\"}; char B[4]{\"aaa\"}; };",true},
        {"ctor-before", "struct C { C(){ B[0]=98; } char B[4]=\"aaa\"; };",true},
        {"ctor-after", "struct C { char B[4]=\"aaa\"; C(){ B[0]=98; } };",true},
        {"static", "typedef int TYPE; static TYPE VAR=7; struct C { int& R=VAR; };",true},
        {"static-namespace", "namespace N { static int VAR=7; struct C { int& R; C():R(VAR){} }; }",true},
        {"static-forward-member-wins", "static int V=7; struct C { int& R=V; int V=9; };",true},
        {"static-conflict", "static int V=7; static int V=9;",false},
        {"static-incomplete", "struct A; static A V;",false},
        {"static-member-still-unsupported", "struct A { static int V; };",false},
    };
    for (const auto& sample : samples) {
        temporary_source source{sample.name,sample.text}; file_context files;
        lexical_generation lexical; file_id root;
        if (!prepare_root(tests,source.path(),files,lexical,root)) return;
        preprocessor_configuration config; config.root_directory=source.path().parent_path();
        string_table strings; identity_space ids{strings}; graph G; source_map sources;
        parser_failure failure;
        const auto status=parse_semantic_project(files,lexical,1,config,strings,ids,G,sources,&failure,nullptr,nullptr,true);
        if (!tests.expect(succeeded(status)==sample.accepted,std::string{"Header literals/static: "}+sample.name)) {
            std::cerr<<failure.detail<<'\n'; continue;
        }
        if (!succeeded(status)) continue;
        const auto type=G.find_type(ids.find(ids.root(),strings.find("C"),identity_kind::type));
        if (std::string_view{sample.name}=="array") {
            const auto values=G.constructor_defaults.entries();
            tests.expect(values.size()==4 && values[0].value.bits()==97 && values[3].value.bits()==0,
                "Header array has three characters and explicit NUL defaults");
        }
        if (std::string_view{sample.name}.starts_with("ctor-")) {
            bool found=false;
            for (const auto& value:G.constructor_defaults.entries())
                if (strings.get(value.path)=="B[0]") found=value.value.bits()==98;
            tests.expect(found,"Constructor overrides Header string defaults irrespective of declaration order");
        }
        if (std::string_view{sample.name}=="static" || std::string_view{sample.name}=="static-forward-member-wins") {
            const auto member=G.find_member(type,strings.find("R"));
            const auto* initial=G.construction(type,member);
            tests.expect(initial && initial->kind==(std::string_view{sample.name}=="static"?
                construction_kind::object_binding:construction_kind::member_binding),
                "Header static lookup respects complete class member scope");
        }
    }
    // Same static spelling remains independent per semantic Header root.
    temporary_source a{"static_root_a","static int V=7; struct A { int& R=V; };"};
    temporary_source b{"static_root_b","static int V=9; struct B { int& R=V; };"};
    file_context files; lexical_generation lexical; file_id aid,bid;
    if (!tests.expect(succeeded(files.resolve(a.path(),file_kind::header,aid)) &&
        succeeded(files.resolve(b.path(),file_kind::header,bid)),"Header static two roots resolve")) return;
    const std::array roots{aid,bid}; if (!prepare_all_roots(tests,files,lexical,roots)) return;
    preprocessor_configuration config; string_table strings; identity_space ids{strings}; graph G; source_map sources; parser_failure failure;
    const auto status=parse_semantic_project(files,lexical,2,config,strings,ids,G,sources,&failure,nullptr,nullptr,true);
    tests.expect(succeeded(status) && G.object_count()==2,"Header static objects retain semantic-root ownership");
}
