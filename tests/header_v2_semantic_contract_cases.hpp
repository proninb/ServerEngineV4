// Representative SC02/SC03 oracle fixtures, now testing production V2 directly.
// Included inside the frontend regression test namespace (no experimental code).
void test_header_v2_semantic_contract(test_state& tests) {
    const auto parse = [&](const std::vector<std::string>& texts,
        string_table& strings, identity_space& ids, graph& G, parser_v2_failure& failure) {
        std::vector<std::unique_ptr<temporary_source>> sources;
        file_context files;std::vector<file_id> roots;
        for(const auto& text:texts){sources.push_back(std::make_unique<temporary_source>("v2_semantic_contract",text));
            file_id root;const auto status=files.resolve(sources.back()->path(),file_kind::header,root);
            if(!succeeded(status))return status;roots.push_back(root);}
        preprocessor_configuration config;config.root_directory=sources.front()->path().parent_path();
        prepared_include_closure_v2 closure;prepared_include_failure_v2 prepared;
        auto status=prepare_header_include_closure_v2(files,roots,config,closure,&prepared);
        if(!succeeded(status))return status;
        preprocessor_v2_failure pp_failure;
        semantic_preprocessor_v2 input{files,closure.view(),config,strings,&pp_failure};
        for(auto root:roots){status=input.start(root);if(!succeeded(status))return status;
            header_parser_v2 parser{input,ids,G,&failure};status=parser.parse();if(!succeeded(status))return status;
            if(!input.finished())return server_status::project_configuration_invalid;}
        return server_status::success;
    };
    struct nested_value {std::string_view path;std::uint64_t value;};
    struct positive {
        std::string_view label;std::vector<std::string> roots;
        std::string_view field,referent;std::vector<nested_value> nested;
    };
    const std::vector<positive> accepted={
        {"immediate reference",{"struct A { int x; int& r; A():r(x){} };"},"r","x",{}},
        {"forward reference",{"struct A { int& r; A():r(x){} int x; };"},"r","x",{}},
        {"pending destination",{"struct A { A():r(x){} int& r; int x; };"},"r","x",{}},
        {"superseded pending destination",{"struct A { A(){r=missing;r=x;} int& r; int x; };"},"r","x",{}},
        {"stale generation",{"struct A { int& r; A():r(late){r=nullptr;} int late; };"},"r","",{}},
        {"rebind after forward",{"struct A { int known; int& r; A():r(late){r=known;} int late; };"},"r","known",{}},
        {"reversed target declaration order",{"struct A { int& r; A(){r=a;r=b;} int b; int a; };"},"r","b",{}},
        {"reference to reference",{"struct A { int x; int& s; int& r; A():s(x),r(s){} };"},"r","s",{}},
        {"same definition across roots",{"struct A { int x; int& r; A():r(x){} };","struct A { int x; int& r; A():r(x){} };"},"r","x",{}},
        {"stale generation across roots",{"struct A { int& r; A():r(late){r=nullptr;} int late; };","struct A { int& r; A():r(late){r=nullptr;} int late; };"},"r","",{}},
        {"forward nested destination",{"struct B { int x; }; struct A { A(){b.x=1;b.x=2;} B b; };"},"","",{{"b.x",2}}},
        {"multidimensional array",{"struct A { int a[2][3]; A(){a[1][2]=7;} };"},"","",{{"a[1][2]",7}}},
        {"nested array member",{"struct B { int x[2]; }; struct A { B b[2]; A(){b[1].x[0]=2;} };"},"","",{{"b[1].x[0]",2}}},
        {"merge disjoint nested defaults",{"struct A { int a[2]; A(){a[0]=1;} };","struct A { int a[2]; A(){a[1]=2;} };"},"","",{{"a[0]",1},{"a[1]",2}}}
    };
    for(const auto& c:accepted){string_table strings;identity_space ids{strings};graph G;parser_v2_failure failure;
        if(!tests.expect(succeeded(parse(c.roots,strings,ids,G,failure)),c.label))continue;
        const auto who=ids.find(ids.root(),strings.find("A"),identity_kind::type);const auto owner=G.find_type(who);
        if(!tests.expect(bool(who)&&bool(owner),"semantic contract has final A WHO/G"))continue;
        if(c.roots.front().starts_with("struct A"))tests.expect(who.slot()==2,"semantic contract deterministic WHO");
        if(!c.field.empty()){
            const auto member=G.find_member(owner,strings.find(c.field));const auto* value=G.construction(owner,member);
            const auto expected=c.referent.empty()?construction_value{}:construction_value::member_binding(G.find_member(owner,strings.find(c.referent)).value()+1);
            tests.expect(value&&*value==expected,c.label);
        }
        const auto entries=G.constructor_defaults.entries();tests.expect(entries.size()==c.nested.size(),"semantic contract nested inventory");
        for(const auto& expected:c.nested){const auto it=std::find_if(entries.begin(),entries.end(),[&](const auto& v){return v.owner==who&&strings.get(v.path)==expected.path;});
            tests.expect(it!=entries.end()&&it->value==construction_value::constant(construction_kind::unsigned_integer,expected.value),c.label);}
    }
    struct negative {
        std::string_view label;std::vector<std::string> roots;std::size_t root;
        std::string_view detail,marker;
    };
    const std::vector<negative> rejected={
        {"stale still validates incompatible source",{"struct A { int& r; A():r(late){r=nullptr;} bool late; };"},0,"Reference binding type does not match bound member type","late"},
        {"unresolved reference",{"struct A { int& r; A(){r=missing;} };"},0,"Reference binding names neither a member nor a visible Header static object","missing"},
        {"pending destination wins over earlier reference",{"struct A { int& r; A(){r=missing;unknown=0;} };"},0,"Header Parser V2 constructor target is not a field of this record","unknown"},
        {"nested destination wins over reference",{"struct A { A(){missing[0]=1;r=absent;} int& r; };"},0,"Constructor target is not a field of this record","missing"},
        {"type failure before close-time pending",{"struct A { int& r; A(){r=missing;} Missing value; };"},0,"Header Parser V2 named type is unknown","Missing"},
        {"duplicate direct value conflict",{"struct A { int x; A(){x=1;} };","struct A { int x; A(){x=2;} }; struct B { int& r; B():r(absent){} };"},1,"Header Parser V2 record definition conflicts with G","; struct B"},
        {"duplicate nested value conflict",{"struct A { int a[2]; A(){a[0]=1;} };","struct A { int a[2]; A(){a[0]=2;} }; struct B { B(){missing[0]=1;} };"},1,"Nested constructor default conflicts with Graph","; struct B"},
        {"first root error precedes second root",{"struct A { int& r; A(){r=missing;} };","struct B { B(){unknown=0;} };"},0,"Reference binding names neither a member nor a visible Header static object","missing"}
    };
    for(const auto& c:rejected){string_table strings;identity_space ids{strings};graph G;parser_v2_failure failure;
        const auto status=parse(c.roots,strings,ids,G,failure);
        const auto offset=c.roots[c.root].find(c.marker);
        const auto length=c.marker=="; struct B"?1:c.marker.size();
        const bool matches=status==server_status::project_configuration_invalid&&failure.kind==parser_v2_failure_kind::semantic&&failure.detail==c.detail&&
            failure.file.value()==c.root+1&&failure.source.offset==offset&&failure.source.length==length;
        if(!tests.expect(matches,c.label))std::cerr<<"contract detail="<<failure.detail<<",file="<<failure.file.value()<<",offset="<<failure.source.offset<<",length="<<failure.source.length<<'\n';
    }
    std::cout<<"production_v2_semantic_contract_cases="<<accepted.size()+rejected.size()<<'\n';
}
