#include "v3_23_direct_index.hpp"
#ifdef CW_V327_DERIVED_CACHE
#include "v3_27_derived_cache.hpp"
#endif
#ifdef CW_V326_FAST_TYPE_RESOLVE
#include "v3_26_fast_type.hpp"
#ifdef CW_V331_SCOPE_CHAIN
#include "v3_31_scope_chain.hpp"
#endif
#endif
// V3-31: sampled named resolution and record-local scope-chain optimization.
// V3-21: experimental *native* Graph producer with source-order WHO/WHERE. Header V2 is not passed into
// any producer function. The only shared input is the preprocessed token stream.
// Fail closed for unsupported syntax; never infer an expected value from V2.
namespace v319 {

// V3-22 optional coarse-grained profiling. Null pointer in paired A/B runs:
// the production-equivalent timing path makes no clock calls inside emit().
struct phase_profile final {
    double type_base_ms=0, type_shape_ms=0, type_derived_ms=0;
    std::uint64_t type_fast_hits=0, type_fast_misses=0, type_fallback=0, type_fast_builtin=0;
#ifdef CW_V327_DERIVED_CACHE
    v327_derived::counters derived{};
#endif
    double syntax_ms=0, identity_ms=0, index_ms=0, fields_ms=0,
        constructors_ms=0, inheritance_ms=0, graph_define_ms=0, nested_ms=0;
    std::uint64_t records=0;
#ifdef CW_V331_SAMPLED_TYPE_PROFILE
    // Sampled exclusive field-type work only. These are NOT whole-project times.
    std::uint64_t sample_stride=32, eligible_records=0, sampled_records=0,
        sampled_field_types=0, sampled_builtin=0, sampled_named_attempts=0,
        sampled_fallback_attempts=0;
    double sampled_builtin_ms=0, sampled_named_ms=0,
        sampled_fallback_ms=0, sampled_prepare_ms=0;
    v327_derived::counters sampled_field_derived{};
#ifdef CW_V331_SCOPE_CHAIN
    v331_type::named_profile sampled_named_details{};
    double sampled_scope_prepare_ms=0;
    std::uint64_t sampled_scope_depth_total=0;
#endif
#endif
};
struct producer_state final {
    identity_space ids;
    graph G;
    phase_profile* profile=nullptr;
    std::uint64_t records=0, aliases=0, fields=0, constructors=0, direct=0, nested=0;
    std::uint64_t method_returns=0, method_derived_created=0, method_no_return=0;
#ifdef CW_V331_SAMPLED_TYPE_PROFILE
    bool sampled_record=false;
#endif
    std::uint64_t unsupported=0;
    std::vector<diagnostic_row> failures;
    std::unordered_map<std::uint32_t,token_origin> first_type_source;
#ifdef CW_V327_DERIVED_CACHE
    v327_derived::cache derived_cache;
#endif
#ifdef CW_V323_DIRECT_INDEX
    v323_index::direct_id_index names; // one reusable index for all records
#ifdef CW_V326_FAST_TYPE_RESOLVE
    v326_type::base_type_cache named_types;
#ifdef CW_V331_SCOPE_CHAIN
    v331_type::scope_chain lexical_scopes;
#endif
#endif
#endif
    explicit producer_state(string_table& strings):ids(strings){}
};

struct parsed_root final {
    const root_tokens* root=nullptr;
    syntax_result_03 syntax;
    std::vector<identity_ref> owners;
    std::vector<type_handle> handles;
};

void reject(producer_state& state,const root_tokens& root,std::uint32_t at,
            std::string reason,std::string detail="") {
    ++state.unsupported;
    const auto o=at<root.origins.size()?root.origins[at]:token_origin{};
    state.failures.push_back({std::move(reason),"","",std::move(detail),
        o.file,o.source_offset,o.source_length});
}

[[nodiscard]] graph_record_kind kind_of(token_kind t) noexcept {
    return t==token_kind::kw_class?graph_record_kind::class_type:
        t==token_kind::kw_union?graph_record_kind::union_type:
        graph_record_kind::struct_type;
}
[[nodiscard]] graph_member_access initial_access(token_kind t) noexcept {
    return t==token_kind::kw_class?graph_member_access::private_access:
        graph_member_access::public_access;
}
[[nodiscard]] graph_member_access access_of(token_kind t) noexcept {
    return t==token_kind::kw_private?graph_member_access::private_access:
        t==token_kind::kw_protected?graph_member_access::protected_access:
        graph_member_access::public_access;
}
[[nodiscard]] v317::number decoded_number(const root_tokens& r,std::size_t i) noexcept {
    if(i>=r.decoded_numbers.size())return {};
    const auto n=r.decoded_numbers[i];
    return {n.bits,n.kind==prepared_number_kind_v2::unsigned_integer?
        v317::number_kind::unsigned_integer:n.kind==prepared_number_kind_v2::real?
        v317::number_kind::real:v317::number_kind::none};
}
[[nodiscard]] std::uint32_t symbol(const root_tokens& r,std::size_t i) noexcept {
    return i<r.identifiers.size()?r.identifiers[i].value():0u;
}
[[nodiscard]] construction_value constant(const v317::operation& op) noexcept {
    if(op.kind==v317::operand_kind::signed_integer)
        return construction_value::constant(construction_kind::signed_integer,op.bits);
    if(op.kind==v317::operand_kind::unsigned_integer)
        return construction_value::constant(construction_kind::unsigned_integer,op.bits);
    if(op.kind==v317::operand_kind::real)
        return construction_value::constant(construction_kind::real,op.bits);
    return {};
}
// Pure V3 lexical shape extraction, NOT a verification against a V2 field.
[[nodiscard]] bool derive_shape(const v316::source_view& src,
    const syntax_declaration_03& decl,const syntax_declarator_08& d,
    bool grouped,v316::type_spec& expected) {
    const auto k=src.tokens;
    if(d.name==0xffffffffu || !(decl.begin<d.type_end && d.type_end<=d.name &&
       d.name<decl.end && decl.end<=k.size()))return false;
    std::uint32_t p=d.type_end;
    derived_type_kind outside=derived_type_kind::pointer;
    if(grouped){
        if(p+2!=d.name || k[p]!=token_kind::l_paren)return false;
        outside=k[p+1]==token_kind::star?derived_type_kind::pointer:
            k[p+1]==token_kind::ampersand?derived_type_kind::lvalue_reference:
            derived_type_kind::rvalue_reference;
        p=d.name;
    }
    while(p<d.name){
        const auto op=k[p++];
        if(op!=token_kind::star && op!=token_kind::ampersand &&
           op!=token_kind::logical_and)return false;
        const auto derived=op==token_kind::star?derived_type_kind::pointer:
            op==token_kind::ampersand?derived_type_kind::lvalue_reference:
            derived_type_kind::rvalue_reference;
        if(!expected.push(derived))return false;
        if(op==token_kind::star){
            bool c=false,v=false;
            while(p<d.name && (k[p]==token_kind::kw_const||k[p]==token_kind::kw_volatile)){
                c|=k[p]==token_kind::kw_const;
                v|=k[p]==token_kind::kw_volatile;++p;
            }
            if(c&&!expected.push(derived_type_kind::const_qualified))return false;
            if(v&&!expected.push(derived_type_kind::volatile_qualified))return false;
        }
    }
    p=d.name+1;
    if(grouped){if(p>=decl.end||k[p++]!=token_kind::r_paren)return false;}
    std::array<std::uint64_t,32> bounds{};
    std::size_t n=0;
    const auto stop=std::min(d.initializer_begin,decl.end);
    while(p<stop && k[p]==token_kind::l_bracket){
        if(p+2>=stop || k[p+1]!=token_kind::pp_number ||
            k[p+2]!=token_kind::r_bracket || !src.positive_numbers[p+1] ||
            n>=bounds.size())return false;
        bounds[n++]=src.positive_numbers[p+1];p+=3;
    }
    if(p<stop && k[p]!=token_kind::semicolon && k[p]!=token_kind::comma)return false;
    if(grouped&&!n)return false;
    while(n>0)if(!expected.push(derived_type_kind::bounded_array,bounds[--n]))return false;
    if(grouped&&!expected.push(outside))return false;
    return true;
}
[[nodiscard]] type_ref produce_type(const v316::source_view& src,
    const syntax_declaration_03& decl,const syntax_declarator_08& d,
    bool grouped,identity_ref owner,producer_state& out) {
    v316::type_spec spec;
#ifdef CW_V331_SAMPLED_TYPE_PROFILE
    // One sampled record out of N; never call a clock for unsampled fields.
    const bool sampled=out.profile!=nullptr && out.sampled_record;
    if(sampled)++out.profile->sampled_field_types;
#else
    const bool sampled=out.profile!=nullptr;
#endif
    const auto base_start=sampled?clock_type::now():clock_type::time_point{};
    bool resolved=false;
#ifdef CW_V326_FAST_TYPE_RESOLVE
#ifdef CW_V331_SAMPLED_TYPE_PROFILE
    const auto builtin_start=sampled?clock_type::now():clock_type::time_point{};
#endif
    resolved=v326_type::try_resolve_single_intrinsic(src,decl.begin,d.type_end,spec);
#ifdef CW_V331_SAMPLED_TYPE_PROFILE
    if(sampled){
        out.profile->sampled_builtin_ms+=elapsed_ms(builtin_start);
        if(resolved)++out.profile->sampled_builtin;
    }
#else
    if(resolved && out.profile)++out.profile->type_fast_builtin;
#endif
    if(!resolved){
#ifdef CW_V331_SAMPLED_TYPE_PROFILE
        const auto named_start=sampled?clock_type::now():clock_type::time_point{};
        if(sampled)++out.profile->sampled_named_attempts;
#endif
#ifdef CW_V331_SCOPE_CHAIN
        resolved=v331_type::try_resolve_simple(src,decl.begin,d.type_end,owner,
            out.ids,out.G,out.named_types,out.lexical_scopes,spec,
            sampled?&out.profile->type_fast_hits:nullptr,
            sampled?&out.profile->type_fast_misses:nullptr,
            sampled?&out.profile->sampled_named_details:nullptr);
#else
        resolved=v326_type::try_resolve_simple(src,decl.begin,d.type_end,owner,
            out.ids,out.G,out.named_types,spec,
            sampled?&out.profile->type_fast_hits:nullptr,
            sampled?&out.profile->type_fast_misses:nullptr);
#endif
#ifdef CW_V331_SAMPLED_TYPE_PROFILE
        if(sampled)out.profile->sampled_named_ms+=elapsed_ms(named_start);
#endif
    }
#ifndef CW_V331_SAMPLED_TYPE_PROFILE
    if(!resolved && out.profile)++out.profile->type_fallback;
#else
    if(!resolved && sampled)++out.profile->type_fallback;
#endif
#endif
    if(!resolved){
#ifdef CW_V331_SAMPLED_TYPE_PROFILE
        const auto fallback_start=sampled?clock_type::now():clock_type::time_point{};
        if(sampled)++out.profile->sampled_fallback_attempts;
#endif
        const auto builtin=unambiguous_builtin_type(src.tokens,decl.begin,d.type_end);
        resolved=v316::resolve_base(src,decl.begin,d.type_end,owner,out.ids,out.G,builtin,spec);
#ifdef CW_V331_SAMPLED_TYPE_PROFILE
        if(sampled)out.profile->sampled_fallback_ms+=elapsed_ms(fallback_start);
#endif
    }
    if(sampled)out.profile->type_base_ms+=elapsed_ms(base_start);
    if(!resolved)return {};
    const auto shape_start=sampled?clock_type::now():clock_type::time_point{};
    const bool shape_ok=derive_shape(src,decl,d,grouped,spec);
    if(sampled)out.profile->type_shape_ms+=elapsed_ms(shape_start);
    if(!shape_ok)return {};
#ifdef CW_V331_SAMPLED_TYPE_PROFILE
    const auto prepare_start=sampled?clock_type::now():clock_type::time_point{};
#endif
    type_ref result{};
    if(spec.base_kind==type_ref_kind::intrinsic)
        result=out.G.intrinsic(static_cast<intrinsic_type>(spec.base_payload));
    else if(spec.base_kind==type_ref_kind::named)
        result=out.G.named(identity_ref::from_raw(
            (static_cast<std::uint32_t>(identity_kind::type)<<30)|spec.base_payload));
#ifdef CW_V331_SAMPLED_TYPE_PROFILE
    if(sampled)out.profile->sampled_prepare_ms+=elapsed_ms(prepare_start);
#endif
    if(!result)return {};
    const auto derived_start=sampled?clock_type::now():clock_type::time_point{};
    for(std::size_t i=0;i<spec.length;++i){
        type_ref next;
#ifdef CW_V327_DERIVED_CACHE
#ifdef CW_V331_SAMPLED_TYPE_PROFILE
        const auto status=out.derived_cache.intern(out.G,result,spec.layers[i].kind,
            spec.layers[i].payload,next,sampled?&out.profile->sampled_field_derived:nullptr);
#else
        const auto status=out.derived_cache.intern(out.G,result,spec.layers[i].kind,
            spec.layers[i].payload,next,out.profile?&out.profile->derived:nullptr);
#endif
#else
        const auto status=out.G.derive_resolved(result,spec.layers[i].kind,
            spec.layers[i].payload,next);
#endif
        if(!succeeded(status)||!next)return {};
        result=next;
    }
    if(sampled)out.profile->type_derived_ms+=elapsed_ms(derived_start);
    return result;
}

// V3-21: return-type side effects of a method MUST be emitted exactly where
// Header V2 calls parse_type(). Method parameter tokens are intentionally opaque.
// This does not create a member_record or store a method ABI.
// Both the identity lookup and all derive_resolved() calls use ONLY the V3 graph.
enum class method_return_status : std::uint8_t { emitted, no_return, invalid };
[[nodiscard]] method_return_status emit_method_return(
    producer_state& out, const root_tokens& root,
    const syntax_declaration_03& decl, identity_ref owner,
    std::string_view owner_name, const string_table& strings) {
    const auto t=std::span<const token_kind>{root.effective};
    if(decl.begin>=decl.end || decl.end>t.size())return method_return_status::invalid;
    if(constructor_shaped(root,decl,owner_name,strings))return method_return_status::no_return;
    std::uint32_t begin=decl.begin;
    // Header V2 consumes virtual/explicit before parsing the method's type.
    while(begin<decl.end &&
          (t[begin]==token_kind::kw_virtual || t[begin]==token_kind::kw_explicit))++begin;
    if(begin>=decl.end)return method_return_status::invalid;
    if(t[begin]==token_kind::tilde)return method_return_status::no_return; // destructor
    // Inspect only the declarator head, not the parameter list or body.
    // An ordinary method body may itself contain an operator expression.
    std::uint32_t first_params=decl.end;
    for(std::uint32_t i=begin;i<decl.end;++i)
        if(t[i]==token_kind::l_paren){first_params=i;break;}
    std::uint32_t operator_at=decl.end;
    for(std::uint32_t i=begin;i<first_params;++i)
        if(t[i]==token_kind::kw_operator){operator_at=i;break;}
    std::uint32_t type_first=begin,type_last=decl.end;
    if(operator_at<decl.end){
        if(operator_at==begin){
            // Conversion operator: operator char*() derives pointer(char).
            type_first=operator_at+1;
            for(std::uint32_t i=type_first;i<decl.end;++i)
                if(t[i]==token_kind::l_paren){type_last=i;break;}
        } else {
            type_last=operator_at;
            // Header V2 allows a qualified operator name: bool T::operator==().
            // The T:: prefix is a *name qualifier*, not part of the return type.
            if(type_last>=begin+2 && t[type_last-1]==token_kind::scope &&
               t[type_last-2]==token_kind::identifier){
                if(strings.get(root.identifiers[type_last-2])!=owner_name)
                    return method_return_status::invalid;
                type_last-=2;
            }
        }
    } else {
        // Ordinary method: parse the return type before its identifier name.
        // Constructor and destructor declarations were already excluded.
        std::uint32_t params=decl.end;
        for(std::uint32_t i=begin;i<decl.end;++i)
            if(t[i]==token_kind::l_paren){params=i;break;}
        if(params==decl.end || params==begin ||
           t[params-1]!=token_kind::identifier)
            return method_return_status::invalid;
        type_last=params-1;
    }
    if(type_first>=type_last)return method_return_status::invalid;
    // A type has one base and a linear sequence of pointer/reference wrappers.
    // Prefix cv belongs to the base; post-* cv belongs to that pointer level.
    std::uint32_t base_last=type_first;
    while(base_last<type_last && t[base_last]!=token_kind::star &&
          t[base_last]!=token_kind::ampersand &&
          t[base_last]!=token_kind::logical_and)++base_last;
    if(base_last==type_first)return method_return_status::invalid;
    const v316::source_view src{root.effective,root.identifiers,root.positive_numbers,
        &strings,&strings};
    v316::type_spec spec;
    const auto builtin=unambiguous_builtin_type(t,type_first,base_last);
    if(!v316::resolve_base(src,type_first,base_last,owner,out.ids,out.G,builtin,spec))
        return method_return_status::invalid;
    type_ref value;
    if(spec.base_kind==type_ref_kind::intrinsic)
        value=out.G.intrinsic(static_cast<intrinsic_type>(spec.base_payload));
    else if(spec.base_kind==type_ref_kind::named)
        value=out.G.named(identity_ref::from_raw(
            (static_cast<std::uint32_t>(identity_kind::type)<<30)|spec.base_payload));
    if(!value)return method_return_status::invalid;
    auto derive=[&](derived_type_kind kind,std::uint64_t payload=0)->bool {
        const auto before=out.G.derived_type_count();
        type_ref next;
#ifdef CW_V327_DERIVED_CACHE
        const auto status=out.derived_cache.intern(out.G,value,kind,payload,next,
            out.profile?&out.profile->derived:nullptr);
#else
        const auto status=out.G.derive_resolved(value,kind,payload,next);
#endif
        if(!succeeded(status)||!next)return false;
        if(out.G.derived_type_count()>before)
            out.method_derived_created+=out.G.derived_type_count()-before;
        value=next;
        return true;
    };
    for(std::uint32_t i=0;i<spec.length;++i)
        if(!derive(spec.layers[i].kind,spec.layers[i].payload))
            return method_return_status::invalid;
    for(std::uint32_t i=base_last;i<type_last;){
        const auto op=t[i++];
        const auto kind=op==token_kind::star?derived_type_kind::pointer:
            op==token_kind::ampersand?derived_type_kind::lvalue_reference:
            op==token_kind::logical_and?derived_type_kind::rvalue_reference:
            derived_type_kind::unbounded_array;
        if(kind==derived_type_kind::unbounded_array || !derive(kind))
            return method_return_status::invalid;
        if(op==token_kind::star){
            bool const_seen=false,volatile_seen=false;
            while(i<type_last &&
                  (t[i]==token_kind::kw_const || t[i]==token_kind::kw_volatile)){
                const_seen|=t[i]==token_kind::kw_const;
                volatile_seen|=t[i]==token_kind::kw_volatile;
                ++i;
            }
            if((const_seen && !derive(derived_type_kind::const_qualified)) ||
               (volatile_seen && !derive(derived_type_kind::volatile_qualified)))
                return method_return_status::invalid;
        }
    }
    ++out.method_returns;
    return method_return_status::emitted;
}

// Namespace spans in V3 root: reconstruct lexical scopes WITHOUT consulting V2.
struct ns_span {std::uint32_t open{},close{}; string_id name{};};
[[nodiscard]] std::vector<ns_span> namespaces(const root_tokens& root){
    std::vector<std::uint32_t> stack;
    std::vector<ns_span> result;
    for(std::uint32_t i=0;i<root.effective.size();++i){
        if(root.effective[i]==token_kind::l_brace)stack.push_back(i);
        else if(root.effective[i]==token_kind::r_brace){
            if(stack.empty())continue;
            const auto open=stack.back();stack.pop_back();
            if(open>=2 && root.effective[open-2]==token_kind::kw_namespace &&
              root.effective[open-1]==token_kind::identifier && root.identifiers[open-1])
                result.push_back({open,i,root.identifiers[open-1]});
        }
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){
        return a.open<b.open;});
    return result;
}
[[nodiscard]] identity_ref lexical_scope(const root_tokens&,std::uint32_t position,
    const std::vector<ns_span>& spans,producer_state& out){
    auto scope=out.ids.root();
    // Outer namespaces resolve before nested. Supports repeated namespace blocks.
    for(const auto& ns:spans){
        if(ns.open>=position)break;
        if(ns.close<=position)continue;
        identity_ref next;
        if(!succeeded(out.ids.resolve(scope,ns.name,identity_kind::namespace_scope,next)))return {};
        scope=next;
    }
    return scope;
}

[[nodiscard]] bool emit(producer_state& out,world& input) {
    const auto profile_now=[&](){return clock_type::now();};
    const auto syntax_start=out.profile?profile_now():clock_type::time_point{};
    std::vector<parsed_root> roots;
    roots.reserve(input.streams.size());
    for(const auto& root:input.streams){
        parsed_root parsed;
        parsed.root=&root;
        if(!header_syntax_v3_06::scan(root.effective,parsed.syntax)){
            reject(out,root,0,"syntax_rejected");return false;
        }
        parsed.owners.resize(parsed.syntax.records.size());
        parsed.handles.resize(parsed.syntax.records.size());
        roots.push_back(std::move(parsed));
    }
    if(out.profile)out.profile->syntax_ms+=elapsed_ms(syntax_start);
    const auto identity_start=out.profile?profile_now():clock_type::time_point{};
    // V3-20: Replay identity and type declarations in lexical event order.
    // Header V2 creates namespaces, records and typedef aliases as it walks
    // the effective stream; emitting every record before aliases shifts WHO
    // and type WHERE even when the semantic types compare equal.
    enum class event_kind : std::uint8_t { name_space, record, intrinsic_alias };
    struct declaration_event final {
        std::uint32_t position{};
        std::uint32_t index{};
        event_kind kind{};
    };
    for(auto& parsed:roots){
        const auto& root=*parsed.root;
        const auto ns=namespaces(root);
        std::vector<declaration_event> events;
        events.reserve(ns.size()+parsed.syntax.records.size()+parsed.syntax.declarations.size());
        for(std::uint32_t i=0;i<ns.size();++i){
            if(ns[i].open<2){reject(out,root,ns[i].open,"namespace_invalid_span");continue;}
            events.push_back({ns[i].open-2,i,event_kind::name_space});
        }
        for(std::uint32_t i=0;i<parsed.syntax.records.size();++i)
            events.push_back({parsed.syntax.records[i].begin,i,event_kind::record});
        for(std::uint32_t i=0;i<parsed.syntax.declarations.size();++i)
            if(parsed.syntax.declarations[i].kind==syntax_decl_03::typedef_decl)
                events.push_back({parsed.syntax.declarations[i].begin,i,event_kind::intrinsic_alias});
        std::sort(events.begin(),events.end(),[](const auto& a,const auto& b){
            if(a.position!=b.position)return a.position<b.position;
            return static_cast<unsigned>(a.kind)<static_cast<unsigned>(b.kind);
        });
        const v316::source_view src{root.effective,root.identifiers,root.positive_numbers,
            &input.strings,&input.strings};
        for(const auto& event:events){
            if(event.kind==event_kind::name_space){
                const auto& space=ns[event.index];
                // Also register empty namespaces; V2 resolves them on entry.
                if(!lexical_scope(root,space.open+1,ns,out))
                    reject(out,root,event.position,"namespace_identity_failed");
                continue;
            }
            if(event.kind==event_kind::record){
                const auto i=event.index;
                const auto& rec=parsed.syntax.records[i];
                if(rec.begin+1>=root.identifiers.size()||!root.identifiers[rec.begin+1]){
                    reject(out,root,rec.begin,"record_name_missing");continue;
                }
                const auto scope=rec.parent!=0xffffffffu && rec.parent<i
                    ? parsed.owners[rec.parent]:lexical_scope(root,rec.begin,ns,out);
                if(!scope){reject(out,root,rec.begin,"record_scope_missing");continue;}
                identity_ref id;
                if(!succeeded(out.ids.resolve(scope,root.identifiers[rec.begin+1],identity_kind::type,id))||!id){
                    reject(out,root,rec.begin,"record_identity_failed");continue;
                }
                parsed.owners[i]=id;
                if(rec.begin<root.origins.size())
                    out.first_type_source.try_emplace(id.value(),root.origins[rec.begin]);
                type_handle handle;
                if(!succeeded(out.G.declare_record(id,kind_of(rec.kind),handle))||!handle){
                    reject(out,root,rec.begin,"declare_record_failed");continue;
                }
                parsed.handles[i]=handle;
                if(!rec.forward)++out.records;
                continue;
            }
            const auto& decl=parsed.syntax.declarations[event.index];
            if(decl.end<=decl.begin+3 || root.effective[decl.end-1]!=token_kind::semicolon){
                reject(out,root,decl.begin,"typedef_invalid_span");continue;
            }
            const auto name_index=decl.end-2;
            if(root.effective[name_index]!=token_kind::identifier||!root.identifiers[name_index]){
                reject(out,root,decl.begin,"typedef_alias_missing");continue;
            }
            const auto scope=lexical_scope(root,decl.begin,ns,out);
            v316::type_spec spec;
            const auto builtin=unambiguous_builtin_type(root.effective,decl.begin+1,name_index);
            if(!v316::resolve_base(src,decl.begin+1,name_index,scope,out.ids,out.G,builtin,spec) ||
               spec.length!=0 || spec.base_kind!=type_ref_kind::intrinsic){
                reject(out,root,decl.begin,"unsupported_typedef");continue;
            }
            identity_ref alias;
            if(!succeeded(out.ids.resolve(scope,root.identifiers[name_index],identity_kind::type,alias))||!alias){
                reject(out,root,decl.begin,"typedef_identity_failed");continue;
            }
            if(decl.begin<root.origins.size())
                out.first_type_source.try_emplace(alias.value(),root.origins[decl.begin]);
            type_handle handle;
            if(!succeeded(out.G.define_intrinsic_alias(alias,
                 static_cast<intrinsic_type>(spec.base_payload),handle))||!handle){
                reject(out,root,decl.begin,"typedef_emit_failed");continue;
            }
            ++out.aliases;
        }
    }
    if(out.profile)out.profile->identity_ms+=elapsed_ms(identity_start);
    // Pass 2: define fields using already declared Graph types and aliases.
    // Pass 3: field records. Build O(N) owner/declarator indexes first.
    for(auto& parsed:roots){
        const auto& root=*parsed.root;
        const v316::source_view src{root.effective,root.identifiers,root.positive_numbers,
            &input.strings,&input.strings};
        const auto index_start=out.profile?profile_now():clock_type::time_point{};
        // V3-37: no owner buckets and no per-declaration descriptor-pointer array.
        // Syntax scanner emits declarations in lexical order. A record's range
        // includes nested records; an owner check filters them without a copy.
        // Declarators are also emitted monotonically, at most one per declaration.
        // A single lower_bound per record + forward-only cursor accesses them.
        if(out.profile)out.profile->index_ms+=elapsed_ms(index_start);
        for(std::size_t ri=0;ri<parsed.syntax.records.size();++ri){
            const auto& rec=parsed.syntax.records[ri];
            if(rec.forward)continue;
            if(!parsed.handles[ri] || !parsed.owners[ri])continue;
#ifdef CW_V324_DIRECT_GRAPH_WRITE
#ifdef CW_V331_SAMPLED_TYPE_PROFILE
            if(out.profile){
                const auto ordinal=out.profile->eligible_records++;
                out.sampled_record=(ordinal%out.profile->sample_stride)==0;
                if(out.sampled_record)++out.profile->sampled_records;
            }
#endif
            const auto record_handle=parsed.handles[ri];
            if(!succeeded(out.G.begin_direct_record(record_handle,rec.declaration_count))){
                reject(out,root,rec.begin,"direct_begin_failed");continue;
            }
            std::size_t field_count=0;
#else
            std::vector<member_record> fields;
            std::vector<construction_value> values;
#endif
#ifdef CW_V323_DIRECT_INDEX
            auto& by_name=out.names;
            by_name.begin_record();
#ifdef CW_V326_FAST_TYPE_RESOLVE
            out.named_types.begin_record();
#ifdef CW_V331_SCOPE_CHAIN
            const bool sample_scope = out.profile && out.sampled_record;
            const auto scope_start=sample_scope?clock_type::now():clock_type::time_point{};
            out.lexical_scopes.begin_record(out.ids,parsed.owners[ri]);
            if(sample_scope){
                out.profile->sampled_scope_prepare_ms+=elapsed_ms(scope_start);
                out.profile->sampled_scope_depth_total+=out.lexical_scopes.depth();
            }
#endif
#endif
#else
            std::unordered_map<std::uint32_t,std::size_t> by_name;
#endif
            constexpr auto no_member=v323_index::direct_id_index::not_found;
            const auto find_member_index=[&](std::uint32_t id)->std::size_t {
#ifdef CW_V323_DIRECT_INDEX
                return by_name.find(id);
#else
                const auto it=by_name.find(id);
                return it==by_name.end()?no_member:it->second;
#endif
            };
            const auto register_member=[&](std::uint32_t id,std::size_t index)->bool {
#ifdef CW_V323_DIRECT_INDEX
                return by_name.insert(id,index);
#else
                return by_name.emplace(id,index).second;
#endif
            };
            std::vector<std::pair<std::size_t,std::uint32_t>> reference_fields;
#ifdef CW_V323_DIRECT_INDEX
            // Upper bound on data fields; V3 descriptors include callables,
            // so reserve is conservative and avoids repeated vector growth.
#ifndef CW_V324_DIRECT_GRAPH_WRITE
            fields.reserve(rec.declaration_count);
            values.reserve(rec.declaration_count);
#endif
#endif
            const auto size_fields=[&]()->std::size_t {
#ifdef CW_V324_DIRECT_GRAPH_WRITE
                return field_count;
#else
                return fields.size();
#endif
            };
            const auto field_type=[&](std::size_t i)->type_ref {
#ifdef CW_V324_DIRECT_GRAPH_WRITE
                const auto* member=out.G.direct_record_member(record_handle,i);
                return member?member->type:type_ref{};
#else
                return i<fields.size()?fields[i].type:type_ref{};
#endif
            };
            const auto push_field=[&](member_record m,construction_value v)->bool {
#ifdef CW_V324_DIRECT_GRAPH_WRITE
                if(!succeeded(out.G.append_direct_member(record_handle,m,v)))return false;
                ++field_count;return true;
#else
                fields.push_back(m);values.push_back(v);return true;
#endif
            };
            const auto set_construction=[&](std::size_t i,construction_value v)->bool {
#ifdef CW_V324_DIRECT_GRAPH_WRITE
                return succeeded(out.G.set_direct_construction(record_handle,i,v));
#else
                if(i>=values.size())return false;
                values[i]=v;return true;
#endif
            };
            graph_member_access access=initial_access(rec.kind);
            bool declares_virtual=false;
            const auto fields_start=out.profile?profile_now():clock_type::time_point{};
            // Record-local cursor into the scanner's monotonic declarator stream.
            // In nested records, skip descriptors belonging to nested owners.
            auto descriptor_it=std::lower_bound(parsed.syntax.declarators.begin(),
                parsed.syntax.declarators.end(),rec.declaration_first,
                [](const syntax_declarator_08& d,std::uint32_t index) {
                    return d.declaration<index;
                });
            const auto descriptor_end=parsed.syntax.declarators.end();
            const auto declaration_end=static_cast<std::size_t>(rec.declaration_first)+
                rec.declaration_count;
            for(std::size_t di=rec.declaration_first;di<declaration_end;++di){
                const auto& decl=parsed.syntax.declarations[di];
                if(decl.owner!=ri){
                    // The scanner's nested record has a complete DFS range;
                    // leap over its declarations (not just one foreign row).
                    if(decl.owner<parsed.syntax.records.size()){
                        const auto& nested=parsed.syntax.records[decl.owner];
                        const auto nested_end=static_cast<std::size_t>(nested.declaration_first)+
                            nested.declaration_count;
                        if(nested_end>di && nested_end<=declaration_end)
                            di=nested_end-1;
                    }
                    continue;
                }
                while(descriptor_it!=descriptor_end && descriptor_it->declaration<di)
                    ++descriptor_it;
                const syntax_declarator_08* descriptor=
                    descriptor_it!=descriptor_end && descriptor_it->declaration==di?
                        &*descriptor_it:nullptr;
                if(decl.kind==syntax_decl_03::access){
                    access=access_of(root.effective[decl.begin]);continue;
                }
                if(decl.begin<root.effective.size()&&root.effective[decl.begin]==token_kind::kw_virtual)
                    declares_virtual=true;
                if(decl.kind!=syntax_decl_03::field_candidate &&
                   decl.kind!=syntax_decl_03::method_candidate &&
                   decl.kind!=syntax_decl_03::ambiguous_callable)continue;
                // Header V2 parses the return type of every non-constructor
                // callable, even though methods do not create Graph members.
                // Do it HERE, before the next field, to preserve derived WHERE.
                const bool callable=decl.kind==syntax_decl_03::method_candidate ||
                    decl.kind==syntax_decl_03::ambiguous_callable;
                const bool grouped_field=descriptor &&
                    v312::parenthesized_array_field(root.effective,decl,*descriptor);
                if(callable && !grouped_field){
                    const auto status=emit_method_return(out,root,decl,parsed.owners[ri],
                        record_name(root,rec,input.strings),input.strings);
                    if(status==method_return_status::invalid)
                        reject(out,root,decl.begin,"method_return_type_unresolved");
                    else if(status==method_return_status::no_return)
                        ++out.method_no_return;
                    continue;
                }
                // One descriptor per declaration in V3-06.
                if(descriptor){
                    const auto& d=*descriptor;
                    const bool grouped=v312::parenthesized_array_field(root.effective,decl,d);
                    if(decl.kind!=syntax_decl_03::field_candidate && !grouped)continue;
                    if(d.name>=root.identifiers.size() || !root.identifiers[d.name]){
                        reject(out,root,decl.begin,"field_name_missing");continue;
                    }
                    const auto name=root.identifiers[d.name];
                    if(find_member_index(name.value())!=no_member){
                        reject(out,root,d.name,"duplicate_field");continue;
                    }
                    const auto type=produce_type(src,decl,d,grouped,parsed.owners[ri],out);
                    if(!type){reject(out,root,d.name,"field_type_unresolved");continue;}
                    const auto current_index=size_fields();
                    if(!register_member(name.value(),current_index)){
                        reject(out,root,d.name,"field_index_insert_failed");continue;
                    }
                    if(!push_field({name,type,access,{}},{})){
                        reject(out,root,d.name,"direct_member_append_failed");continue;
                    }
                    const auto decoded=v318::parse_field(root.effective,d.initializer_begin,decl.end,
                        [&](std::size_t i){return symbol(root,i);},
                        [&](std::size_t i){return decoded_number(root,i);});
                    if(!decoded.complete){reject(out,root,d.name,"field_init_"+decoded.reason);continue;}
                    if(decoded.value.kind==v317::operand_kind::member_reference){
                        reference_fields.emplace_back(current_index,decoded.value.reference);
                    }else if(!set_construction(current_index,constant(decoded.value))){
                        reject(out,root,d.name,"field_construction_failed");continue;
                    }
                    ++out.fields;
                }
            }
            auto reference_binding=[&](std::size_t to,std::uint32_t source_name,
                                       construction_value& result)->bool {
                const auto source_index=find_member_index(source_name);
                if(source_index==no_member||to>=size_fields())return false;
                derived_type_record target_ref{},source_ref{};
                if(!out.G.derived(field_type(to),target_ref)||
                   (target_ref.kind!=derived_type_kind::lvalue_reference &&
                    target_ref.kind!=derived_type_kind::rvalue_reference))return false;
                auto source_type=field_type(source_index);
                if(out.G.derived(source_type,source_ref) &&
                   (source_ref.kind==derived_type_kind::lvalue_reference ||
                    source_ref.kind==derived_type_kind::rvalue_reference))
                    source_type=source_ref.child;
                if(source_type!=target_ref.child)return false;
                result=construction_value::member_binding(
                    static_cast<std::uint32_t>(source_index+1));
                return true;
            };
            for(const auto& [index,src]:reference_fields){
                construction_value value{};
                if(!reference_binding(index,src,value) ||
                   !set_construction(index,value))
                    reject(out,root,rec.begin,"field_reference_failed");
            }
            if(out.profile)out.profile->fields_ms+=elapsed_ms(fields_start);
            const auto ctor_start=out.profile?profile_now():clock_type::time_point{};
            std::vector<v317::operation> nested_ops;
            const auto owner_name=record_name(root,rec,input.strings);
            // Constructor replay must retain the same owner-filtered lexical order.
            for(std::size_t di=rec.declaration_first;di<declaration_end;++di){
                const auto& decl=parsed.syntax.declarations[di];
                if(decl.owner!=ri){
                    // The scanner's nested record has a complete DFS range;
                    // leap over its declarations (not just one foreign row).
                    if(decl.owner<parsed.syntax.records.size()){
                        const auto& nested=parsed.syntax.records[decl.owner];
                        const auto nested_end=static_cast<std::size_t>(nested.declaration_first)+
                            nested.declaration_count;
                        if(nested_end>di && nested_end<=declaration_end)
                            di=nested_end-1;
                    }
                    continue;
                }
                if(decl.kind!=syntax_decl_03::method_candidate &&
                   decl.kind!=syntax_decl_03::ambiguous_callable)continue;
                if(!constructor_shaped(root,decl,owner_name,input.strings))continue;
                const auto decoded=v317::parse(root.effective,decl.begin,decl.end,
                    [&](std::size_t i){return symbol(root,i);},
                    [&](std::size_t i){return decoded_number(root,i);});
                if(!decoded.complete){
                    reject(out,root,decl.begin,"constructor_"+decoded.reason);continue;
                }
                ++out.constructors;
                for(const auto& op:decoded.operations){
                    if(!op.path.empty()){
                        nested_ops.push_back(op);continue;
                    }
                    const auto target_index=find_member_index(op.target);
                    if(target_index==no_member){
                        reject(out,root,op.token,"constructor_target_unmapped");continue;
                    }
                    auto value=constant(op);
                    if(op.kind==v317::operand_kind::member_reference &&
                       !reference_binding(target_index,op.reference,value)){
                        reject(out,root,op.token,"constructor_reference_failed");continue;
                    }
                    if(!set_construction(target_index,value)){
                        reject(out,root,op.token,"constructor_write_failed");continue;
                    } // last assignment wins
                    ++out.direct;
                }
            }
            if(out.profile)out.profile->constructors_ms+=elapsed_ms(ctor_start);
            const auto base_start=out.profile?profile_now():clock_type::time_point{};
            std::vector<base_record> bases;
            if(rec.base_begin<rec.base_end) {
                // Grammar: [public|protected|private] [virtual] Base [, ...].
                auto p=rec.base_begin;
                while(p<rec.base_end){
                    auto ba=initial_access(rec.kind);
                    bool access_seen=false,virtual_seen=false;
                    while(p<rec.base_end){
                        const auto k=root.effective[p];
                        if(!access_seen&&(k==token_kind::kw_public||k==token_kind::kw_protected||k==token_kind::kw_private)){
                            ba=access_of(k);access_seen=true;++p;
                        } else if(!virtual_seen && k==token_kind::kw_virtual){
                            virtual_seen=true;++p;
                        } else break;
                    }
                    if(virtual_seen || p>=rec.base_end || root.effective[p]!=token_kind::identifier ||
                       !root.identifiers[p]){
                        reject(out,root,rec.base_begin,"unsupported_base");break;
                    }
                    identity_record owner_record{};
                    if(!out.ids.record(parsed.owners[ri],owner_record)){
                        reject(out,root,p,"owner_identity_missing");break;
                    }
                    v316::type_spec trace;
                    auto base=v316::lookup_type(out.ids,owner_record.parent,
                        root.identifiers[p],trace,p);
                    if(!base){reject(out,root,p,"base_identity_unresolved");break;}
                    bases.push_back({base,ba,0,0});
                    ++p;
                    if(p>=rec.base_end)break;
                    if(root.effective[p]!=token_kind::comma){
                        reject(out,root,p,"unsupported_base_syntax");break;
                    }
                    ++p;
                }
            }
            if(out.profile)out.profile->inheritance_ms+=elapsed_ms(base_start);
            const auto define_start=out.profile?profile_now():clock_type::time_point{};
#ifdef CW_V324_DIRECT_GRAPH_WRITE
            const auto defined=out.G.commit_prevalidated_record(record_handle,kind_of(rec.kind),
                bases,declares_virtual);
            if(!succeeded(defined)){
                out.G.abort_direct_record(record_handle);
                reject(out,root,rec.begin,"record_direct_commit_failed");continue;
            }
#else
            if(!succeeded(out.G.define_resolved_record(parsed.handles[ri],kind_of(rec.kind),
                fields,values,bases,declares_virtual))){
                reject(out,root,rec.begin,"record_definition_failed");continue;
            }
#endif
            if(out.profile)out.profile->graph_define_ms+=elapsed_ms(define_start);
            const auto nested_start=out.profile?profile_now():clock_type::time_point{};
            // Nested construction defaults are stored directly in the V3 Graph
            // after the owning type is defined. The expected value is NEVER
            // read from the V2 Graph.
            for(const auto& op:nested_ops){
                const auto target_index=find_member_index(op.target);
                if(target_index==no_member){
                    reject(out,root,op.token,"nested_target_unmapped");continue;
                }
                if(op.kind==v317::operand_kind::member_reference){
                    reject(out,root,op.token,"nested_reference_unsupported");continue;
                }
                auto type=field_type(target_index);
                std::string path{input.strings.spelling(op.target)};
                bool valid=true;
                for(const auto& step:op.path){
                    if(step.array){
                        derived_type_record array{};
                        if(!out.G.derived(type,array) ||
                           array.kind!=derived_type_kind::bounded_array ||
                           step.index>=array.payload){valid=false;break;}
                        type=array.child;
                        path+="["+std::to_string(step.index)+"]";
                    }else{
                        type_handle record;
                        if(!out.G.named(type,record)){valid=false;break;}
                        const auto step_name=input.strings.find(input.strings.spelling(step.name));
                        // V3-25: member-name index is built in one batch after all records.
                        // During construction we only need this read-only record-local lookup.
                        const member_record* member=nullptr;
                        for(const auto& candidate:out.G.members(record)){
                            if(candidate.name.value()==step_name.value()){member=&candidate;break;}
                        }
                        if(!member){valid=false;break;}
                        type=member->type;
                        path+="."+std::string{input.strings.spelling(step.name)};
                    }
                }
                if(!valid || type.kind()!=type_ref_kind::intrinsic){
                    reject(out,root,op.token,"nested_path_invalid");continue;
                }
                string_id key{};
                if(!succeeded(input.strings.intern(path,key)) || !key ||
                   !succeeded(out.G.constructor_defaults.add({parsed.owners[ri],key,constant(op)}))){
                    reject(out,root,op.token,"nested_default_emit_failed");continue;
                }
                ++out.nested;
            }
            if(out.profile){
                out.profile->nested_ms+=elapsed_ms(nested_start);
                ++out.profile->records;
            }
        }
    }
    // Batch-build the same O(1) Graph member index expected by subsequent
    // PUBLISH consumers. Do not omit it from the final Header Graph.
    const auto index_finish=out.profile?profile_now():clock_type::time_point{};
    if(!succeeded(out.G.finish_prevalidated_member_index())){
        ++out.unsupported;
        return false;
    }
    if(out.profile)out.profile->graph_define_ms+=elapsed_ms(index_finish);
    return out.unsupported==0;
}

[[nodiscard]] std::string path(identity_ref id,const identity_space& ids,const string_table& strings){
    std::vector<std::string> parts;
    for(unsigned i=0;id && i<256;++i){
        identity_record rec;
        if(!ids.record(id,rec))break;
        if(rec.name)parts.emplace_back(strings.get(rec.name));
        if(!rec.parent)break;
        id=rec.parent;
    }
    std::string result;
    for(auto it=parts.rbegin();it!=parts.rend();++it){
        if(!result.empty())result+="::";
        result+=*it;
    }
    return result;
}
[[nodiscard]] std::string type_signature(type_ref value,const graph& G,
    const identity_space& ids,const string_table& strings){
    std::string result;
    for(std::size_t n=0;value && n<64;++n){
        if(value.kind()==type_ref_kind::derived){
            derived_type_record d;
            if(!G.derived(value,d))return "INVALID";
            result+=std::to_string(static_cast<unsigned>(d.kind))+"("+
                std::to_string(d.payload)+")/";
            value=d.child;
        }else if(value.kind()==type_ref_kind::intrinsic){
            return result+"intrinsic:"+std::to_string(value.payload());
        }else if(value.kind()==type_ref_kind::named){
            return result+"named:"+path(identity_ref::from_raw(
                 (static_cast<std::uint32_t>(identity_kind::type)<<30)|value.payload()),ids,strings);
        }else return "INVALID";
    }
    return "INVALID";
}
struct comparison final {
    std::uint64_t type_matches=0,type_missing=0,member_matches=0,member_mismatch=0,
        construction_matches=0,construction_mismatch=0,base_matches=0,base_mismatch=0,
        nested_matches=0,nested_mismatch=0,extra_types=0,polymorphic_mismatch=0,
        identity_mismatch=0,type_where_mismatch=0,member_where_mismatch=0,
        derived_where_mismatch=0,derived_extra_v3=0,derived_extra_v2=0;
    std::vector<diagnostic_row> locations;
    std::vector<std::string> differences;
};
[[nodiscard]] comparison compare(const producer_state& own,const string_table& own_strings,
                                  const semantic_world& reference){
    comparison result;
    struct type_snapshot{
        const graph* G{};const identity_space* ids{};const string_table* strings{};
        const type_entry* entry{};std::size_t index=0;
    };
    std::map<std::string,type_snapshot> produced;
    const auto own_entries=own.G.type_entries();
    const auto own_ids=own.G.type_identity_entries();
    for(std::size_t i=0;i<own_entries.size();++i)
        produced.emplace(path(own_ids[i],own.ids,own_strings),type_snapshot{
            &own.G,&own.ids,&own_strings,&own_entries[i],i});
    const auto ref_entries=reference.G.type_entries();
    const auto ref_ids=reference.G.type_identity_entries();
    const auto om=own.G.member_entries(), rm=reference.G.member_entries();
    const auto oc=own.G.member_construction_entries(),rc=reference.G.member_construction_entries();
    for(std::size_t i=0;i<ref_entries.size();++i){
        const auto name=path(ref_ids[i],reference.identities,reference.strings);
        const auto it=produced.find(name);
        if(it==produced.end()){
            ++result.type_missing;
            if(result.differences.size()<300)result.differences.push_back("missing_type:"+name);
            continue;
        }
        ++result.type_matches;
        const auto own_who=own.G.type_identity_entries()[it->second.index];
        const bool who_diff=own_who.value()!=ref_ids[i].value();
        const bool where_diff=it->second.index!=i;
        if(who_diff)++result.identity_mismatch;
        if(where_diff)++result.type_where_mismatch;
        if((who_diff||where_diff)&&result.locations.size()<64){
            const auto source=own.first_type_source.find(own_who.value());
            const auto origin=source==own.first_type_source.end()?token_origin{}:source->second;
            result.locations.push_back({"identity_or_type_where_mismatch",name,"",
               "v3_who="+std::to_string(own_who.value())+
               ";v2_who="+std::to_string(ref_ids[i].value())+
               ";v3_where="+std::to_string(it->second.index+1)+
               ";v2_where="+std::to_string(i+1),
               origin.file,origin.source_offset,origin.source_length});
        }
        const auto& src=*it->second.entry;
        const auto& ref=ref_entries[i];
        if(src.kind!=ref.kind || src.record_kind!=ref.record_kind || src.defined()!=ref.defined()){
            ++result.member_mismatch;
            if(result.differences.size()<300)result.differences.push_back("type_header:"+name);
            continue;
        }
        if(src.kind==graph_type_kind::intrinsic_alias){
            if(src.alias_intrinsic()!=ref.alias_intrinsic())++result.member_mismatch;
            continue;
        }
        if(src.members.begin>om.size()||src.members.count>om.size()-src.members.begin ||
           ref.members.begin>rm.size()||ref.members.count>rm.size()-ref.members.begin){
            ++result.member_mismatch;continue;
        }
        if(src.polymorphic()!=ref.polymorphic()){
            ++result.polymorphic_mismatch;
            if(result.differences.size()<300)result.differences.push_back("polymorphic_flag:"+name);
        }
        if(src.members.count!=ref.members.count){
            ++result.member_mismatch;
            if(result.differences.size()<300)result.differences.push_back("member_count:"+name+":"+
               std::to_string(src.members.count)+"/"+std::to_string(ref.members.count));
        }
        const auto length=std::min(src.members.count,ref.members.count);
        for(std::size_t j=0;j<length;++j){
            const auto a=src.members.begin+j,b=ref.members.begin+j;
            if(a!=b)++result.member_where_mismatch;
            const bool equal=own_strings.get(om[a].name)==reference.strings.get(rm[b].name) &&
                om[a].access==rm[b].access &&
                type_signature(om[a].type,own.G,own.ids,own_strings)==
                    type_signature(rm[b].type,reference.G,reference.identities,reference.strings);
            if(equal)++result.member_matches;
            else{
                ++result.member_mismatch;
                if(result.differences.size()<300)result.differences.push_back("member:"+name+":"+
                    std::string{own_strings.get(om[a].name)}+"/"+
                    std::string{reference.strings.get(rm[b].name)});
            }
            if(a<oc.size()&&b<rc.size()&&oc[a]==rc[b])++result.construction_matches;
            else ++result.construction_mismatch;
        }
        const auto own_bases=own.G.base_entries(),ref_bases=reference.G.base_entries();
        if(src.bases.begin>own_bases.size()||src.bases.count>own_bases.size()-src.bases.begin ||
           ref.bases.begin>ref_bases.size()||ref.bases.count>ref_bases.size()-ref.bases.begin){
            ++result.base_mismatch;continue;
        }
        if(src.bases.count!=ref.bases.count){
            ++result.base_mismatch;
            if(result.differences.size()<300)result.differences.push_back("base_count:"+name);
        }
        for(std::size_t bi=0;bi<std::min(src.bases.count,ref.bases.count);++bi){
            const auto& a=own_bases[src.bases.begin+bi];
            const auto& b=ref_bases[ref.bases.begin+bi];
            if(path(a.type,own.ids,own_strings)==
               path(b.type,reference.identities,reference.strings) &&
               a.access==b.access && a.flags==b.flags)++result.base_matches;
            else {
                ++result.base_mismatch;
                if(result.differences.size()<300)result.differences.push_back("base:"+name);
            }
        }
    }
    if(produced.size()>ref_entries.size())result.extra_types+=produced.size()-ref_entries.size();
    std::map<std::string,construction_value> nested;
    for(const auto& e:own.G.constructor_defaults.entries()){
        const auto key=path(e.owner,own.ids,own_strings)+":"+
            std::string{own_strings.get(e.path)};
        if(!nested.emplace(key,e.value).second)++result.nested_mismatch;
    }
    for(const auto& e:reference.G.constructor_defaults.entries()){
        const auto key=path(e.owner,reference.identities,reference.strings)+":"+
            std::string{reference.strings.get(e.path)};
        const auto it=nested.find(key);
        if(it!=nested.end() && it->second==e.value){++result.nested_matches;nested.erase(it);}
        else{
            ++result.nested_mismatch;
            if(result.differences.size()<300)result.differences.push_back("nested:"+key);
        }
    }
    result.nested_mismatch+=nested.size();
    // Compare raw Graph-derived WHERE order as well as the canonical identity
    // of each derived entry. Matching field type signatures alone does not
    // prove that all derived records were emitted (including unused records).
    const auto own_derived=own.G.derived_type_entries();
    const auto ref_derived=reference.G.derived_type_entries();
    const auto common=std::min(own_derived.size(),ref_derived.size());
    result.derived_extra_v3=own_derived.size()-common;
    result.derived_extra_v2=ref_derived.size()-common;
    const auto signature=[&](const derived_type_record& d,const graph& G,
                             const identity_space& ids,const string_table& strings){
        return std::to_string(static_cast<unsigned>(d.kind))+"("+
            std::to_string(d.payload)+")/"+type_signature(d.child,G,ids,strings);
    };
    for(std::size_t i=0;i<common;++i){
        const auto a=signature(own_derived[i],own.G,own.ids,own_strings);
        const auto b=signature(ref_derived[i],reference.G,reference.identities,reference.strings);
        if(a!=b){
            ++result.derived_where_mismatch;
            if(result.locations.size()<64)
                result.locations.push_back({"derived_where_mismatch","","",
                    "where="+std::to_string(i+1)+";v3="+a+";v2="+b,0,0,0});
        }
    }
    // Unordered canonical multisets reveal the *actual* 15 missing shapes;
    // comparing only trailing WHERE entries confuses a shifted sequence with
    // missing data. Every Graph derived node is included, even if unused.
    std::map<std::string,std::int64_t> canonical_delta;
    for(const auto& d:own_derived)
        ++canonical_delta[signature(d,own.G,own.ids,own_strings)];
    for(const auto& d:ref_derived)
        --canonical_delta[signature(d,reference.G,reference.identities,reference.strings)];
    for(const auto& [shape,delta]:canonical_delta){
        if(delta==0)continue;
        if(result.locations.size()<128)
            result.locations.push_back({delta>0?"derived_semantic_only_v3":"derived_semantic_only_v2",
                "","","count="+std::to_string(delta>0?delta:-delta)+";shape="+shape,0,0,0});
    }
    for(std::size_t i=common;i<own_derived.size()&&i<common+32;++i)
        result.locations.push_back({"derived_only_v3","","",
            "where="+std::to_string(i+1)+";v3="+
            signature(own_derived[i],own.G,own.ids,own_strings),0,0,0});
    for(std::size_t i=common;i<ref_derived.size()&&i<common+32;++i)
        result.locations.push_back({"derived_only_v2","","",
            "where="+std::to_string(i+1)+";v2="+
            signature(ref_derived[i],reference.G,reference.identities,reference.strings),0,0,0});
    return result;
}
} // namespace v319
