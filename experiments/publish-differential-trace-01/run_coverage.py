"""Observe mismatches without silently treating skipped grammar as covered."""
import collections
import csv
import hashlib
import json
import pathlib
import subprocess
import sys

exe, output = (pathlib.Path(p).resolve() for p in sys.argv[1:])
cases = [
 ('control_scalar', 'constructor', ['struct A { int x; A():x(3){} };']),
 ('control_nested', 'nested', ['struct B { int x; }; struct A { B b; A(){b.x=2;} };']),
 ('control_error', 'constructor', ['struct A { int* x; A():x(1){} };']),
 ('member_equal', 'member_initializer', ['struct A { int x=1; };']),
 ('member_braces', 'member_initializer', ['struct A { int x{2}; };']),
 ('member_override', 'member_initializer', ['struct A { int x=1; A():x(3){} };']),
 ('member_forward_ref', 'member_initializer', ['struct A { int& r=x; int x; };']),
 ('member_wrong_type', 'member_initializer', ['struct A { int* x=3; };']),
 ('access', 'access', ['class A { int x; public: int y; protected: int z; };']),
 ('base_access', 'inheritance', ['struct B { int x; }; class A: public B { int y; };']),
 ('base_duplicate', 'inheritance', ['struct B {}; struct A: B,B {};']),
 ('union_base', 'inheritance', ['struct B {}; union A: B {};']),
 ('virtual_base', 'inheritance', ['struct B {}; struct A: virtual B {};']),
 ('incomplete_base', 'inheritance', ['struct B; struct A: B {};']),
 ('method_decl', 'callable', ['struct A { void f(); };']),
 ('method_body', 'callable', ['struct A { void f(){} };']),
 ('method_virtual', 'polymorphism', ['struct A { virtual void f(); };']),
 ('method_override_bad', 'polymorphism', ['struct A { void f() override; };']),
 ('method_override_base', 'polymorphism', ['struct B { virtual void f(); }; struct A: B { void f() override; };']),
 ('method_pure', 'polymorphism', ['struct A { virtual void f()=0; };']),
 ('destructor', 'callable', ['struct A { virtual ~A(); };']),
 ('conversion', 'callable', ['struct A { operator int() const; };']),
 ('named_operator', 'callable', ['struct A { int operator[](int); };']),
 ('bad_operator', 'callable', ['struct A { int operator[](); };']),
 ('multiple_members', 'declarator', ['struct A { int x,y; };']),
 ('duplicate_member', 'declarator', ['struct A { int x; int x; };']),
 ('invalid_intrinsic', 'intrinsic', ['struct A { unsigned float x; };']),
 ('alias', 'alias', ['typedef int T; struct A { T x; };']),
 ('void_alias', 'alias', ['typedef void V;']),
 ('derived_alias', 'alias', ['typedef int* P;']),
 ('duplicate_access', 'duplicate_metadata', ['class A { public: int x; };', 'class A { private: int x; };']),
 ('duplicate_virtual', 'duplicate_metadata', ['struct A { virtual void f(); int x; };', 'struct A { int x; };']),
 ('mixed_first', 'diagnostic_order', ['struct A { int* x=3; void f(){} };']),
 ('nested_string_order', 'string_order', ['struct B { int x; }; struct A { B b; A(){b.x=2;} };', 'struct Later { int after; };']),
 ('include_repeat', 'provenance', ['#include "common.inc"\nstruct A { B b; };', '#include "common.inc"\nstruct C { B b; };']),
 ('macro_context', 'preprocessing', ['struct B {};\n#define T B\nstruct A { T x; };\n#undef T\n']),
]
output.mkdir(parents=True, exist_ok=True)
rows=[]
for name, category, sources in cases:
    fixture=output/'fixtures'/name
    fixture.mkdir(parents=True, exist_ok=True)
    (fixture/'common.inc').write_text('#pragma once\nstruct B { int x; };\n')
    project={'version':1,'name':'differential','project':[], 'preprocessor':{'predefines':[]}}
    for i, source in enumerate(sources):
        file=f'{i}.h';(fixture/file).write_text(source)
        project['project'].append({'name':file,'type':'header','path':file})
    path=fixture/'project.json';path.write_text(json.dumps(project))
    signatures=[]
    production_traces=[]
    for workers in (1,2,4,8,16):
        for target in (1,32768):
            folder=output/'traces'/name/f'w{workers}-t{target}'
            result=subprocess.run([str(exe),str(path),str(folder),str(workers),str(target)],capture_output=True,text=True)
            folder.mkdir(parents=True,exist_ok=True)
            (folder/'stdout.txt').write_text(result.stdout);(folder/'stderr.txt').write_text(result.stderr)
            if result.returncode:
                raise RuntimeError((name,workers,target,result.returncode,result.stderr))
            row=dict(field.split('=',1) for field in result.stdout.strip().split(','))
            row.update(fixture=name,category=category,workers=workers,target=target)
            rows.append(row)
            signatures.append(tuple(row[k] for k in ('comparison','production_accepted','projection_accepted','gaps','excluded_callables','excluded_initializers')))
            production_traces.append(tuple(
                tuple(tuple(r.items()) for r in csv.DictReader((folder/file).open()) if r['side']=='production')
                for file in ('string-id-events.csv','type-id-events.csv','semantic-events.csv','provenance-events.csv')))
    assert len(set(signatures))==1,(name,signatures)
    assert len(set(production_traces))==1,(name,'production trace changed across configs')
    print(name, signatures[0], flush=True)

with (output/'coverage.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
summary={'fixtures':len(cases),'runs':len(rows),'comparisons':dict(collections.Counter(r['comparison'] for r in rows)),
 'full_header_parity':False,'not_emitted':['production string_id numbering','graph-local derived IDs','production SourceMap/reuse'],
 'metadata_not_represented':['member/base access','declares_virtual/polymorphic'],
 'note':'projection_parity validates existing type/value projection only; skipped callables and metadata are not covered',
 'exe_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
 'cases':[{'name':name,'category':category,'sources':sources} for name,category,sources in cases]}
(output/'coverage-summary.json').write_text(json.dumps(summary,indent=2))
for file in ('string-id-events.csv','type-id-events.csv','semantic-events.csv','provenance-events.csv','diagnostics.csv'):
    combined=[]
    for name,_,_ in cases:
        with (output/'traces'/name/'w8-t32768'/file).open() as f:
            combined.extend(dict(fixture=name,**r) for r in csv.DictReader(f))
    with (output/file).open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(combined[0]));writer.writeheader();writer.writerows(combined)
assert all(r['comparison']=='projection_parity' for r in rows if r['fixture'] in ('control_scalar','control_nested'))
assert all(r['comparison']=='exact_diagnostic' for r in rows if r['fixture']=='control_error')
assert any(r['comparison']=='acceptance_mismatch' for r in rows)
print(json.dumps({k:v for k,v in summary.items() if k!='cases'},indent=2))
