#pragma once
// SB04 grammar, bounded original-token view; no catalog or WHO in workers.
namespace hc02 {
using namespace sb01;
class extractor {
    const stream& in; id root; std::size_t p=0,limit=0; local_output out;
    K kind(std::size_t look=0) const {return p+look<limit?in.tokens[p+look].kind:K::invalid;}
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
    [[noreturn]] void coverage_fail(id kind,std::string_view detail){out.failure={error_kind::coverage,position(root,p),p<in.tokens.size()?in.tokens[p].source:origin{},kind,detail};throw error_kind::coverage;}
    void expected(K k,std::string_view detail){if(!eat(k))coverage_fail(1,detail);}
    bool scalar(CV& value){
        bool negative=false;if(kind()==K::minus||kind()==K::plus){negative=kind()==K::minus;++p;}
        if(kind()==K::kw_false||kind()==K::kw_nullptr){if(negative)coverage_fail(1,"Header Parser V2 boolean/nullptr initializer cannot be negated");value={};++p;return true;}
        if(kind()==K::kw_true){if(negative)coverage_fail(1,"Header Parser V2 boolean initializer cannot be negated");value=CV::constant(CK::unsigned_integer,1);++p;return true;}
        if(kind()!=K::pp_number)return false;
        if(current().number_kind==2)value=CV::constant(CK::real,current().number^(negative?(std::uint64_t{1}<<63):0));
        else if(current().number_kind==1)value=CV::constant(negative?CK::signed_integer:CK::unsigned_integer,negative?0-current().number:current().number);
        else coverage_fail(3,"Header Parser V2 initializer requires a plain decimal numeric or boolean literal");
        ++p;return true;
    }
    void constructor(id definition){
        out.operations.push_back({definition,operation_kind::begin,{},{},position(root,p),current().source});
        expected(K::l_paren,"Header Parser V2 expected '(' after constructor name");
        if(kind()!=K::r_paren)coverage_fail(3,"Header Parser V2 managed constructors must have no parameters");++p;eat(K::kw_noexcept);
        if(eat(K::semicolon))return;
        if(eat(K::assign)){expected(K::kw_default,"Header Parser V2 managed constructor supports '= default' only");expected(K::semicolon,"Header Parser V2 expected ';' after '= default'");return;}
        auto operand=[&](K close,bool allow_empty){const auto target=current();const auto at=p;expected(K::identifier,"Header Parser V2 expected constructor member name");
            K end=close;if(close==K::invalid){if(eat(K::l_paren))end=K::r_paren;else if(eat(K::l_brace))end=K::r_brace;else coverage_fail(1,"Header Parser V2 expected constructor member initializer");}
            else expected(K::assign,"Header Parser V2 expected '=' in constructor field assignment");
            CV value{};bool complete=true;
            if(!(allow_empty&&kind()==end))complete=scalar(value);
            if(!complete){out.operations.push_back({definition,operation_kind::gap,{},{},position(root,at),target.source,(kind()==K::identifier||kind()==K::kw_this)?gap_kind::reference_operand:gap_kind::unsupported_operand});while(kind()!=end&&kind()!=K::invalid)++p;}
            expected(end,close==K::invalid?"Header Parser V2 expected end of constructor member initializer":"Header Parser V2 expected ';' after constructor field assignment");
            if(complete)out.operations.push_back({definition,operation_kind::scalar,target.name,value,position(root,at),target.source});};
        if(eat(K::colon)){do{operand(K::invalid,true);}while(eat(K::comma));}
        expected(K::l_brace,"Header Parser V2 expected managed constructor body");
        while(kind()!=K::r_brace){
            if(kind()==K::invalid)coverage_fail(1,"Header Parser V2 managed constructor body is not closed");
            if(kind()!=K::identifier||kind(1)!=K::assign){out.operations.push_back({definition,operation_kind::gap,{},{},position(root,p),current().source,(kind()==K::identifier&&(kind(1)==K::dot||kind(1)==K::l_bracket))?gap_kind::nested_target:gap_kind::body_grammar});
                unsigned depth=0;while(kind()!=K::invalid){if(kind()==K::r_brace&&depth==0)break;if(kind()==K::l_brace)++depth;else if(kind()==K::r_brace)--depth;++p;}break;}
            operand(K::semicolon,false);
        }expected(K::r_brace,"Header Parser V2 managed constructor body is not closed");
    }
    void field(id scope,id owner,id definition,std::string_view record_name){
        const auto begin=p;
        if(kind()==K::identifier&&current().name==record_name&&kind(1)==K::l_paren){++p;if(constructor_coverage_enabled)constructor(definition);else callable();return;}
        if(constructor_coverage_enabled&&kind()==K::kw_virtual&&kind(1)==K::identifier&&in.tokens[p+1].name==record_name&&kind(2)==K::l_paren){p+=2;coverage_fail(1,"Constructor cannot be virtual");}
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
            if(eat(K::assign)||kind()==K::l_brace){
                ++out.excluded_initializers;out.operations.push_back({definition,operation_kind::gap,{},{},position(root,p),in.tokens[p-1].source,gap_kind::member_initializer});
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
    extractor(const stream& value,id ordinal,std::size_t first,std::size_t last):in(value),root(ordinal),p(first),limit(last){ca01::require(first<=last&&last<=value.tokens.size(),"invalid extraction range");}
    local_output run(){out.symbols.push_back({none,root,0,{},0});try{scope(0,false);}catch(error_kind){}out.shapes->frozen=true;return std::move(out);}
};


}
