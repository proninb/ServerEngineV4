from pathlib import Path
import hashlib,sys
p=Path(__file__).resolve().parent.parent/'benchmarks'
sha=lambda x:hashlib.sha256((p/x).read_bytes()).hexdigest()
checks={
 'v3_31_graph_producer.inc':'97d3b813e120486fcd804e21f1d28fba1fab430ab057cee213625ef4c668333d',
 'v3_37_graph_producer.inc':'7b122b66427bd6a5883835b1ef8f2f99d977b871cc62e1de544f05ab3c4c6579',
 'header_syntax_v3_06.hpp':'a0e0522b8c941dbaba70f99780adf62ed16b5d063ef73609fa286e1277dff5e0'
}
for file,expected in checks.items():
    if sha(file)!=expected: sys.exit(f'Frozen input changed: {file}')
s=(p/'v3_37r_graph_producer.inc').read_text()
if 'std::vector<std::vector<std::uint32_t>> by_owner' in s or 'std::vector<const syntax_declarator_08*> descriptors' in s:
    sys.exit('Dense owner/descriptor index reintroduced')
if s.count('v337r_ranges::next_after_foreign(')!=2:
    sys.exit('Nested skipping diverged')
if s.count('v337r_ranges::owned_declaration_count(')!=1:
    sys.exit('Exact reservation missing')
if 'events.reserve(ns.size()+parsed.syntax.records.size());' not in s:
    sys.exit('Over-reserve still present')
print('V337R_SOURCE_CONTRACT_PASS frozen_v337_sha256='+sha('v3_37_graph_producer.inc'))
