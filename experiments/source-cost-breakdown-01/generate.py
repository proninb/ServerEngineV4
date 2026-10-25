"""Instrument copies only. Balanced call wrappers preserve original return values."""
import hashlib
import json
import pathlib
import re
import sys

repo, output=(pathlib.Path(p).resolve() for p in sys.argv[1:])
output.mkdir(parents=True,exist_ok=True)
header=repo/'experiments/source-cost-breakdown-01/cost.hpp'
sources={}
def read(relative):
    path=repo/relative;sources[relative]=hashlib.sha256(path.read_bytes()).hexdigest()
    text=path.read_text(encoding='utf-8-sig')
    return re.sub(r'#include "([^"]+)"',lambda m:'#include "'+(path.parent/m[1]).resolve().as_posix()+'"',text)
def inject(text, signature, code):
    assert text.count(signature)==1,signature
    match=re.search(r'\bnoexcept\s*\{',text[text.index(signature):])
    assert match,signature
    pos=text.index(signature)+match.end()
    return text[:pos]+'\n'+code+'\n'+text[pos:]
def wrap_calls(text, spelling, bucket):
    # These selected API arguments contain no string literals/comments; fail
    # closed rather than attempting a general C++ source transformation.
    pattern=re.escape(spelling)+r'\s*\('
    matches=list(re.finditer(pattern,text))
    assert matches,spelling
    for match in reversed(matches):
        start=match.start();pos=match.end();depth=1
        while depth:
            c=text[pos];assert c not in ('"',"'"),spelling
            depth+=(c=='(')-(c==')');pos+=1
        original=text[start:pos]
        text=text[:start]+'scost01::call(scost01::'+bucket+',[&]() { return '+original+'; })'+text[pos:]
    return text

text=read('server_engine/project/parser/source_parser_v2.cpp')
resolver=read('server_engine/project/parser/source_endpoint_resolver.hpp')
for signature,bucket in [
    ('bool object_type(','object_lookup'),('server_status prepare_member(','member_transition'),
    ('server_status append_member(','member_transition'),('server_status append_index(','member_transition'),
    ('bool array_type(','member_transition')]:
    resolver=inject(resolver,signature,'scost01::scope cost_probe{scost01::'+bucket+'};')
(output/'source_endpoint_resolver.hpp').write_text('#include "'+header.as_posix()+'"\n'+resolver)
text=text.replace((repo/'server_engine/project/parser/source_endpoint_resolver.hpp').as_posix(),(output/'source_endpoint_resolver.hpp').as_posix())
for signature,bucket in [
    ('server_status advance()','cursor'),('server_status peek_kind(','cursor'),
    ('identity_ref find_visible(','binding'),('server_status read_endpoint(','endpoint_syntax'),
    ('server_status object(','declaration_syntax'),('server_status scalar(','scalar_syntax'),
    ('server_status assign_string(','string_syntax'),('server_status assignment(','assignment_syntax')]:
    text=inject(text,signature,'scost01::scope cost_probe{scost01::'+bucket+'};')
for call,bucket in [
    ('identities.resolve','binding'),
    ('G.intern_resolved_endpoint_path','path_intern'),
    ('G.add_resolved_object','graph_write'),('G.complete_resolved_extern_object','graph_write'),
    ('G.rebind_resolved_link','graph_write'),('G.add_resolved_initialization','graph_write'),
    ('G.add_resolved_initializations','graph_write'),
    ('sources.begin_root','provenance'),('sources.end_root','provenance'),
    ('sources.add_dependency','provenance'),('sources.add_initialization','provenance'),
    ('sources.add','provenance')]:
    text=wrap_calls(text,call,bucket)
text=inject(text,'server_status parse_source_roots_v2(',
    'scost01::session cost_session; scost01::scope cost_root{scost01::source};')
(output/'source_parser_v2.cpp').write_text('#include "'+header.as_posix()+'"\n'+text)
(output/'source-manifest.json').write_text(json.dumps(sources,indent=2))
