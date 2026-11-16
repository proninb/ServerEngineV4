void test_header_templates(test_state& tests) {
    struct sample { const char* name; const char* text; bool accepted; };
    const sample samples[] = {
        {"requested", R"(
using PLC_INT = short;
using PLC_WORD = unsigned short;
template<typename T, int N, int Lower> struct IecArray { T values[N]; };
struct Device {
 IecArray<short,6,1> Flags;
 IecArray<PLC_INT,6,1> Backup;
 IecArray<PLC_INT,6,1> BackupAgain;
 IecArray<short,6,0> Other;
};)", true},
        {"generic", "template<class T, int Count> struct Buffer { T data[Count]; int tag=7; }; struct C { Buffer<unsigned short,4> data; };",true},
        {"nominal-scalars", "using I=short; using J=I; using R=float; struct C { I a=7; J b=8; R r=1.25; C(){a=9;} };",true},
        {"nominal-float-to-int", "using I=short; struct C { I a=1.25; };",false},
        {"using-typedef-conflict", "using I=short; typedef short I;",false},
        {"nested", "template<class T> struct Box { T value; }; struct C { Box<Box<short>> nested; };",true},
        {"nested-array", "template<class T,int N> struct Seq { T data[N]; }; struct C { Seq<Seq<short,2>,3> data; };",true},
        {"record-argument", "struct A { int x; }; template<class T> struct Box { T value; }; struct C { Box<A> data; };",true},
        {"pointer-argument", "template<class T> struct Box { T value; }; struct C { Box<int*> data; };",true},
        {"constructor", "template<class T> struct Box { T value=1; Box(){value=7;} }; struct C { Box<int> data; };",true},
        {"self-pointer", "template<class T> struct Box { Box* next=nullptr; T value; }; struct C { Box<int> data; };",true},
        {"explicit-self-pointer", "template<class T> struct Box { Box<T>* next=nullptr; T value; }; struct C { Box<int> data; };",true},
        {"string-member", "template<class T> struct Box { T value; const char* name=\"box\"; }; struct C { Box<int> data; };",true},
        {"void-member", "template<class T> struct Box { T value; }; struct C { Box<void> data; };",false},
        {"template-name-conflict", "template<class T> struct Box {}; struct Box {};",false},
        {"alias-name-conflict", "template<class T> struct Box {}; using Box=int;",false},
        {"parameter-shadow", "template<class T> struct Box { int T; }; struct C { Box<int> data; };",false},
        {"qualified", "namespace N { template<class T,int N> struct Box { T data[N]; }; } using S=short; struct C { N::Box<S,4> data; };",true},
        {"negative-tag", "template<class T,int N,int Lower> struct Arr { T data[N]; }; struct C { Arr<short,4,-2> data; };",true},
        {"arithmetic", "template<class T,int N,int Lower> struct Arr { T data[N-Lower+1]; }; struct C { Arr<short,(2+3)*2,1> data; };",true},
        {"precedence", "template<int N> struct Arr { int data[N*2+1]; }; struct C { Arr<2+3*4> data; };",true},
        {"unused", "template<class T,int N> struct Box { T data[N]; };",true},
        {"zero-bound", "template<int N> struct Box { int data[N]; }; struct C { Box<0> data; };",false},
        {"negative-bound", "template<int N> struct Box { int data[N]; }; struct C { Box<-1> data; };",false},
        {"missing-argument", "template<class T,int N> struct Box { T data[N]; }; struct C { Box<short> data; };",false},
        {"extra-argument", "template<class T> struct Box { T data; }; struct C { Box<short,1> data; };",false},
        {"wrong-type", "template<class T> struct Box { T data; }; struct C { Box<7> data; };",false},
        {"wrong-value", "template<int N> struct Box { int data[N]; }; struct C { Box<int> data; };",false},
        {"unknown", "struct C { Missing<short,4> data; };",false},
        {"not-template", "struct Box {}; struct C { Box<short> data; };",false},
        {"duplicate-param", "template<class T,int T> struct Box { T data; };",false},
        {"duplicate-definition", "template<class T> struct Box {}; template<class T> struct Box {};",false},
        {"int-overflow", "template<int N> struct Box { int data[N]; }; struct C { Box<2147483648> data; };",false},
        {"arithmetic-overflow", "template<int N> struct Box { int data[N]; }; struct C { Box<9223372036854775807+1> data; };",false},
        {"multiply-overflow", "template<int N> struct Box { int data[N]; }; struct C { Box<3037000500*3037000500> data; };",false},
        {"divide-zero", "template<int N> struct Box { int data[N]; }; struct C { Box<4/0> data; };",false},
        {"pack", "template<class... T> struct Box {};",false},
        {"default-param", "template<class T=int> struct Box {};",false},
        {"partial-specialization", "template<class T> struct Box<T*> {};",false},
    };
    for (const auto& sample : samples) {
        temporary_source source{sample.name, sample.text}; file_context files;
        lexical_generation lexical; file_id root;
        if (!prepare_root(tests,source.path(),files,lexical,root)) return;
        preprocessor_configuration config; config.root_directory=source.path().parent_path();
        string_table strings; identity_space ids{strings}; graph G; source_map sources; parser_failure failure;
        const auto status=parse_semantic_project(files,lexical,1,config,strings,ids,G,sources,&failure,nullptr,nullptr,true);
        if (!tests.expect(succeeded(status)==sample.accepted,std::string{"Header template: "}+sample.name)) {
            std::cerr<<failure.detail<<'\n'; continue;
        }
        if (succeeded(status) && std::string_view{sample.name}=="unused")
            tests.expect(!G.find_type(ids.find(ids.root(),strings.find("Box"),identity_kind::type)),
                "An unused template definition creates no concrete Graph type");
        if (!succeeded(status) || std::string_view{sample.name}!="requested") continue;
        const auto device=G.find_type(ids.find(ids.root(),strings.find("Device"),identity_kind::type));
        const auto* flags=G.member(device,G.find_member(device,strings.find("Flags")));
        const auto* backup=G.member(device,G.find_member(device,strings.find("Backup")));
        const auto* other=G.member(device,G.find_member(device,strings.find("Other")));
        const auto* again=G.member(device,G.find_member(device,strings.find("BackupAgain")));
        tests.expect(flags && backup && other && again && flags->type!=backup->type && flags->type!=other->type && again->type==backup->type,
            "Template canonical type preserves nominal using identity and unused Lower");
        const auto specialization=G.find_type(ids.find(ids.root(),strings.find("IecArray<short,6,1>"),identity_kind::type));
        const auto* values=G.member(specialization,G.find_member(specialization,strings.find("values")));
        derived_type_record array;
        tests.expect(values && G.derived(values->type,array) && array.kind==derived_type_kind::bounded_array &&
            array.payload==6 && array.child==G.intrinsic(intrinsic_type::signed_short),
            "Template T and N resolve into the ordinary canonical short[6] graph type");
    }
}
