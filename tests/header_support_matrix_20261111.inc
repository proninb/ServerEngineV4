// Observational audit mode; rejected subset features are observations, not PASS.
int run_header_support_matrix(const std::filesystem::path& output) {
    if (!std::filesystem::create_directory(output)) return 2;
    struct fixture { const char* id; const char* source; };
    const fixture fixtures[] = {
        {"global-static", "static int VAR = 7;"},
        {"global-static-alias", "typedef int TYPE; static TYPE VAR = 7;"},
        {"namespace-static", "namespace N { static int VAR = 7; }"},
        {"global-nonstatic", "int VAR = 7;"},
        {"member-equals", "typedef int TYPE; struct A { TYPE AA = 7; };"},
        {"member-empty-braces", "typedef int TYPE; struct A { TYPE BB{}; };"},
        {"member-value-braces", "typedef int TYPE; struct A { TYPE BB{7}; };"},
        {"member-missing-semicolon", "typedef int TYPE; struct A { TYPE BB{} };"},
        {"empty-record", "struct B {};"},
        {"multiple-bases", "struct A { int AA=7; }; struct B {}; struct C : A, B { int CC{}; };"},
        {"base-access", "struct A {}; struct B {}; struct C : public A, private B {};"},
        {"method-declaration", "struct C { int FUNC(); };"},
        {"method-parameters", "struct C { int FUNC(int x, const char* text) const noexcept; };"},
        {"method-unknown-parameter", "struct C { int FUNC(UnknownType x); };"},
        {"method-body", "struct C { int FUNC() { return 7; } };"},
        {"virtual-method", "struct A { virtual int FUNC()=0; virtual ~A(); }; struct C : A { int FUNC() override; };"},
        {"pointer-nullptr", "typedef int TYPE; struct C { TYPE *X = nullptr; };"},
        {"pointer-zero", "struct C { int *X = 0; };"},
        {"scalar-nullptr-invalid", "struct C { int X = nullptr; };"},
        {"mutable-string-pointer", "struct C { char* AAA = \"aaa\"; };"},
        {"const-string-pointer", "struct C { const char* AAA = \"aaa\"; };"},
        {"const-char-pointer-declaration", "struct C { const char* AAA; };"},
        {"char-array-string", "struct C { char AAA[4] = \"aaa\"; };"},
        {"array-literal", "typedef int TYPE; struct C { TYPE ARR[4]; };"},
        {"array-macro", "#define N 4\ntypedef int TYPE; struct C { TYPE ARR[N]; };"},
        {"array-enum-bound", "enum Sizes { N=4 }; struct C { int ARR[N]; };"},
        {"array-constexpr-bound", "constexpr int N=4; struct C { int ARR[N]; };"},
        {"array-expression-bound", "struct C { int ARR[2+2]; };"},
        {"array-zero-invalid", "struct C { int ARR[0]; };"},
        {"multidimensional-array", "struct C { int ARR[2][3]; };"},
        {"reference-array-declaration", "typedef int TYPE; struct C { TYPE (&ARR_REF)[4]; };"},
        {"reference-array-default", "typedef int TYPE; struct C { TYPE ARR[4]; TYPE (&ARR_REF)[4] = ARR; };"},
        {"reference-array-constructor", "typedef int TYPE; struct C { TYPE ARR[4]; TYPE (&ARR_REF)[4]; C() : ARR_REF(ARR) {} };"},
        {"reference-array-type-bound-invalid", "typedef int TYPE; struct C { TYPE (&ARR_REF)[TYPE]; };"},
        {"array-of-references-invalid", "struct C { int &ARR[4]; };"},
        {"pointer-to-array", "struct C { int (*ARR)[4] = nullptr; };"},
        {"array-of-pointers", "struct C { int *ARR[4]; };"},
        {"static-member", "struct C { static int VAR; };"},
        {"static-method", "struct C { static int FUNC(); };"},
        {"static-constexpr-member", "struct C { static constexpr int N=4; int ARR[N]; };"},
        {"multiple-declarators", "struct C { int AA=1, BB=2; };"},
        {"arithmetic-initializer", "struct C { int AA=3+4; };"},
        {"using-alias", "using TYPE=int; struct C { TYPE AA=7; };"},
        {"record-typedef", "struct A {}; typedef A TYPE; struct C { TYPE AA; };"},
        {"function-pointer", "struct C { int (*FUNC)(int); };"},
        {"nested-record", "struct C { struct A { int X; }; A AA; };"},
        {"enum-initializer", "enum Values { VAL=7 }; struct C { int AA=VAL; };"},
        {"combined-supported", "typedef int TYPE; struct A { TYPE AA=7; TYPE BB{}; }; struct B {}; struct C : A,B { int FUNC(); char* AAA=nullptr; TYPE ARR[4]; TYPE (&ARR_REF)[4]=ARR; TYPE *X=nullptr; };"},
        {"combined-user-normalized", "typedef int TYPE; static TYPE VAR=7; struct A { TYPE AA=7; TYPE BB{}; }; struct B {}; struct C : A,B { int FUNC(); char* AAA=\"aaa\"; TYPE ARR[4]; TYPE (&ARR_REF)[4]; TYPE *X=nullptr; };"},
    };
    std::ofstream report{output / "results.tsv"};
    report << "case\tbackend\tok\tstatus\tkind\tdetail\tobjects\tsemantic_projection\n";
    test_state tests;
    for (const auto& item : fixtures) {
        const auto path = output / (std::string{item.id} + ".hpp");
        { std::ofstream file{path}; file << item.source << '\n'; }
        for (bool v2 : {false, true}) {
            file_context files; lexical_generation lexical; file_id root;
            if (!prepare_root(tests, path, files, lexical, root)) return 3;
            preprocessor_configuration config; config.root_directory = output;
            string_table strings; identity_space ids{strings}; graph G; source_map sources;
            parser_failure failure; semantic_parse_telemetry telemetry;
            const auto status = parse_semantic_project(files, lexical, 1, config,
                strings, ids, G, sources, &failure, nullptr, &telemetry, v2);
            report << item.id << '\t' << (v2 ? "PUBLISH_V2" : "LEGACY") << '\t'
                << succeeded(status) << '\t' << static_cast<unsigned>(status) << '\t'
                << static_cast<unsigned>(failure.kind) << '\t' << failure.detail << '\t'
                << G.object_count() << '\t';
            if (succeeded(status)) {
                for (const auto& entry : differential_projection(G, ids, strings)) report << entry << '|';
                for (std::size_t i=0; i<G.object_count(); ++i) {
                    construction_value initial;
                    if (G.construction(G.object_at(i), initial)) report << "object-init:"
                        << static_cast<unsigned>(initial.kind) << ':' << initial.bits() << '|';
                }
            }
            report << '\n';
        }
    }
    if (!report.good()) return 4;
    std::cout << "Header support observations written to " << output << '\n';
    return tests.failures ? 1 : 0;
}
