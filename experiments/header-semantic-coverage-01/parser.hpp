#pragma once
// Experimental Header type binding. No identity_space, string_table or G here.
#include "../canonicalizer-01/kernel.hpp"
#include "../../server_engine/project/frontend/lexical_token.hpp"
#include "../../server_engine/project/graph/type_ref.hpp"
#include <string>
#include <span>
#include "../../server_engine/project/graph/construction_value.hpp"

namespace sb01 {
using ca01::id;
using ca01::none;
using K = cw::server::token_kind;
using I = cw::server::intrinsic_type;
using D = cw::server::derived_type_kind;
struct origin { id file=0,offset=0,length=0; friend bool operator==(origin,origin)=default; };
struct token { K kind; std::string_view name; std::uint64_t number=0; origin source; id number_kind=0; };
struct stream { std::vector<token> tokens; };
using event = std::uint64_t;
inline event position(id root,std::size_t token) {
    ca01::require(token<UINT32_MAX,"root token limit");
    return (event(root+1)<<32)|event(token+1);
}
struct modifier { D kind; std::uint64_t payload=0; friend bool operator==(modifier,modifier)=default; };
struct shape_arena;
struct shape_range {
    const shape_arena* arena=nullptr; id offset=0,count=0;
    const modifier* begin()const; const modifier* end()const;
    bool empty()const{return count==0;} std::size_t size()const{return count;}
};
struct shape_arena {
    std::vector<modifier> payload; bool frozen=false;
    shape_range append(std::span<const modifier> layers){
        ca01::require(!frozen&&payload.size()+layers.size()<none,"invalid shape append");
        auto offset=static_cast<id>(payload.size());payload.insert(payload.end(),layers.begin(),layers.end());
        return {this,offset,static_cast<id>(layers.size())};
    }
};
inline const modifier* shape_range::begin()const {static const modifier empty{};return count?arena->payload.data()+offset:&empty;}
inline const modifier* shape_range::end()const{return begin()+count;}
struct request_type {std::string_view name; I builtin=I::none; shape_range layers;};
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
    request_type type;
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
enum class error_kind { none,syntax,unsupported,unknown_type,incomplete_type,record_family,alias_conflict,definition_conflict,coverage };
struct issue { error_kind kind=error_kind::none; event at=UINT64_MAX; origin source; id diagnostic_kind=0;std::string_view detail; };
inline void earliest(issue& out,issue candidate) {if(candidate.at<out.at)out=candidate;}
inline bool constructor_coverage_enabled=true;
using CV=cw::server::construction_value;using CK=cw::server::construction_kind;
enum class operation_kind {begin,scalar,gap};
enum class gap_kind {none,reference_operand,nested_target,body_grammar,member_initializer,structural_fallback,unsupported_operand};
struct operation {id definition;operation_kind kind;std::string_view target;CV value;event at;origin source;gap_kind gap=gap_kind::none;};
struct local_output {
    std::vector<ca01::symbol> symbols;
    std::vector<declaration> declarations;
    std::vector<request> requests;
    std::vector<operation> operations;
    std::shared_ptr<shape_arena> shapes=std::make_shared<shape_arena>();
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
    request_type store(const type_expr& t){return {t.name,t.builtin,out.shapes->append(t.layers)};}
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
            out.requests.push_back({scope,owner,definition,name.name,store(t),position(root,begin),type_source,false,name.source});
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
            out.requests.push_back({scope,sym,def,{},store(type_expr{base.name}),position(root,at),base.source,true,base.source});
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
    local_output run(){out.symbols.push_back({none,root,0,{},0});try{scope(0,false);}catch(error_kind){}out.shapes->frozen=true;return std::move(out);}
};


}
