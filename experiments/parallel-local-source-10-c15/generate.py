"""Return the already-resolved declaration WHO; no lookup cache or index."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);own=Path(__file__).resolve().parent
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-10-c10/generate.py'),str(repo),str(out)],check=True)
def once(s,a,b):
 assert s.count(a)==1,(a,s.count(a));return s.replace(a,b)
for folder in (out,out/'native'):
 p=folder/'ordered_binder.hpp';s='#include "'+(own/'selector.hpp').as_posix()+'"\n'+p.read_text()
 s=once(s,'void declare(const event& e){','identity_ref declare(const event& e){')
 s=once(s,'    }\npublic:\n    ordered_committer','        return who;\n    }\npublic:\n    ordered_committer')
 s=once(s,'declare(e);const auto who=symbol_entity(e.symbol,identity_kind::object);', '''const auto registered=declare(e);const auto who=pls15::direct?registered:symbol_entity(e.symbol,identity_kind::object);
#ifdef PLS15_AUDIT
                ++pls15::declarations;if(pls15::direct)++pls15::removed;
                const auto& sym=local->symbols.at(e.symbol);const auto& use=local->components.at(sym.components.begin);identity_record record;
                const auto parent=e.scope==0?identities.root():scopes.at(e.scope);
                if(sym.components.count!=1||sym.scope!=e.scope||who.kind()!=identity_kind::object||!identities.record(who,record)||record.parent!=parent||record.name!=name_ids.at(use.name))fail("C15 declaration WHO invariant",e.at);
                ++pls15::verified;
#endif
                ''')
 p.write_text(s)
 p=folder/'publish_main.cpp';s='#include "'+(own/'selector.hpp').as_posix()+'"\n'+p.read_text();s=once(s,'pls10c10::print(std::cout);','pls10c10::print(std::cout);pls15::print(std::cout);');p.write_text(s)
