"""PLS10-B: typed producer writes to a direct-winner/sparse-pending sink."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);own=repo/'experiments/parallel-local-source-10-b'
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-10-a/generate.py'),str(repo),str(out)],check=True)
def once(s,old,new):
 assert s.count(old)==1,(old,s.count(old));return s.replace(old,new)
for mode in ('native','audit'):
 d=out/mode
 s=(d/'bind.hpp').read_text()
 s=once(s,'bool declaration=false;};','bool declaration=false;bool indirect=false;id sequence=none;};')
 s=once(s,'struct bound_model {','}\n#include "'+(own/'write_store.hpp').as_posix()+'"\nnamespace pls02 {\nstruct bound_model {')
 s=once(s,'std::vector<write> writes;','pls10b::write_store writes;')
 s=once(s,'writes.capacity()*sizeof(write)+','writes.capacity()*sizeof(write)+writes.extra_bytes()+')
 s=once(s,'identities(ids),resolver(g){}','identities(ids),resolver(g){out.writes.attach(out.endpoints,out.paths);}')
 (d/'bind.hpp').write_text(s)
 s=(d/'aliases.hpp').read_text()
 s=once(s,'bool SparseDiagnostics=false> class alias_linker','bool SparseDiagnostics=false,bool SparseWrites=false> class alias_linker')
 s=once(s,'pls03::flat_index flat;','std::conditional_t<SparseWrites,pls10b::registry,pls03::flat_index> flat;')
 s=once(s,'void reduce_writes(){','void reduce_writes_legacy(){')
 at=s.index('public:\n    alias_linker(')
 s=s[:at]+(own/'reduce_body.inc').read_text()+'\n    void reduce_writes(){if constexpr(SparseWrites)reduce_writes_sparse();else reduce_writes_legacy();}\n'+s[at:]
 s=once(s,'cell_equal{&input.paths}){}','cell_equal{&input.paths}),flat([&](){if constexpr(SparseWrites)return input.writes.take_index();else return pls03::flat_index{};}()){}')
 (d/'aliases.hpp').write_text(s)
 choice='(pls10::sparse_diagnostics()?pls02::alias_linker<true,true,true>(G,bound).run():'
 prefix='(pls10b::enabled()?pls02::alias_linker<true,true,true,true>(G,bound).run():'
 for name in ('publish_source.hpp','main.cpp') if mode=='native' else ('publish_source.hpp',):
  s=(d/name).read_text();i=s.index(choice);s=s[:i]+prefix+s[i:]
  old='pls02::alias_linker<false>(G,bound).run()))'
  s=once(s,old,old+')');(d/name).write_text(s)
 s=(d/'publish_main.cpp').read_text();s='#include "'+(own/'selector.hpp').as_posix()+'"\n'+s
 s=once(s,"std::cout << '\\n';\n    pls08c::cold_check",'''std::cout<<",pls10_writes="<<pls10b::enabled()<<",pls10_write_ops="<<pls10b::stats.operations
      <<",pls10_direct_ops="<<pls10b::stats.direct<<",pls10_pending_ops="<<pls10b::stats.indirect
      <<",pls10_direct_winners="<<pls10b::stats.direct_winners<<",pls10_early_losers="<<pls10b::stats.early_losers
      <<",pls10_write_rows_before="<<pls10b::stats.retained_before<<",pls10_write_rows_after="<<pls10b::stats.retained_after
      <<",pls10_conflict_origins="<<pls10b::stats.conflict_origins;
    std::cout << '\\n';
    pls08c::cold_check''');(d/'publish_main.cpp').write_text(s)
 heads=(repo/'experiments/parallel-local-source-06/heads.hpp').read_text()
 heads=once(heads,'    template<class F>void each(','''    std::uint32_t get(std::uint32_t cell)const{
        if(dense)return cell<values.size()?values[cell]:invalid;
        const auto found=map.find(cell);return found==map.end()?invalid:found->second;
    }
    void set(std::uint32_t cell,std::uint32_t row){(void)exchange(cell,row);}
    template<class F>void each(''')
 (d/'heads.hpp').write_text(heads)
 s=(d/'aliases.hpp').read_text().replace((repo/'experiments/parallel-local-source-06/heads.hpp').as_posix(),(d/'heads.hpp').as_posix());(d/'aliases.hpp').write_text(s)
 ownership=(repo/'experiments/parallel-local-source-07/ownership.hpp').read_text()
 ownership=once(ownership,'writes(b.writes.size())','writes(b.writes.operation_count())')
 (d/'ownership.hpp').write_text(ownership)
 s=(d/'aliases.hpp').read_text().replace((repo/'experiments/parallel-local-source-07/ownership.hpp').as_posix(),(d/'ownership.hpp').as_posix());(d/'aliases.hpp').write_text(s)
 for name in ('main.cpp','publish_source.hpp') if mode=='native' else ('publish_source.hpp',):
  s=(d/name).read_text().replace((repo/'experiments/parallel-local-source-07/ownership.hpp').as_posix(),(d/'ownership.hpp').as_posix());(d/name).write_text(s)
 if mode=='audit':
  # Measure Source extraction + typed production + alias finalization together:
  # direct-key storage now starts before alias_ms and must not disappear from
  # allocation/peak-live accounting merely because its lifetime moved earlier.
  probe=(d/'probe.hpp').read_text()
  probe=once(probe,'off,setup,header,','off,source_extraction,source_linking,setup,header,')
  probe=once(probe,'{"off","alias_setup",','{"off","source_extraction","source_linking","alias_setup",')
  probe=once(probe,'template<class B> void begin(const B& b){','''inline bool source_observation=false;
inline void start_source(){
    stages={};samples={};requests.clear();total_live=total_peak=total_calls=total_bytes=flat_capacity_bytes=0;
    running=true;active=source_extraction;changed=clock::now();source_observation=true;
}
template<class B> void begin(const B& b){
    if(source_observation){
        {pause disabled;requests.reserve(b.bindings.size()*4+b.writes.size()*2+1024);}
        boundary(setup);return;
    }''')
  (d/'probe.hpp').write_text(probe)
  hook=(d/'publish_source.hpp').read_text()
  hook=once(hook,'at=clock::now();std::vector<pls02::model> locals(tasks.size());','pls03::start_source();at=clock::now();std::vector<pls02::model> locals(tasks.size());')
  hook=once(hook,'at=clock::now();auto bound=pls02::semantic_linker','pls03::boundary(pls03::source_linking);at=clock::now();auto bound=pls02::semantic_linker')
  (d/'publish_source.hpp').write_text(hook)
