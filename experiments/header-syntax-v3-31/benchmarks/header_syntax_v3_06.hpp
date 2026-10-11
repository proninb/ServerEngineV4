#pragma once
// HEADER-SYNTAX-FAST-06: syntax-only physical token-kind classifier.
// Deliberately no semantic identity, Graph, string interning, or persistence.
// A token-kind-only view cannot distinguish a constructor from other identifier
// methods reliably: these declarations are reported AMBIGUOUS, not validated.
#include "server_engine/project/frontend/lexical_token.hpp"
#include <cstdint>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace cw::server {

enum class syntax_decl_03 : std::uint8_t {
    field_candidate, method_candidate, ambiguous_callable,
    record, forward_record, access, typedef_decl, unsupported
};

struct syntax_record_03 {
    std::uint32_t begin, end, parent, declaration_first, declaration_count;
    std::uint32_t base_begin, base_end; // half-open range, empty when absent
    token_kind kind;
    bool forward;
};
struct syntax_declaration_03 {
    std::uint32_t begin, end, owner;
    syntax_decl_03 kind;
};
enum class syntax_reason_04 : std::uint8_t {
    root_semicolon_declaration, root_braced_declaration, root_unterminated,
    member_using, member_static_assert, member_friend, member_template,
    member_enum, member_unrecognized, namespace_semicolon,
    count
};
struct syntax_event_04 {
    syntax_reason_04 reason;
    std::uint32_t begin, end;
};
struct syntax_diagnostics_04 {
    static constexpr std::size_t reason_count = static_cast<std::size_t>(syntax_reason_04::count);
    std::uint64_t counts[reason_count]{};
    std::vector<syntax_event_04> examples;
    void add(syntax_reason_04 reason, std::size_t begin, std::size_t end) {
        const auto index = static_cast<std::size_t>(reason);
        ++counts[index];
        // Bounded sampling: retain at most 8 examples per reason.
        if (counts[index] <= 8)
            examples.push_back({reason, static_cast<std::uint32_t>(begin),
                                static_cast<std::uint32_t>(end)});
    }
};
struct syntax_stats_03 {
    std::uint64_t tokens = 0, namespaces = 0, record_definitions = 0;
    std::uint64_t forward_records = 0, field_candidates = 0;
    std::uint64_t method_candidates = 0, ambiguous_callables = 0;
    std::uint64_t access_labels = 0, typedef_declarations = 0;
    std::uint64_t ignored_directive_tokens = 0;
    std::uint64_t inherited_records = 0;
    std::uint64_t skipped = 0, unsupported = 0, malformed = 0;
    std::uint64_t structural_visits = 0, declarator_visits = 0;
    std::uint64_t extraction_probe_checksum = 0;
};
// FAST-07: ranges refer to the same per-file filtered token_kind array.
// No resolved types, names, identities or Graph handles are stored.
struct syntax_declarator_08 final {
    // type_begin = declarations[declaration].begin;
    // initializer_end = declaration.end minus a terminal semicolon.
    // These endpoints are intentionally not persisted twice.
    std::uint32_t declaration = 0;
    std::uint32_t type_end = 0;
    std::uint32_t name = 0xffffffffu;
    std::uint32_t initializer_begin = 0;
    std::uint16_t pointers = 0, references = 0, arrays = 0;
    std::uint16_t flags = 0; // 1=field, 2=callable, 4=unresolved
};
static_assert(sizeof(syntax_declarator_08) == 24);

struct syntax_result_03 {
    std::vector<syntax_record_03> records;
    std::vector<syntax_declaration_03> declarations;
    std::vector<syntax_declarator_08> declarators;
    syntax_stats_03 stats{};
    syntax_diagnostics_04* diagnostics = nullptr;
    void clear() noexcept { records.clear(); declarations.clear(); declarators.clear(); stats = {}; }
    std::size_t model_bytes() const noexcept {
        return records.size()*sizeof(syntax_record_03) +
               declarations.size()*sizeof(syntax_declaration_03) +
               declarators.size()*sizeof(syntax_declarator_08);
    }
};

class header_syntax_v3_06 {
    using tokens = std::span<const token_kind>;
    static constexpr std::uint32_t no_parent = 0xffffffffu;
    struct state { tokens t; syntax_result_03& result; };
    static void report(state& s, syntax_reason_04 reason,
                       std::size_t begin, std::size_t end) {
        if (s.result.diagnostics) s.result.diagnostics->add(reason, begin, end);
    }
    static bool record_keyword(token_kind k) noexcept {
        return k==token_kind::kw_class || k==token_kind::kw_struct || k==token_kind::kw_union;
    }
    static bool access_keyword(token_kind k) noexcept {
        return k==token_kind::kw_public || k==token_kind::kw_private || k==token_kind::kw_protected;
    }
    static std::size_t close_match(tokens t,std::size_t start,std::size_t limit,
                                   token_kind left,token_kind right) noexcept {
        unsigned depth=0;
        for(auto i=start;i<limit;++i) {
            if(t[i]==left) ++depth;
            else if(t[i]==right && (!depth || --depth==0)) return depth?limit:i;
        }
        return limit;
    }
    static void append_decl(state& s,std::size_t begin,std::size_t end,
                            std::uint32_t owner,syntax_decl_03 kind) {
        s.result.declarations.push_back({static_cast<std::uint32_t>(begin),
            static_cast<std::uint32_t>(end),owner,kind});
        auto& st=s.result.stats;
        switch(kind) {
            case syntax_decl_03::field_candidate:++st.field_candidates;break;
            case syntax_decl_03::method_candidate:++st.method_candidates;break;
            case syntax_decl_03::ambiguous_callable:++st.ambiguous_callables;break;
            case syntax_decl_03::access:++st.access_labels;break;
            case syntax_decl_03::typedef_decl:++st.typedef_declarations;break;
            case syntax_decl_03::unsupported:++st.unsupported;break;
            default:break;
        }
    }
    // Find one whole member declaration, respecting balanced delimiters.
    // This is a structural classifier, NOT the complete Header V2 grammar.
    static bool member(state& s,std::size_t& pos,std::size_t limit,std::uint32_t owner) {
        const auto start=pos;
        auto& t=s.t;
        if(access_keyword(t[pos]) && pos+1<limit && t[pos+1]==token_kind::colon) {
            pos+=2;append_decl(s,start,pos,owner,syntax_decl_03::access);return true;
        }
        // Fused extraction: collect declarator state during the structural walk.
        // No per-member second token traversal.
        const std::size_t end_unknown = limit;
        std::size_t fused_name=end_unknown;
        std::size_t fused_initializer=end_unknown;
        std::uint16_t fused_pointers=0, fused_references=0, fused_arrays=0;
        bool previous_was_identifier=false;
        bool extraction_finished=false;
        // Structural depths are also the extraction depths until initializer/body.
        // Do not maintain a second fp/fb/fc state machine on the common path.
        unsigned paren=0,bracket=0,brace=0;
        bool callable=false, has_equal=false, has_brace=false, method_body=false;
        bool malformed=false;
        for(;pos<limit;++pos) {
            ++s.result.stats.structural_visits;
            auto k=t[pos];
            // Exactly one nesting state machine. Extraction observes the depth
            // BEFORE the structural state consumes the current token.
            if(!extraction_finished && k!=token_kind::semicolon) {
                if((k==token_kind::assign || k==token_kind::l_brace) &&
                   !paren && !bracket && !brace) {
                    fused_initializer=pos;
                    extraction_finished=true;
                } else {
                    if(k==token_kind::identifier && !paren && !bracket && !brace) {
                        if(previous_was_identifier) fused_name=pos;
                        if(pos+1==limit) {
                            fused_name=pos;
                        } else {
                            switch(t[pos+1]) {
                                case token_kind::l_bracket:
                                case token_kind::l_paren:
                                case token_kind::comma:
                                case token_kind::semicolon:
                                case token_kind::assign:
                                case token_kind::l_brace:
                                    fused_name=pos; break;
                                default: break;
                            }
                        }
                    }
                    if(k==token_kind::star && !paren && !bracket && !brace) ++fused_pointers;
                    if((k==token_kind::ampersand || k==token_kind::logical_and) &&
                       !paren && !bracket && !brace) ++fused_references;
                    if(k==token_kind::l_bracket && !paren && !brace &&
                       fused_name!=end_unknown) ++fused_arrays;
                    previous_was_identifier=(k==token_kind::identifier);
                }
            }
            if(k==token_kind::l_paren) {if(!bracket && !brace)callable=true;++paren;}
            else if(k==token_kind::r_paren) {if(!paren){malformed=true;break;}--paren;}
            else if(k==token_kind::l_bracket) ++bracket;
            else if(k==token_kind::r_bracket) {if(!bracket){malformed=true;break;}--bracket;}
            else if(k==token_kind::assign && !paren && !bracket && !brace) has_equal=true;
            else if(k==token_kind::l_brace) {
                if(!paren && !bracket && !brace && callable && !has_equal) {
                    // Could be a method body or a braced initializer; conservative.
                    method_body=true;
                }
                ++brace;has_brace=true;
            } else if(k==token_kind::r_brace) {
                if(!brace){malformed=true;break;}
                --brace;
                if(method_body && !paren && !bracket && !brace) {++pos;break;}
            }
            if(k==token_kind::semicolon && !paren && !bracket && !brace) {++pos;break;}
        }
        if(malformed || paren || bracket || brace || pos<=start || pos>limit ||
           (pos==limit && t[pos-1]!=token_kind::semicolon && !method_body)) {
            ++s.result.stats.malformed;
            return false;
        }
        syntax_decl_03 kind;
        if(t[start]==token_kind::kw_typedef)kind=syntax_decl_03::typedef_decl;
        else if(t[start]==token_kind::kw_using || t[start]==token_kind::kw_static_assert ||
                t[start]==token_kind::kw_friend || t[start]==token_kind::kw_template ||
                t[start]==token_kind::kw_enum) {
            kind=syntax_decl_03::unsupported;
            const auto reason=t[start]==token_kind::kw_using ? syntax_reason_04::member_using :
                t[start]==token_kind::kw_static_assert ? syntax_reason_04::member_static_assert :
                t[start]==token_kind::kw_friend ? syntax_reason_04::member_friend :
                t[start]==token_kind::kw_template ? syntax_reason_04::member_template :
                syntax_reason_04::member_enum;
            report(s,reason,start,pos);
        }
        else if(callable)kind=method_body ? syntax_decl_03::method_candidate
                                         : syntax_decl_03::ambiguous_callable;
        else if(has_brace || has_equal || t[start]==token_kind::identifier ||
                t[start]==token_kind::kw_const || t[start]==token_kind::kw_int ||
                t[start]==token_kind::kw_bool || t[start]==token_kind::kw_char ||
                t[start]==token_kind::kw_float || t[start]==token_kind::kw_double ||
                t[start]==token_kind::kw_unsigned || t[start]==token_kind::kw_long ||
                t[start]==token_kind::kw_short || t[start]==token_kind::kw_volatile ||
                t[start]==token_kind::kw_void || t[start]==token_kind::kw_wchar_t ||
                t[start]==token_kind::kw_char8_t || t[start]==token_kind::kw_char16_t ||
                t[start]==token_kind::kw_char32_t || t[start]==token_kind::kw_signed)
            kind=syntax_decl_03::field_candidate;
        else {
            kind=syntax_decl_03::unsupported;
            report(s,syntax_reason_04::member_unrecognized,start,pos);
        }
        append_decl(s,start,pos,owner,kind);
        if(kind==syntax_decl_03::field_candidate ||
           kind==syntax_decl_03::method_candidate ||
           kind==syntax_decl_03::ambiguous_callable) {
            syntax_declarator_08 d{};
            d.declaration=static_cast<std::uint32_t>(s.result.declarations.size()-1);
            d.type_end=static_cast<std::uint32_t>(start);
            d.name=0xffffffffu;
            d.initializer_begin=static_cast<std::uint32_t>(
                fused_initializer==end_unknown ? pos : fused_initializer);
            d.flags=kind==syntax_decl_03::field_candidate ? 1u : 2u;
            const auto terminal=pos>start && t[pos-1]==token_kind::semicolon ? pos-1 : pos;
            if(start+5<terminal && t[start+1]==token_kind::l_paren &&
               (t[start+2]==token_kind::ampersand || t[start+2]==token_kind::star ||
                t[start+2]==token_kind::logical_and) &&
               t[start+3]==token_kind::identifier && t[start+4]==token_kind::r_paren &&
               t[start+5]==token_kind::l_bracket) {
                d.name=static_cast<std::uint32_t>(start+3);
                d.type_end=static_cast<std::uint32_t>(start+1);
                d.pointers=t[start+2]==token_kind::star ? 1u : 0u;
                d.references=t[start+2]==token_kind::star ? 0u : 1u;
                d.arrays=fused_arrays ? fused_arrays : 1u;
                d.flags|=8u;
            } else if(fused_name>start && fused_name<terminal) {
                d.name=static_cast<std::uint32_t>(fused_name);
                auto te=fused_name;
                while(te>start && (t[te-1]==token_kind::star ||
                    t[te-1]==token_kind::ampersand ||
                    t[te-1]==token_kind::logical_and)) --te;
                d.type_end=static_cast<std::uint32_t>(te);
                d.pointers=fused_pointers;
                d.references=fused_references;
                d.arrays=fused_arrays;
            }
            if(d.name==0xffffffffu || d.type_end==start) d.flags|=4u;
            s.result.declarators.push_back(d);
        }
        return true;
    }
    static bool scope(state& s,std::size_t begin,std::size_t end,std::uint32_t parent,
                      unsigned depth) {
        if(depth>256){++s.result.stats.malformed;return false;}
        auto& t=s.t;
        for(auto pos=begin;pos<end;) {
            const auto k=t[pos];
            if(k==token_kind::semicolon){++pos;continue;}
            if(record_keyword(k)) {
                if(!record(s,pos,end,parent,depth+1))return false;
            } else if(k==token_kind::kw_namespace) {
                auto j=pos+1;
                while(j<end && t[j]!=token_kind::l_brace && t[j]!=token_kind::semicolon)++j;
                if(j==end){++s.result.stats.malformed;return false;}
                if(t[j]==token_kind::semicolon){report(s,syntax_reason_04::namespace_semicolon,pos,j+1);pos=j+1;++s.result.stats.skipped;continue;}
                const auto close=close_match(t,j,end,token_kind::l_brace,token_kind::r_brace);
                if(close==end){++s.result.stats.malformed;return false;}
                ++s.result.stats.namespaces;
                if(!scope(s,j+1,close,parent,depth+1))return false;
                pos=close+1;
            } else if(parent!=no_parent) {
                if(!member(s,pos,end,parent))return false;
            } else if(k==token_kind::kw_typedef) {
                auto j=pos+1;
                while(j<end && t[j]!=token_kind::semicolon &&
                      t[j]!=token_kind::l_brace)++j;
                if(j==end || t[j]!=token_kind::semicolon) {
                    report(s,syntax_reason_04::root_unterminated,pos,end);
                    ++s.result.stats.malformed;return false;
                }
                append_decl(s,pos,j+1,no_parent,syntax_decl_03::typedef_decl);
                pos=j+1;
            } else {
                // Outside records, unsupported forms are counted explicitly.
                auto j=pos;
                while(j<end && t[j]!=token_kind::semicolon && t[j]!=token_kind::l_brace)++j;
                if(j==end){report(s,syntax_reason_04::root_unterminated,pos,end);++s.result.stats.skipped;break;}
                if(t[j]==token_kind::l_brace){
                    auto c=close_match(t,j,end,token_kind::l_brace,token_kind::r_brace);
                    if(c==end){++s.result.stats.malformed;return false;}
                    pos=c+1;
                    report(s,syntax_reason_04::root_braced_declaration,j,c+1);
                } else {
                    report(s,syntax_reason_04::root_semicolon_declaration,pos,j+1);
                    pos=j+1;
                }
                ++s.result.stats.unsupported;
            }
        }
        return true;
    }
    static bool record(state& s,std::size_t& pos,std::size_t end,std::uint32_t parent,
                       unsigned depth) {
        auto& t=s.t;
        const auto start=pos;
        auto j=pos+1;
        // Ignore base-list names, but don't confuse balanced parentheses with a body.
        while(j<end && t[j]!=token_kind::l_brace && t[j]!=token_kind::semicolon)++j;
        if(j==end){++s.result.stats.malformed;return false;}
        // Preserve the base-clause token range, without resolving its type.
        // A colon inside other delimiters is not an inheritance clause.
        std::size_t base_begin=j, base_end=j;
        unsigned paren=0, bracket=0;
        for(auto k=start+1;k<j;++k) {
            ++s.result.stats.structural_visits;
            if(t[k]==token_kind::l_paren)++paren;
            else if(t[k]==token_kind::r_paren && paren)--paren;
            else if(t[k]==token_kind::l_bracket)++bracket;
            else if(t[k]==token_kind::r_bracket && bracket)--bracket;
            else if(t[k]==token_kind::colon && !paren && !bracket) {
                base_begin=k+1; base_end=j; break;
            }
        }
        if(base_begin<base_end)++s.result.stats.inherited_records;
        const auto owner=static_cast<std::uint32_t>(s.result.records.size());
        if(t[j]==token_kind::semicolon) {
            s.result.records.push_back({static_cast<std::uint32_t>(start),
                static_cast<std::uint32_t>(j+1),parent,
                static_cast<std::uint32_t>(s.result.declarations.size()),0,
                static_cast<std::uint32_t>(base_begin),static_cast<std::uint32_t>(base_end),t[start],true});
            ++s.result.stats.forward_records;pos=j+1;return true;
        }
        const auto close=close_match(t,j,end,token_kind::l_brace,token_kind::r_brace);
        if(close==end){++s.result.stats.malformed;return false;}
        s.result.records.push_back({static_cast<std::uint32_t>(start),
            static_cast<std::uint32_t>(close+1),parent,
            static_cast<std::uint32_t>(s.result.declarations.size()),0,
            static_cast<std::uint32_t>(base_begin),static_cast<std::uint32_t>(base_end),t[start],false});
        ++s.result.stats.record_definitions;
        if(!scope(s,j+1,close,owner,depth+1))return false;
        auto& r=s.result.records[owner];
        // A range is for traversal, not an assertion that nested descriptors belong to owner.
        r.declaration_count=static_cast<std::uint32_t>(s.result.declarations.size()-r.declaration_first);
        pos=close+1;
        if(pos<end && t[pos]==token_kind::semicolon)++pos;
        return true;
    }
public:
    static bool scan(std::span<const token_kind> input,syntax_result_03& out) {
        out.clear();out.stats.tokens=input.size();
        if(input.size()>std::numeric_limits<std::uint32_t>::max())return false;
        state s{input,out};
        return scope(s,0,input.size(),no_parent,0);
    }
};
}
