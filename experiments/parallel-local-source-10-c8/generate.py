"""One algorithmic difference: rebind's proven miss enters existing append tail."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);own=repo/'experiments/parallel-local-source-10-c8';parent=repo/'experiments/parallel-local-source-10-c3'
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-10-c7/generate.py'),str(repo),str(out)],check=True)
def once(s,a,b):
 assert s.count(a)==1,(a,s.count(a));return s.replace(a,b)
header='#include "'+(own/'selector.hpp').as_posix()+'"\n'
s=(repo/'server_engine/project/graph/graph.cpp').read_text()
def segment(s,signature,change):
 start=s.index(signature);opening=s.index('{',start);end=s.index('\n}',opening)+2
 return s[:start]+change(s[start:end])+s[end:]
add_start=s.index('server_status graph::add_resolved_link(')
add_end=s.index('\n}',s.index('{',add_start))+2
add=s[add_start:add_end]
tail=add[add.index('    if (links.size() >='):add.rfind('}')]
def rebind(p):
 p=once(p,'        replaced = true;','        replaced = true;pls10c8::replaced();')
 return once(p,'    return add_resolved_link(source, target, output);', '''    if (pls10c8::single) {
        // No intervening mutation/concurrency: the lookup above proved absence.
        // Do not retain its table cursor: capacity preparation may rehash.
        const auto append_known_absent = [&]() noexcept -> server_status {
'''+tail+'''
        };
        const auto result=append_known_absent();
        if(succeeded(result))pls10c8::inserted();
        return result;
    }
    const auto result=add_resolved_link(source, target, output);
    if(succeeded(result))pls10c8::inserted();
    return result;''')
s=segment(s,'server_status graph::rebind_resolved_link(',rebind)
# Audit-only counts compile away in the native target; no clocks are introduced.
def lookup(p):
 opening=p.index('{');body=p[opening+1:p.rfind('}')]
 return p[:opening+1]+'auto value=([&]() -> link_handle {'+body+'})();pls10c8::lookup(bool(value));return value;}'
s=segment(s,'link_handle graph::find_link_target(',lookup)
(out/'single_graph.cpp').write_text(header+s)
s=(parent/'publish_source.hpp').read_text();s=header+s;s=once(s,' try {',' try {pls10c8::observation source_observation;')
(out/'single_source.hpp').write_text(s)
s=(out/'publish_parser.cpp').read_text().replace((parent/'publish_source.hpp').as_posix(),(out/'single_source.hpp').as_posix());(out/'publish_parser.cpp').write_text(s)
s=(out/'publish_main.cpp').read_text();s=header+s;s=once(s,'pls10c3::print(std::cout);','pls10c8::print(std::cout);pls10c3::print(std::cout);');s=s.replace('PLS10C7.','PLS10C8.');(out/'publish_main.cpp').write_text(s)
fixture=(repo/'experiments/parallel-local-source-10-c1/feasibility.cpp').read_text();fixture=fixture[:fixture.index('void replacement()')].replace('#include "direct_commit.hpp"','#include "'+(repo/'experiments/parallel-local-source-10-c1/direct_commit.hpp').as_posix()+'"')
(out/'fixture.inc').write_text(fixture)
