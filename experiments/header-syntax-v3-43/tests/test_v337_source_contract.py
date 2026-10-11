#!/usr/bin/env python3
from pathlib import Path
import hashlib
p=Path(__file__).resolve().parent.parent/'benchmarks'
b=(p/'v3_31_graph_producer.inc').read_bytes()
c=(p/'v3_37_graph_producer.inc').read_text(encoding='utf-8')
assert hashlib.sha256(b).hexdigest()=="97d3b813e120486fcd804e21f1d28fba1fab430ab057cee213625ef4c668333d",'Frozen V3-31 source changed'
assert 'std::vector<std::vector<std::uint32_t>> by_owner' not in c
assert 'std::vector<const syntax_declarator_08*> descriptors' not in c
assert c.count('for(std::size_t di=rec.declaration_first;di<declaration_end;++di)')==2
assert c.count('if(decl.owner!=ri){')==2
assert c.count('di=nested_end-1;')==2
assert c.count('std::lower_bound(')==1
assert c.count('descriptor_it!=descriptor_end && descriptor_it->declaration==di')==1
assert 'rec.declaration_count' in c
assert c.count('if(descriptor){')==1
# Syntax and Graph ABI must be unmodified in this isolated experiment
assert hashlib.sha256((p/'header_syntax_v3_06.hpp').read_bytes()).hexdigest()=='a0e0522b8c941dbaba70f99780adf62ed16b5d063ef73609fa286e1277dff5e0','Syntax Scanner was modified'
print('V337_SOURCE_CONTRACT_PASS frozen_v331_sha256='+hashlib.sha256(b).hexdigest())
