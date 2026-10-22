#pragma once
// Experimental Header type binding. No identity_space, string_table or G here.
#include "../canonicalizer-01/kernel.hpp"
#include "../../server_engine/project/frontend/lexical_token.hpp"
#include "../../server_engine/project/graph/type_ref.hpp"
#include <string>
#include <span>

namespace sb01 {
using ca01::id;
using ca01::none;
using K = cw::server::token_kind;
using I = cw::server::intrinsic_type;
using D = cw::server::derived_type_kind;
struct origin { id file=0,offset=0,length=0; friend bool operator==(origin,origin)=default; };
struct token { K kind; std::string_view name; std::uint64_t number=0; origin source; };
struct stream { std::vector<token> tokens; };
using event = std::uint64_t;
inline event position(id root,std::size_t token) {
    ca01::require(token<UINT32_MAX,"root token limit");
    return (event(root+1)<<32)|event(token+1);
}
struct modifier { D kind; std::uint64_t payload=0; friend bool operator==(modifier,modifier)=default; };
struct type_expr { std::string_view name; I builtin=I::none; std::vector<modifier> layers; };
enum class declaration_kind { name_space,record,alias };
struct declaration {
    id symbol; declaration_kind kind; K record_kind=K::invalid; I alias=I::none;
    event begin=0,complete=UINT64_MAX; origin source,complete_source,open_source;
    event open=UINT64_MAX;
};
struct request {
    id scope=0,owner=none,definition=none;
    std::string_view member;
    type_expr type;
    event at=0; origin source;
    bool base=false;
    origin incomplete_source;
#ifdef SB02_REQUEST_LIFETIME
#ifdef _MSC_VER
    [[msvc::no_unique_address]] sb02::request_lifetime audit_lifetime;
#else
    [[no_unique_address]] sb02::request_lifetime audit_lifetime;
#endif
#endif
};
enum class error_kind { none,syntax,unsupported,unknown_type,incomplete_type,record_family,alias_conflict,definition_conflict };
struct issue { error_kind kind=error_kind::none; event at=UINT64_MAX; origin source; };
inline void earliest(issue& out,issue candidate) {if(candidate.at<out.at)out=candidate;}
struct local_output {
    std::vector<ca01::symbol> symbols;
    std::vector<declaration> declarations;
    std::vector<request> requests;
    issue failure;
    std::size_t excluded_callables=0,excluded_initializers=0;
};

class parser {
    const stream& in; id root; std::size_t p=0; local_output out;
    K kind(std::size_t look=0) const {return p+look<in.tokens.size()?in.tokens[p+look].kind:K::invalid;}
    const token& current()const {ca01::require(p<in.tokens.size(),"unexpected end");return in.tokens[p];}
    bool eat(K k){if(kind()!=k)return false;++p;return true;}
    void need(K k){if(!eat(k))fail(error_kind::syntax);}
    [[noreturn]] void fail(error_kind k){out.failure={k,position(root,p),p<in.tokens.size()?current().source:origin{}};throw k;}
    static bool builtin(K k){return k==K::kw_void||k==K::kw_bool||k==K::kw_char||k==K::kw_wchar_t||k==K::kw_char8_t||k==K::kw_char16_t||k==K::kw_char32_t||k==K::kw_short||k==K::kw_int||k==K::kw_long||k==K::kw_signed||k==K::kw_unsigned||k==K::kw_float||k==K::kw_double;}
    static bool qualifier(K k){return k==K::kw_const||k==K::kw_volatile;}
    void qualifiers(std::vector<modifier>& layers) {
        bool c=false,v=false;while(qualifier(kind())){c|=kind()==K::kw_const;v|=kind()==K::kw_volatile;++p;}
        if(c)layers.push_back({D::const_qualified});if(v)layers.push_back({D::volatile_qualified});
    }
    type_expr type() {
        type_expr t;std::vector<modifier> before;qualifiers(before);
        if(kind()==K::identifier){t.name=current().name;++p;}
        else if(builtin(kind())){
            bool u=false,s=false;unsigned longs=0;K primary=K::invalid;
            while(builtin(kind())){const auto k=kind();++p;
                if(k==K::kw_unsigned){if(u)fail(error_kind::unsupported);u=true;}
                else if(k==K::kw_signed){if(s)fail(error_kind::unsupported);s=true;}
                else if(k==K::kw_long)++longs;
                else {if(primary!=K::invalid && !(primary==K::kw_short&&k==K::kw_int))fail(error_kind::unsupported);if(k!=K::kw_int||primary==K::invalid)primary=k;}
            }
            if(u&&s||longs>2)fail(error_kind::unsupported);
            switch(primary){
            case K::kw_void:t.builtin=I::void_type;break;case K::kw_bool:t.builtin=I::bool_type;break;
            case K::kw_char:t.builtin=u?I::unsigned_char:s?I::signed_char:I::char_type;break;
            case K::kw_wchar_t:t.builtin=I::wchar_type;break;case K::kw_char8_t:t.builtin=I::char8_type;break;
            case K::kw_char16_t:t.builtin=I::char16_type;break;case K::kw_char32_t:t.builtin=I::char32_type;break;
            case K::kw_float:t.builtin=I::float_type;break;case K::kw_double:t.builtin=longs?I::long_double_type:I::double_type;break;
            case K::kw_short:t.builtin=u?I::unsigned_short:I::signed_short;break;
            case K::invalid:case K::kw_int:t.builtin=longs==2?(u?I::unsigned_long_long:I::signed_long_long):longs?(u?I::unsigned_long:I::signed_long):(u?I::unsigned_int:I::signed_int);break;
            default:fail(error_kind::unsupported);
            }
        }else fail(error_kind::unsupported);
        qualifiers(t.layers);t.layers.insert(t.layers.begin(),before.begin(),before.end());
        return t;
    }
    void pointers(type_expr& t){while(kind()==K::star||kind()==K::ampersand||kind()==K::logical_and){
        auto k=kind();++p;t.layers.push_back({k==K::star?D::pointer:k==K::ampersand?D::lvalue_reference:D::rvalue_reference});qualifiers(t.layers);}}
    id symbol(id parent,id kind,std::string_view name,event at){
        ca01::require(out.symbols.size()<none,"local symbol limit");id value=static_cast<id>(out.symbols.size());
        out.symbols.push_back({parent,root,kind,name,at});return value;
    }
    void balanced(K open,K close){need(open);unsigned depth=1;while(depth){if(kind()==K::invalid)fail(error_kind::syntax);if(kind()==open)++depth;if(kind()==close)--depth;++p;}}
    // Exclusions are semantic coverage gaps, never a successful full-Header verdict.
    void callable(){++out.excluded_callables;
        while(kind()!=K::semicolon&&kind()!=K::l_brace&&kind()!=K::invalid){if(kind()==K::l_paren)balanced(K::l_paren,K::r_paren);else ++p;}
        if(kind()==K::l_brace)balanced(K::l_brace,K::r_brace);else need(K::semicolon);
    }
    void field(id scope,id owner,id definition,std::string_view record_name){
        const auto begin=p;
        if(kind()==K::identifier&&current().name==record_name&&kind(1)==K::l_paren){++p;callable();return;}
        if(kind()==K::kw_virtual||kind()==K::tilde){callable();return;}
        auto base=type();
        for(;;){auto t=base;pointers(t);std::vector<modifier> grouped;
            const bool parenthesized=eat(K::l_paren);
            if(parenthesized){type_expr inside;pointers(inside);if(inside.layers.empty())fail(error_kind::unsupported);grouped=std::move(inside.layers);}
            const auto name=current();need(K::identifier);
            if(parenthesized)need(K::r_paren);
            if(kind()==K::l_paren){callable();return;}
            std::vector<modifier> arrays;
            while(eat(K::l_bracket)){
                if(eat(K::r_bracket))arrays.push_back({D::unbounded_array});
                else {if(kind()!=K::pp_number||!current().number)fail(error_kind::unsupported);auto n=current().number;++p;need(K::r_bracket);arrays.push_back({D::bounded_array,n});}
            }
            for(auto i=arrays.rbegin();i!=arrays.rend();++i)t.layers.push_back(*i);
            t.layers.insert(t.layers.end(),grouped.begin(),grouped.end());
            auto type_source=in.tokens[begin].source;
            if(!t.name.empty())for(auto i=begin;i<p;++i)if(in.tokens[i].kind==K::identifier){type_source=in.tokens[i].source;break;}
            out.requests.push_back({scope,owner,definition,name.name,std::move(t),position(root,begin),type_source,false,name.source});
            if(eat(K::assign)){
                ++out.excluded_initializers;
                while(kind()!=K::comma&&kind()!=K::semicolon&&kind()!=K::invalid){
                    if(kind()==K::l_brace)balanced(K::l_brace,K::r_brace);else if(kind()==K::l_paren)balanced(K::l_paren,K::r_paren);else ++p;}
            }
            if(!eat(K::comma))break;
        }
        need(K::semicolon);
    }
    void record(id scope){
        const auto start=p;const auto family=kind();++p;const auto name=current();need(K::identifier);
        const auto sym=symbol(scope,2,name.name,position(root,start));
        const auto def=static_cast<id>(out.declarations.size());
        out.declarations.push_back({sym,declaration_kind::record,family,I::none,position(root,start),UINT64_MAX,name.source});
        if(eat(K::colon))for(;;){
            if(kind()==K::kw_public||kind()==K::kw_private||kind()==K::kw_protected)++p;
            if(kind()==K::kw_virtual)fail(error_kind::unsupported);
            const auto at=p;const auto base=current();need(K::identifier);
            out.requests.push_back({scope,sym,def,{},type_expr{base.name},position(root,at),base.source,true,base.source});
            if(!eat(K::comma))break;
        }
        if(eat(K::semicolon))return;
        need(K::l_brace);
        out.declarations[def].open=position(root,p);out.declarations[def].open_source=current().source;
        while(kind()!=K::r_brace){
            if(kind()==K::invalid)fail(error_kind::syntax);
            if(eat(K::semicolon))continue;
            if((kind()==K::kw_public||kind()==K::kw_private||kind()==K::kw_protected)&&kind(1)==K::colon){p+=2;continue;}
            field(scope,sym,def,name.name);
        }
        ++p;out.declarations[def].complete=position(root,p);out.declarations[def].complete_source=current().source;need(K::semicolon);
    }
    void scope(id parent,bool nested){
        while(kind()!=K::invalid&&kind()!=K::r_brace){
            if(eat(K::semicolon))continue;
            if(eat(K::kw_namespace)){
                const auto at=p;const auto n=current();need(K::identifier);
                auto sym=symbol(parent,1,n.name,position(root,at));
                out.declarations.push_back({sym,declaration_kind::name_space,K::invalid,I::none,position(root,at),position(root,at),n.source});
                need(K::l_brace);scope(sym,true);continue;
            }
            if(kind()==K::kw_struct||kind()==K::kw_class||kind()==K::kw_union){record(parent);continue;}
            if(eat(K::kw_typedef)){
                const auto at=p;auto t=type();if(!t.name.empty()||!t.layers.empty())fail(error_kind::unsupported);
                const auto n=current();need(K::identifier);need(K::semicolon);
                auto sym=symbol(parent,2,n.name,position(root,at));
                out.declarations.push_back({sym,declaration_kind::alias,K::invalid,t.builtin,position(root,at),position(root,p-1),n.source});continue;
            }
            fail(error_kind::unsupported);
        }
        if(nested)need(K::r_brace);else if(kind()!=K::invalid)fail(error_kind::syntax);
    }
public:
    parser(const stream& value,id ordinal):in(value),root(ordinal){}
    local_output run(){out.symbols.push_back({none,root,0,{},0});try{scope(0,false);}catch(error_kind){}return std::move(out);}
};

#ifndef SB03_STORAGE
struct bound_type {id entity=none; I builtin=I::none; std::vector<modifier> layers;friend bool operator==(const bound_type&,const bound_type&)=default;};
struct binding_result {
    std::vector<bound_type> types; std::vector<issue> failures;
    issue first; std::size_t compatibility_checks=0;
};
struct batch {
    ca01::input identities;
    std::vector<declaration> declarations;
    std::vector<request> requests;
    issue first;
    std::size_t excluded_callables=0,excluded_initializers=0;
};
inline batch combine(std::vector<local_output>& locals){
    batch out;
    std::size_t symbols=0,declarations=0,requests=0;
    for(const auto& local:locals){symbols+=local.symbols.size();declarations+=local.declarations.size();requests+=local.requests.size();}
    ca01::require(symbols<none&&declarations<none,"batch local index limit");
    out.identities.symbols.reserve(symbols);out.declarations.reserve(declarations);out.requests.reserve(requests);
    for(auto& local:locals){const auto offset=static_cast<id>(out.identities.symbols.size());const auto def_offset=static_cast<id>(out.declarations.size());
        earliest(out.first,local.failure);out.excluded_callables+=local.excluded_callables;out.excluded_initializers+=local.excluded_initializers;
        for(auto s:local.symbols){if(s.parent!=none)s.parent+=offset;out.identities.symbols.push_back(s);}
        for(auto d:local.declarations){d.symbol+=offset;out.declarations.push_back(d);}
        for(auto& r:local.requests){r.scope+=offset;r.owner+=offset;r.definition+=def_offset;out.requests.push_back(std::move(r));}
    }
    out.identities.prepare();return out;
}
struct catalog_entry {
    event first=UINT64_MAX,complete=UINT64_MAX;
    event alias_at=UINT64_MAX;
    I alias=I::none; bool has_graph=false,record=true,union_type=false;
};
struct binding_key {id scope; std::string_view name;friend bool operator==(binding_key,binding_key)=default;};
struct binding_hash {std::size_t operator()(binding_key k)const{return static_cast<std::size_t>(ca01::mix(ca01::hash_text(k.name)^ca01::mix(k.scope)));}};

inline binding_result bind(const batch& b,const ca01::result& c,ca01::pool& workers){
    binding_result out;out.first=b.first;out.types.resize(b.requests.size());out.failures.resize(b.requests.size());
    std::vector<catalog_entry> entries(b.identities.symbols.size());
    // One deterministic pass for declaration compatibility, not one per request.
    std::vector<id> ordered(b.declarations.size());std::iota(ordered.begin(),ordered.end(),0);
    std::sort(ordered.begin(),ordered.end(),[&](id x,id y){return b.declarations[x].begin<b.declarations[y].begin;});
    std::unordered_map<binding_key,id,binding_hash> lookup;
    std::unordered_map<std::uint64_t,bool> root_families;
    for(auto i:ordered){const auto& d=b.declarations[i];if(d.kind==declaration_kind::name_space)continue;
        const auto rep=c.representative[d.symbol];auto& e=entries[rep];const bool record=d.kind==declaration_kind::record;
        e.first=(std::min)(e.first,d.begin);
        if(record){
            const bool is_union=d.record_kind==K::kw_union;
            const auto family_key=(event(b.identities.symbols[d.symbol].worker)<<32)|rep;
            const auto [family,inserted]=root_families.emplace(family_key,is_union);
            if(!inserted){++out.compatibility_checks;if(family->second!=is_union)earliest(out.first,{error_kind::record_family,d.begin,d.source});}
            if(d.open!=UINT64_MAX){
                if(e.has_graph){++out.compatibility_checks;
                    if(!e.record)earliest(out.first,{error_kind::alias_conflict,d.open,d.open_source});
                    else if(e.union_type!=is_union)earliest(out.first,{error_kind::record_family,d.open,d.open_source});
                }else{e.has_graph=true;e.record=true;e.union_type=is_union;}
                e.complete=(std::min)(e.complete,d.complete);
            }
        }else{
            if(e.has_graph){++out.compatibility_checks;if(e.record||e.alias!=d.alias)earliest(out.first,{error_kind::alias_conflict,d.begin,d.source});}
            else{e.has_graph=true;e.record=false;e.alias=d.alias;e.alias_at=d.complete;}
        }
        const auto& s=b.identities.symbols[d.symbol];lookup.emplace(binding_key{c.representative[s.parent],s.name},rep);
    }
    const auto& immutable_lookup=lookup;
    const auto tasks=(std::min)(workers.size,b.requests.size());
    workers.run(tasks,[&](auto task,auto){for(auto i=task*b.requests.size()/tasks;i<(task+1)*b.requests.size()/tasks;++i){
        const auto& r=b.requests[i];auto& t=out.types[i];t.builtin=r.type.builtin;t.layers=r.type.layers;
        if(r.type.name.empty())continue;
        id scope=c.representative[r.scope],target=none;
        for(;;){const auto found=immutable_lookup.find({scope,r.type.name});
            if(found!=immutable_lookup.end()&&entries[found->second].first<=r.at){target=found->second;break;}
            const auto parent=b.identities.symbols[scope].parent;if(parent==none)break;scope=c.representative[parent];
        }
        auto failure=[&](error_kind k){out.failures[i]={k,r.at,k==error_kind::incomplete_type?r.incomplete_source:r.source};};
        if(target==none){failure(error_kind::unknown_type);continue;}
        const auto& e=entries[target];
        if(e.alias_at<=r.at){if(r.base)failure(error_kind::incomplete_type);else t.builtin=e.alias;continue;}
        t.entity=target;
        const bool indirect=std::any_of(t.layers.begin(),t.layers.end(),[](auto l){return l.kind==D::pointer||l.kind==D::lvalue_reference||l.kind==D::rvalue_reference;});
        if((r.base||!indirect)&&e.complete>r.at)failure(error_kind::incomplete_type);
        if(r.base&&e.union_type)failure(error_kind::incomplete_type);
    }});
    for(auto f:out.failures)earliest(out.first,f);
    // Compare each additional complete record's field/base shape once. This is
    // only the type-binding projection; callable/initializer semantics are excluded.
    std::vector<std::vector<std::size_t>> fields(b.declarations.size());
    for(std::size_t i=0;i<b.requests.size();++i)fields[b.requests[i].definition].push_back(i);
    std::vector<id> first_definition(entries.size(),none);
    for(auto di:ordered){const auto& d=b.declarations[di];if(d.kind!=declaration_kind::record||d.complete==UINT64_MAX)continue;
        auto& first=first_definition[c.representative[d.symbol]];if(first==none){first=di;continue;}++out.compatibility_checks;
        const auto& a=fields[first];const auto& z=fields[di];bool equal=a.size()==z.size();
        for(std::size_t j=0;equal&&j<a.size();++j)equal=b.requests[a[j]].member==b.requests[z[j]].member&&b.requests[a[j]].base==b.requests[z[j]].base&&out.types[a[j]]==out.types[z[j]];
        if(!equal)earliest(out.first,{error_kind::definition_conflict,d.complete,d.complete_source});
    }
    return out;
}
#endif // SB03_STORAGE: experiment 03 reuses the identical local Parser.
} // namespace sb01
