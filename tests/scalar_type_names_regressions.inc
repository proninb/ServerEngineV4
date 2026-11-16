void test_scalar_type_names(test_state& tests) {
    struct sample { const char* name; intrinsic_type type; };
    const sample names[] = {
        {"uint8_t",intrinsic_type::unsigned_char},{"int8_t",intrinsic_type::signed_char},
        {"uint16_t",intrinsic_type::unsigned_short},{"int16_t",intrinsic_type::signed_short},
        {"uint32_t",intrinsic_type::unsigned_int},{"int32_t",intrinsic_type::signed_int},
        {"uint64_t",intrinsic_type::unsigned_long_long},{"int64_t",intrinsic_type::signed_long_long},
        {"uintmax_t",intrinsic_type::unsigned_long_long},{"intmax_t",intrinsic_type::signed_long_long},
        {"byte",intrinsic_type::unsigned_char},{"sbyte",intrinsic_type::signed_char},
        {"uchar",intrinsic_type::unsigned_char},{"ushort",intrinsic_type::unsigned_short},
        {"uint",intrinsic_type::unsigned_int},{"ulong",intrinsic_type::unsigned_long},
        {"longlong",intrinsic_type::signed_long_long},{"ulonglong",intrinsic_type::unsigned_long_long},
        {"u8",intrinsic_type::unsigned_char},{"i8",intrinsic_type::signed_char},
        {"uint64",intrinsic_type::unsigned_long_long},{"int64",intrinsic_type::signed_long_long},
    };
    for (const auto& sample:names) for (const bool v2 : {false,true}) {
        const auto text=std::string{"struct C { "}+sample.name+" value{}; "+sample.name+" array[2]; };";
        temporary_source source{sample.name,text}; file_context files; lexical_generation lexical; file_id root;
        if (!prepare_root(tests,source.path(),files,lexical,root)) return;
        preprocessor_configuration config; config.root_directory=source.path().parent_path();
        string_table strings;identity_space ids{strings};graph G;source_map sources;parser_failure failure;
        const auto status=parse_semantic_project(files,lexical,1,config,strings,ids,G,sources,&failure,nullptr,nullptr,v2);
        if (!tests.expect(succeeded(status),std::string{v2?"V2 scalar name: ":"Legacy scalar name: "}+sample.name)) {
            std::cerr<<failure.detail<<'\n';continue;
        }
        const auto c=G.find_type(ids.find(ids.root(),strings.find("C"),identity_kind::type));
        const auto* value=G.member(c,G.find_member(c,strings.find("value")));
        tests.expect(value && value->type==G.intrinsic(sample.type),"Scalar spelling resolves to its exact intrinsic storage type");
    }
    const char* cases[]={
        "struct C { std::uint8_t a=255; std::int64_t b=7; std::uint32_t c[2]; };",
        "struct uint8_t { int x; }; struct C { uint8_t value; };",
        "namespace N { namespace std { struct uint8_t { int x; }; } struct C { std::uint8_t value; }; }",
        "using byte=short; struct C { byte value=7; };",
        "template<class uint8_t> struct Box { uint8_t value; }; struct C { Box<short> field; };",
    };
    for (const auto text:cases) {
        temporary_source source{"scalar-name-context",text};file_context files;lexical_generation lexical;file_id root;
        if (!prepare_root(tests,source.path(),files,lexical,root)) return;
        preprocessor_configuration config;string_table strings;identity_space ids{strings};graph G;source_map sources;parser_failure failure;
        const auto status=parse_semantic_project(files,lexical,1,config,strings,ids,G,sources,&failure,nullptr,nullptr,true);
        tests.expect(succeeded(status),"Qualified and explicitly shadowed scalar conveniences parse in V2");
    }
}
