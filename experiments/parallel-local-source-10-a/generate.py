"""PLS10-A: existing typed input, only the final-binding kernel changes."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);own=repo/'experiments/parallel-local-source-10-a'
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-09-b1/generate.py'),str(repo),str(out)],check=True)
def once(s,old,new):
 assert s.count(old)==1,(old,s.count(old));return s.replace(old,new)
for mode in ('native','audit'):
 d=out/mode;s=(d/'aliases.hpp').read_text()
 s=once(s,'#pragma once','#pragma once\n#include "'+(own/'binding_reduction.hpp').as_posix()+'"')
 s=once(s,'template<bool Indexed> class alias_linker {','template<bool Indexed,bool Finalize=false,bool SparseDiagnostics=false> class alias_linker {')
 s=once(s,'std::unordered_map<id,id> bindings;', '''std::conditional_t<Finalize,pls09b1::empty_state,std::unordered_map<id,id>> bindings;
    std::conditional_t<Finalize,pls10::binding_reduction,pls09b1::empty_state> final_catalog;''')
 start=s.index('    void link_bindings(){');end=s.index('    void reduce_writes()',start)
 old=s[start:end];comment=old.index('        // Detect duplicates after targets')
 diagnostics=old[comment:old.rfind('    }')]
 # Both kernels share the exact existing warning projection.
 legacy=old[:comment]+'        binding_diagnostics();\n    }\n'
 legacy=once(legacy,'void link_bindings(){','void link_bindings_legacy(){')
 compact=(own/'binding_body.inc').read_text()
 s=s[:start]+legacy+'    void binding_diagnostics(){\n'+diagnostics+'    }\n'+compact+'\n    void link_bindings(){if constexpr(Finalize)link_bindings_final();else link_bindings_legacy();}\n'+s[end:]
 # Only the Indexed follow specialization is used by the new kernel. Preserve
 # the complete legacy lookup body for the B1 control specialization.
 a=s.index('    id follow(');b=s.index('}else{',a)
 prefix=s[a:b]
 needle='''if(const auto it=(pls03::count(pls03::binding_lookups),bindings.find(c));it!=bindings.end())source=canonical(model.endpoints[model.bindings[it->second].source],true,depth+1);
        else if(const auto it=(pls03::count(pls03::binding_lookups),header_bindings.find(c));it!=header_bindings.end())source=canonical(model.endpoints[it->second],true,depth+1);
        else {++out.default_follows;const auto endpoint=default_source(c);source=canonical(endpoint,true,depth+1);}'''
 replacement='''if constexpr(Finalize){
            pls03::count(pls03::binding_lookups);const auto operation=final_catalog.find(c);
            if(operation!=none)source=canonical(model.endpoints[model.bindings[operation].source],true,depth+1);
            else if(const auto it=(pls03::count(pls03::binding_lookups),header_bindings.find(c));it!=header_bindings.end())source=canonical(model.endpoints[it->second],true,depth+1);
            else {++out.default_follows;const auto endpoint=default_source(c);source=canonical(endpoint,true,depth+1);}
        }else{'''+needle+'''}'''
 prefix=once(prefix,needle,replacement);s=s[:a]+prefix+s[b:];(d/'aliases.hpp').write_text(s)
 choice='(pls09b1::enabled()?pls02::alias_linker<true>(G,bound).run():pls02::alias_linker<false>(G,bound).run())'
 final='(pls10::sparse_diagnostics()?pls02::alias_linker<true,true,true>(G,bound).run():(pls10::enabled()?pls02::alias_linker<true,true>(G,bound).run():'+choice+'))'
 h=(d/'publish_source.hpp').read_text();h=once(h,choice,final);(d/'publish_source.hpp').write_text(h)
 if mode=='native':
  main=(d/'main.cpp').read_text();main=once(main,choice,final);(d/'main.cpp').write_text(main)
 main=(d/'publish_main.cpp').read_text();main='#include "'+(own/'binding_reduction.hpp').as_posix()+'"\n'+main
 main=once(main,"std::cout << '\\n';\n    pls08c::cold_check",'''std::cout<<",pls10_final="<<pls10::enabled()<<",pls10_direct_bindings="<<pls10::stats.direct_operations
      <<",pls10_indirect_bindings="<<pls10::stats.indirect_operations<<",pls10_context_rounds="<<pls10::stats.rounds
      <<",pls10_target_evaluations="<<pls10::stats.target_evaluations<<",pls10_catalog_bytes="<<pls10::stats.capacity_bytes
      <<",pls10_sparse="<<pls10::sparse_diagnostics()<<",pls10_diagnostic_pass_skipped="<<pls10::stats.diagnostic_pass_skipped;
    std::cout << '\\n';
    pls08c::cold_check''')
 (d/'publish_main.cpp').write_text(main)
