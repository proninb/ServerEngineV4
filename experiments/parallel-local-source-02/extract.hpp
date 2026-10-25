#pragma once
#include "model.hpp"
#include "../../server_engine/project/frontend/source_token.hpp"
#include <bit>
#include <charconv>
#include <span>
#include <unordered_map>

namespace pls02 {
// Sole Source grammar owner in this prototype. It has no G/WHO/string_table.
class extractor {
    model out;
    std::span<const source_predecoded_token> tokens;
    std::string_view source;
    std::size_t cursor=0;
    id consumed_end=0;
    std::unordered_map<std::string_view,id> names;
    token_kind kind() const {return cursor<tokens.size()?tokens[cursor].kind:token_kind::invalid;}
    source_range at() const {return cursor<tokens.size()?source_range{tokens[cursor].source_offset,tokens[cursor].source_length}:source_range{static_cast<id>(source.size()),0};}
    std::string_view spelling() const {const auto r=at();return source.substr(r.offset,r.length);}
    void advance(){if(cursor<tokens.size()){consumed_end=tokens[cursor].source_offset+tokens[cursor].source_length;++cursor;++out.token_reads;}}
    void require(token_kind k,const char* text){if(kind()!=k)throw failure(text,{out.input.file,at()});}
    void take(token_kind k,const char* text){require(k,text);advance();}
    id name(){
        require(token_kind::identifier,"Expected identifier");const auto s=spelling();
        const auto found=names.find(s);id n;
        if(found!=names.end())n=found->second;
        else {n=static_cast<id>(out.names.size());out.names.push_back({static_cast<id>(out.text.size()),static_cast<id>(s.size())});out.text.insert(out.text.end(),s.begin(),s.end());names.emplace(s,n);}
        advance();return n;
    }
    id symbol_at(id scope){
        const auto start=at();range r{static_cast<id>(out.components.size()),0};
        for(;;){const auto loc=at();const auto n=name();out.components.push_back({n,loc});++r.count;
            if(kind()!=token_kind::scope)break;advance();}
        const auto last=out.components.back().at;
        out.symbols.push_back({scope,r,{start.offset,last.offset+last.length-start.offset}});return static_cast<id>(out.symbols.size()-1);
    }
    std::uint64_t integer(){
        require(token_kind::pp_number,"Expected decimal integer");auto s=spelling();std::uint64_t n=0;
        const auto p=std::from_chars(s.data(),s.data()+s.size(),n);
        if(p.ec!=std::errc{} || p.ptr!=s.data()+s.size())throw failure("Invalid decimal integer",{out.input.file,at()});
        advance();return n;
    }
    id endpoint_after(id sym){
        const auto loc=out.symbols[sym].at;range rr{static_cast<id>(out.selectors.size()),0};
        auto end=loc.offset+loc.length;
        while(kind()==token_kind::dot || kind()==token_kind::l_bracket){
            if(rr.count>=128)throw failure("Source endpoint too deep",{out.input.file,at()});
            if(kind()==token_kind::dot){advance();const auto a=at();const auto n=name();
                if(a.length&0x80000000u)throw failure("Selector spelling too long",{out.input.file,a});
                out.selectors.push_back({n,a.offset,a.length|0x80000000u});end=a.offset+a.length;}
            else {advance();const auto a=at();const auto n=integer();end=at().offset+at().length;take(token_kind::r_bracket,"Expected ']'");
                if(a.length&0x80000000u)throw failure("Index spelling too long",{out.input.file,a});out.selectors.push_back({n,a.offset,a.length});}
            ++rr.count;
        }
        out.endpoints.push_back({sym,rr,{loc.offset,end-loc.offset}});return static_cast<id>(out.endpoints.size()-1);
    }
    void string_bytes(){
        auto text=spelling();const auto loc=at();
        if(text.size()<2 || text.front()!='"' || text.back()!='"')throw failure("Only narrow strings are supported",{out.input.file,loc});
        for(std::size_t i=1;i+1<text.size();++i){unsigned v=static_cast<unsigned char>(text[i]);
            if(v=='\\'){
                if(++i+1>=text.size())throw failure("Incomplete string escape",{out.input.file,loc});
                const auto ch=text[i];
                if(ch=='n')v='\n';else if(ch=='r')v='\r';else if(ch=='t')v='\t';else if(ch=='a')v='\a';else if(ch=='b')v='\b';else if(ch=='f')v='\f';else if(ch=='v')v='\v';
                else if(ch=='\\' || ch=='\'' || ch=='"' || ch=='?')v=static_cast<unsigned char>(ch);
                else if(ch=='x'){
                    const auto begin=++i;while(i+1<text.size() && ((text[i]>='0'&&text[i]<='9')||(text[i]>='a'&&text[i]<='f')||(text[i]>='A'&&text[i]<='F')))++i;
                    if(i==begin)throw failure("Empty hexadecimal escape",{out.input.file,loc});
                    const auto p=std::from_chars(text.data()+begin,text.data()+i,v,16);if(p.ec!=std::errc{} || v>255)throw failure("String escape exceeds byte range",{out.input.file,loc});--i;
                } else if(ch>='0'&&ch<='7'){
                    v=0;unsigned digits=0;while(i+1<text.size() && digits<3 && text[i]>='0'&&text[i]<='7'){v=v*8+(text[i]-'0');++i;++digits;}--i;
                    if(v>255)throw failure("String escape exceeds byte range",{out.input.file,loc});
                } else throw failure("Unsupported string escape",{out.input.file,loc});
            }
            out.bytes.push_back(static_cast<std::uint8_t>(v));
        }
        advance();
    }
    id rhs(id scope){
        value v;const auto begin=at();v.at=begin;
        if(kind()==token_kind::string_literal){v.kind=value_kind::string;v.bytes.begin=static_cast<id>(out.bytes.size());
            while(kind()==token_kind::string_literal)string_bytes();v.bytes.count=static_cast<id>(out.bytes.size())-v.bytes.begin;
        } else if(kind()==token_kind::identifier){v.kind=value_kind::symbol;v.endpoint=endpoint_after(symbol_at(scope));v.at=out.endpoints[v.endpoint].at;
        } else if(kind()==token_kind::kw_true || kind()==token_kind::kw_false || kind()==token_kind::kw_nullptr){
            v.number=kind()==token_kind::kw_true?construction_value::constant(construction_kind::unsigned_integer,1):construction_value{};advance();
        } else {
            bool neg=false;if(kind()==token_kind::plus || kind()==token_kind::minus){neg=kind()==token_kind::minus;advance();}
            require(token_kind::pp_number,"Expected scalar literal or symbolic operand");const auto s=spelling();
            if(s.find_first_of(".eE")!=std::string_view::npos){double n=0;const auto p=std::from_chars(s.data(),s.data()+s.size(),n,std::chars_format::general);
                if(p.ec!=std::errc{}||p.ptr!=s.data()+s.size())throw failure("Invalid real literal",{out.input.file,at()});
                if(neg)n=-n;v.number=construction_value::constant(construction_kind::real,std::bit_cast<std::uint64_t>(n));advance();
            } else {const auto n=integer();if(neg && n>(std::uint64_t{1}<<63))throw failure("Signed integer overflows",{out.input.file,begin});
                v.number=construction_value::constant(neg?construction_kind::signed_integer:construction_kind::unsigned_integer,neg?std::uint64_t{}-n:n);}
        }
        v.at={begin.offset,consumed_end-begin.offset};out.values.push_back(v);return static_cast<id>(out.values.size()-1);
    }
    bool builtin() const {
        switch(kind()){
        case token_kind::kw_void:case token_kind::kw_bool:case token_kind::kw_char:case token_kind::kw_wchar_t:case token_kind::kw_char8_t:case token_kind::kw_char16_t:case token_kind::kw_char32_t:
        case token_kind::kw_short:case token_kind::kw_int:case token_kind::kw_long:case token_kind::kw_signed:case token_kind::kw_unsigned:case token_kind::kw_float:case token_kind::kw_double:return true;
        default:return kind()==token_kind::identifier && spelling()=="__int64";
        }
    }
    intrinsic_type intrinsic(){
        bool sign=false,uns=false,short_=false;unsigned longs=0;
        while(kind()==token_kind::kw_signed || kind()==token_kind::kw_unsigned || kind()==token_kind::kw_short || kind()==token_kind::kw_long){
            if(kind()==token_kind::kw_signed || kind()==token_kind::kw_unsigned){if(sign||uns)throw failure("Invalid signedness",{out.input.file,at()});sign=kind()==token_kind::kw_signed;uns=!sign;}
            else if(kind()==token_kind::kw_short){if(short_||longs)throw failure("Invalid short type",{out.input.file,at()});short_=true;}
            else {if(short_||longs==2)throw failure("Invalid long type",{out.input.file,at()});++longs;}advance();
        }
        if(kind()==token_kind::identifier && spelling()=="__int64"){if(short_||longs)throw failure("Invalid __int64 modifiers",{out.input.file,at()});advance();return uns?intrinsic_type::unsigned_long_long:intrinsic_type::signed_long_long;}
        if(kind()==token_kind::kw_char){if(short_||longs)throw failure("Invalid char modifiers",{out.input.file,at()});advance();return uns?intrinsic_type::unsigned_char:sign?intrinsic_type::signed_char:intrinsic_type::char_type;}
        if(kind()==token_kind::kw_double){if(sign||uns||short_||longs>1)throw failure("Invalid double modifiers",{out.input.file,at()});advance();return longs?intrinsic_type::long_double_type:intrinsic_type::double_type;}
        if(kind()==token_kind::kw_int)advance();
        else if(!sign&&!uns&&!short_&&!longs){auto k=kind();advance();switch(k){
            case token_kind::kw_void:return intrinsic_type::void_type;case token_kind::kw_bool:return intrinsic_type::bool_type;
            case token_kind::kw_wchar_t:return intrinsic_type::wchar_type;case token_kind::kw_char8_t:return intrinsic_type::char8_type;
            case token_kind::kw_char16_t:return intrinsic_type::char16_type;case token_kind::kw_char32_t:return intrinsic_type::char32_type;
            case token_kind::kw_float:return intrinsic_type::float_type;default:throw failure("Invalid intrinsic type",{out.input.file,at()});}}
        else if(builtin())throw failure("Invalid intrinsic combination",{out.input.file,at()});
        if(short_)return uns?intrinsic_type::unsigned_short:intrinsic_type::signed_short;
        if(longs==1)return uns?intrinsic_type::unsigned_long:intrinsic_type::signed_long;
        if(longs==2)return uns?intrinsic_type::unsigned_long_long:intrinsic_type::signed_long_long;
        return uns?intrinsic_type::unsigned_int:intrinsic_type::signed_int;
    }
    void emit(event e){e.local_order=static_cast<id>(out.events.size());out.events.push_back(e);}
    void body(id scope_id,bool nested,unsigned depth){
        if(depth>=256)throw failure("Source namespace depth",{out.input.file,at()});
        while(kind()!=token_kind::invalid && kind()!=token_kind::r_brace){
            if(kind()==token_kind::semicolon){advance();continue;}
            const auto start=at();
            if(kind()==token_kind::kw_namespace){advance();id selected=scope_id;
                for(;;){const auto loc=at();const auto n=name();out.scopes.push_back({selected,n,loc});selected=static_cast<id>(out.scopes.size()-1);
                    if(kind()!=token_kind::scope)break;advance();}
                emit({event_kind::namespace_scope,selected,none,none,none,none,0,0,start});
                take(token_kind::l_brace,"Expected namespace '{'");body(selected,true,depth+1);take(token_kind::r_brace,"Expected namespace '}'");continue;
            }
            bool ext=false;if(kind()==token_kind::kw_extern){ext=true;advance();}
            type_request tr;tr.at=start;
            while(kind()==token_kind::kw_const || kind()==token_kind::kw_volatile){tr.qualifiers|=kind()==token_kind::kw_const?1:2;advance();}
            id named=none;
            if(builtin())tr.builtin=intrinsic();else named=symbol_at(scope_id);
            if(named!=none && !ext && !tr.qualifiers && (kind()==token_kind::dot || kind()==token_kind::l_bracket || kind()==token_kind::assign)){
                const auto target=endpoint_after(named);take(token_kind::assign,"Expected '='");const auto v=rhs(scope_id);
                take(token_kind::semicolon,"Expected ';' after assignment");emit({event_kind::assign,scope_id,none,none,target,v,0,0,start});continue;
            }
            tr.named=named;
            if(kind()==token_kind::ampersand || kind()==token_kind::logical_and){tr.reference=kind()==token_kind::ampersand?1:2;advance();}
            const auto object_loc=at();const auto object_name=name();
            const auto cb=static_cast<id>(out.components.size());out.components.push_back({object_name,object_loc});
            out.symbols.push_back({scope_id,{cb,1},object_loc});const auto object=static_cast<id>(out.symbols.size()-1);
            tr.extents.begin=static_cast<id>(out.extents.size());
            while(kind()==token_kind::l_bracket){if(tr.reference)throw failure("Arrays of references unsupported",{out.input.file,at()});advance();const auto n=integer();
                if(!n || tr.extents.count>=32)throw failure("Invalid array extent",{out.input.file,at()});out.extents.push_back(n);++tr.extents.count;take(token_kind::r_bracket,"Expected array ']'");}
            id v=none;bool brace=false;
            if(kind()==token_kind::assign){advance();if(kind()==token_kind::l_brace){brace=true;advance();}if(kind()!=token_kind::r_brace)v=rhs(scope_id);}
            else if(kind()==token_kind::l_brace){brace=true;advance();if(kind()!=token_kind::r_brace)v=rhs(scope_id);}
            if(brace)take(token_kind::r_brace,"Expected initializer '}'");
            if(ext && v!=none)throw failure("Extern cannot have initializer",{out.input.file,start});
            take(token_kind::semicolon,"Expected ';' after declaration");
            tr.at={start.offset,consumed_end-start.offset};
            out.types.push_back(tr);emit({event_kind::declare,scope_id,object,static_cast<id>(out.types.size()-1),none,v,0,ext?1u:0u,object_loc});
        }
        if(!nested && kind()==token_kind::r_brace)throw failure("Unexpected '}'",{out.input.file,at()});
        if(nested && kind()==token_kind::invalid)throw failure("Unclosed namespace",{out.input.file,at()});
    }
public:
    extractor(task t,std::string_view s,std::span<const source_predecoded_token> input):tokens(input),source(s){out.input=t;}
    model run(){
        try{body(0,false,0);}catch(const failure& f){out.error=f.what();out.error_at=f.at.source;}
        return std::move(out);
    }
};
}
