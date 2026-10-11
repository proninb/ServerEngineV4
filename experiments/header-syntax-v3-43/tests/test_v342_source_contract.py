#!/usr/bin/env python3
from pathlib import Path
import hashlib,sys
p=Path(__file__).resolve().parents[1]/'benchmarks'
checks={
 'v3_38a_graph_producer.inc':'d208d9617a7576efb4d535274671e807420578d20967f6678af9465d991451a9',
}
for name,want in checks.items():
 if hashlib.sha256((p/name).read_bytes()).hexdigest()!=want:sys.exit('V342 CONTROL MODIFIED: '+name)
a=(p/'v3_16_canonical.hpp').read_text()
b=(p/'v3_42_canonical.hpp').read_text().replace('namespace v342_canonical {','namespace v316 {').replace('struct layer { derived_type_kind kind; std::uint64_t payload; };','struct layer { derived_type_kind kind{}; std::uint64_t payload=0; };').replace('std::array<layer,32> layers; // inner to outer; only [0,length) is initialized by push()','std::array<layer,32> layers{}; // inner to outer')
if a!=b:sys.exit('V342 canonical clone contains unexpected changes')
for original,new,changes in [('v3_26_fast_type.hpp','v3_42_fast_type.hpp',[('v3_42_canonical.hpp','v3_16_canonical.hpp'),('namespace v342_type {','namespace v326_type {'),('v342_canonical::','v316::')]),('v3_31_scope_chain.hpp','v3_42_scope_chain.hpp',[('v3_42_fast_type.hpp','v3_26_fast_type.hpp'),('namespace v342_scope {','namespace v331_type {'),('v342_canonical::','v316::'),('v342_type::','v326_type::')]),('v3_38a_graph_producer.inc','v3_42_graph_producer.inc',[('v3_42_fast_type.hpp','v3_26_fast_type.hpp'),('v3_42_scope_chain.hpp','v3_31_scope_chain.hpp'),('v342_canonical::','v316::'),('v342_type::','v326_type::'),('v342_scope::','v331_type::')])]:
 content=(p/new).read_text()
 for x,y in changes:content=content.replace(x,y)
 if content!=(p/original).read_text():sys.exit('V342 non-isolated change '+new)
print('V342_SOURCE_CONTRACT_PASS')
