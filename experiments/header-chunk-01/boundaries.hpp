#pragma once
// Only structural discovery over an already effective token stream.
// No name lookup, type parsing, WHO, request records or G.
namespace hc01 {
using namespace sb01;
struct context {id root,parent;std::size_t name_token;event at;};
struct unit {id root,context;std::size_t first,last;}; // half-open original tokens
struct chunk {id root,context;std::size_t first,last,units;};
struct plan {
    std::vector<context> contexts;
    std::vector<unit> units;
    std::vector<id> fallback_roots;
    std::size_t tokens=0;
    std::size_t bytes()const{return contexts.capacity()*sizeof(context)+units.capacity()*sizeof(unit)+fallback_roots.capacity()*sizeof(id);}
};
inline K closer(K k){return k==K::l_brace?K::r_brace:k==K::l_paren?K::r_paren:K::r_bracket;}
class discovery {
    plan& out;const stream& input;id root;std::size_t at=0;
    std::vector<K> stack;
    K kind(std::size_t n=0)const{return at+n<input.tokens.size()?input.tokens[at+n].kind:K::invalid;}
    void scope(id parent,bool nested,unsigned depth=0){
        ca01::require(depth<=128,"namespace depth requires unsplit fallback");
        while(at<input.tokens.size()){
            if(kind()==K::r_brace){ca01::require(nested,"unexpected namespace close");++at;return;}
            if(kind()==K::semicolon){++at;continue;}
            if(kind()==K::kw_namespace){
                ca01::require(kind(1)==K::identifier&&kind(2)==K::l_brace,"unsupported namespace boundary");
                ca01::require(out.contexts.size()<none,"context limit");const auto ctx=static_cast<id>(out.contexts.size());
                out.contexts.push_back({root,parent,at+1,position(root,at+1)});at+=3;scope(ctx,true,depth+1);continue;
            }
            ca01::require(kind()==K::kw_struct||kind()==K::kw_class||kind()==K::kw_union||kind()==K::kw_typedef,"unsupported top-level boundary");
            const auto begin=at;stack.clear();bool finished=false;
            for(;at<input.tokens.size();++at){const auto k=kind();
                if(k==K::l_brace||k==K::l_paren||k==K::l_bracket)stack.push_back(closer(k));
                else if(k==K::r_brace||k==K::r_paren||k==K::r_bracket){ca01::require(!stack.empty()&&stack.back()==k,"unbalanced declaration");stack.pop_back();}
                else if(k==K::semicolon&&stack.empty()){++at;finished=true;break;}
            }
            ca01::require(finished,"unfinished declaration");out.units.push_back({root,parent,begin,at});
        }
        ca01::require(!nested,"unfinished namespace");
    }
public:
    discovery(plan& p,const stream& s,id r):out(p),input(s),root(r){}
    void run(){scope(none,false);}
};
inline plan discover(const std::vector<stream>& input){
    plan out;
    for(id root=0;root<input.size();++root){out.tokens+=input[root].tokens.size();
        const auto contexts=out.contexts.size(),units=out.units.size();
        try{discovery(out,input[root],root).run();}
        catch(const std::runtime_error&){
            // Fail closed: discard every tentative split of this root. The
            // existing Parser retains responsibility for its exact diagnostic.
            out.contexts.resize(contexts);out.units.resize(units);out.fallback_roots.push_back(root);
        }
    }return out;
}
inline std::vector<chunk> prepare(const plan& p,std::size_t target){
    ca01::require(target>0,"zero chunk target");std::vector<chunk> out;
    for(const auto& u:p.units){
        if(!out.empty()){auto& c=out.back();
            if(c.root==u.root&&c.context==u.context&&c.last==u.first&&u.last-c.first<=target){c.last=u.last;++c.units;continue;}}
        out.push_back({u.root,u.context,u.first,u.last,1});
    }return out;
}
}
