"""Only cache/visiting vary; catalogs, interning, traversal and output stay unchanged."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
own=repo/'experiments/parallel-local-source-09-b1'
def once(s,old,new):
 assert s.count(old)==1,(old,s.count(old));return s.replace(old,new)
for mode,parent in (('native','parallel-local-source-08'),('audit','parallel-local-source-09')):
 directory=out/mode
 subprocess.run([sys.executable,str(repo/'experiments'/parent/'generate.py'),str(repo),str(directory)],check=True)
 s=(directory/'aliases.hpp').read_text()
 s=once(s,'#pragma once','#pragma once\n#include "'+(own/'reference_state.hpp').as_posix()+'"\n#include <type_traits>')
 s=once(s,'class alias_linker {','template<bool Indexed> class alias_linker {')
 s=once(s,'std::unordered_map<id,id> bindings,cache,header_bindings;\n    std::unordered_set<id> visiting;', '''std::unordered_map<id,id> bindings;
    std::conditional_t<Indexed,pls09b1::empty_state,std::unordered_map<id,id>> cache;
    std::unordered_map<id,id> header_bindings;
    std::conditional_t<Indexed,pls09b1::empty_state,std::unordered_set<id>> visiting;
    std::conditional_t<Indexed,pls09b1::reference_state,pls09b1::empty_state> indexed_state;
    void clear_reference_state(){if constexpr(Indexed)indexed_state.clear();else {cache.clear();visiting.clear();}}''')
 start=s.index('    id follow(id c,unsigned depth){');end=s.index('    void select(',start)
 original=s[start:end];body=original[original.index('{')+1:original.rfind('}')]
 # Preserve the original legacy body verbatim, including diagnostic counters.
 indexed='''
        pls03::sample sample(pls03::follow_method);pls03::count(pls03::follow_calls);
        if(depth>256)fail("Reference chain too deep");
        id resolved;pls03::count(pls03::cache_lookups);
        if(indexed_state.find(c,resolved)){pls03::count(pls03::follow_hits);return resolved;}
        pls03::count(pls03::follow_misses);pls03::count(pls03::visiting_insert);
        if(!indexed_state.enter(c))fail("Reference cycle");
        ++out.reference_follows;id source;
        if(const auto it=(pls03::count(pls03::binding_lookups),bindings.find(c));it!=bindings.end())source=canonical(model.endpoints[model.bindings[it->second].source],true,depth+1);
        else if(const auto it=(pls03::count(pls03::binding_lookups),header_bindings.find(c));it!=header_bindings.end())source=canonical(model.endpoints[it->second],true,depth+1);
        else {++out.default_follows;const auto endpoint=default_source(c);source=canonical(endpoint,true,depth+1);}
        // Re-index after recursion: cell interning may have grown the vector.
        pls03::count(pls03::visiting_erase);indexed_state.leave(c);
        pls03::count(pls03::cache_store);indexed_state.store(c,source);return source;
'''
 s=s[:start]+'    id follow(id c,unsigned depth){if constexpr(Indexed){'+indexed+'\n    }else{'+body+'\n    }}\n'+s[end:]
 # Clear sites are paired in the baseline, including caught unresolved errors.
 assert s.count('cache.clear();visiting.clear();')==4
 # Keep the legacy implementation of the helper itself intact.
 token='else {cache.clear();visiting.clear();}'
 s=s.replace(token,'else {LEGACY_CLEAR_BODY}')
 s=s.replace('cache.clear();visiting.clear();','clear_reference_state();').replace('LEGACY_CLEAR_BODY','cache.clear();visiting.clear();')
 (directory/'aliases.hpp').write_text(s)
 hook=directory/'publish_source.hpp'
 if mode=='native':
  # Native PLS08 hook has absolute includes: relocate only aliases locally.
  import re
  origin=repo/'experiments/parallel-local-source-08'
  h=(origin/'publish_source.hpp').read_text()
  h=re.sub(r'#include "([^"]+)"',lambda m:m[0] if m[1]=='aliases.hpp' else '#include "'+(origin/m[1]).resolve().as_posix()+'"',h)
 else:h=hook.read_text()
 h=once(h,'pls02::alias_linker(G,bound).run()', '(pls09b1::enabled()?pls02::alias_linker<true>(G,bound).run():pls02::alias_linker<false>(G,bound).run())')
 hook.write_text(h)
 p=(directory/'publish_parser.cpp').read_text().replace((repo/'experiments/parallel-local-source-08/publish_source.hpp').as_posix(),hook.as_posix())
 (directory/'publish_parser.cpp').write_text(p)
 b=(directory/'publish_main.cpp').read_text()
 b='#include "'+(own/'reference_state.hpp').as_posix()+'"\n'+b
 b=once(b,"pls08c::print(std::cout);std::cout << '\\n';", "pls08c::print(std::cout);std::cout<<\",pls09_indexed=\"<<pls09b1::enabled();std::cout << '\\n';")
 (directory/'publish_main.cpp').write_text(b)
 if mode=='native':
  # Deep semantic fingerprint retains canonical cells, warning events/origins,
  # values and bindings; correctness only, never used as a PUBLISH timer.
  main=(directory/'main.cpp').read_text()
  main=once(main,'alias_linker(G,bound).run()', '(pls09b1::enabled()?pls02::alias_linker<true>(G,bound).run():pls02::alias_linker<false>(G,bound).run())')
  main=once(main,'int main(int argc,char** argv)', '''std::uint64_t numbering(const pls02::result& state,pls07::consumer_view data){
    std::uint64_t hash=14695981039346656037ULL;
    const auto add=[&](std::uint64_t value){for(unsigned i=0;i<8;++i){hash=(hash^static_cast<unsigned char>(value))*1099511628211ULL;value>>=8;}};
    add(state.cells.size());for(std::size_t i=0;i<state.cells.size();++i){const auto& c=state.cells[i];add(i);add(c.who.value());add(c.type.value());add(c.steps.count);
        for(const auto& step:std::span<const pls02::endpoint_path_step>{data.paths()}.subspan(c.steps.begin,c.steps.count)){add(static_cast<unsigned>(step.kind));add(step.value);}}
    return hash;
}
int main(int argc,char** argv)''')
  main=once(main,'const auto semantic_hash=fingerprint(final_state,retained);','const auto semantic_hash=fingerprint(final_state,retained);const auto numbering_hash=numbering(final_state,retained);')
  main=once(main,'pls08::json(report,graph_probe);','pls08::json(report,graph_probe);report<<",\\n\\\"canonical_numbering_fingerprint\\\":\\\""<<numbering_hash<<"\\\"";')
  (directory/'main.cpp').write_text(main)
