"""Generate instrumented copies; never edit production source files."""
import pathlib
import re
import sys
import hashlib
import json

repo, output = (pathlib.Path(x).resolve() for x in sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
trace = repo / 'experiments/publish-differential-trace-01/trace.hpp'
manifest={}

def inject(text, signature, code):
    assert text.count(signature)==1, ('instrumentation anchor changed', signature)
    begin = text.index(signature)
    pos = text.index('{', begin) + 1
    return text[:pos] + '\n' + code + '\n' + text[pos:]

def generate(relative, edits):
    source = repo / relative
    manifest[relative]=hashlib.sha256(source.read_bytes()).hexdigest()
    text = source.read_text(encoding='utf-8-sig')
    # Quoted engine includes must resolve exactly as in the original source.
    text = re.sub(r'#include "([^"]+)"', lambda m: '#include "' + (source.parent / m[1]).resolve().as_posix() + '"', text)
    for signature, code in edits:
        text = inject(text, signature, code)
    (output / source.name).write_text('#include "' + trace.as_posix() + '"\n' + text, encoding='utf-8')

generate('server_engine/project/string/string_table.cpp', [
 ('server_status string_table::intern_prehashed(', 'pd01::on_exit dt{[&]() noexcept { pd01::emit(pd01::strings,"intern",output.value(),0,0,output?get(output):std::string_view{}); }};')])
generate('server_engine/project/semantic/identity.cpp', [
 ('server_status identity_space::resolve(', 'pd01::on_exit dt{[&]() { pd01::emit(pd01::semantic,"identity_resolve",parent.value(),name.value(),output.value(),std::to_string(static_cast<unsigned>(kind))); }};')])
generate('server_engine/project/graph/graph.cpp', [
 ('server_status graph::derive_resolved(', 'pd01::on_exit dt{[&]() { pd01::emit(pd01::types,"derive",child.value(),static_cast<unsigned>(kind),output.value(),std::to_string(payload)); }};')])
generate('server_engine/project/frontend/preprocessor_v2.cpp', [
 ('server_status semantic_preprocessor_v2::record_reuse(', 'pd01::emit(pd01::provenance,"reuse_begin",static_cast<unsigned>(event),file.value(),value);\npd01::on_exit dt{[&]() noexcept { pd01::emit(pd01::provenance,"reuse_end",static_cast<unsigned>(event),file.value(),reused?*reused:0); }};')])
edits=[]
for name in ['parse_record','parse_namespace','parse_typedef','parse_member_declarator','parse_member_initializer','parse_method_tail','parse_named_operator','parse_constructor']:
    edits.append(('server_status header_parser_v2::'+name+'(',
        'const auto* dt_token=current(); pd01::emit(pd01::semantic,"'+name+'",dt_token?dt_token->file.value():0,dt_token?dt_token->source_offset:0,dt_token?dt_token->source_length:0);'))
edits.append(('server_status header_parser_v2::fail_at(', 'pd01::emit(pd01::semantic,"diagnostic",file.value(),source.offset,static_cast<unsigned>(kind),detail);'))
generate('server_engine/project/parser/header_parser_v2.cpp', edits)
generate('server_engine/project/source/source_map.cpp', [
 ('server_status source_map::begin_root(', 'pd01::emit(pd01::provenance,"source_begin_root",root.value());'),
 ('server_status source_map::add(\n', 'pd01::emit(pd01::provenance,"source_add",file.value(),data.raw());'),
 ('server_status source_map::add_dependency(\n    identity_ref', 'pd01::emit(pd01::provenance,"source_dependency",identity.value());'),
 ('server_status source_map::end_root(', 'pd01::emit(pd01::provenance,"source_end_root");')])

# Same experimental pipeline in this translation unit; retain original includes.
ep = repo / 'experiments/evaluator-parallel-01/main.cpp'
text = ep.read_text().replace('int main(int argc,char** argv)', 'int unused_ep01_cli(int argc,char** argv)')
text = re.sub(r'#include "([^"]+)"', lambda m: '#include "' + (ep.parent / m[1]).resolve().as_posix() + '"', text)
(output/'harness.hpp').write_text(text)

# Use the exact production reuse implementation rather than a new approximation.
parser = (repo/'server_engine/project/parser/parser.cpp').read_text()
first=parser.index('struct header_v2_semantic_reuse_cache final')
last=parser.index('struct parser_stage_timer final',first)
(output/'reuse.hpp').write_text('namespace cw::server {\n'+parser[first:last]+'\n}\n')
manifest['server_engine/project/parser/parser.cpp']=hashlib.sha256((repo/'server_engine/project/parser/parser.cpp').read_bytes()).hexdigest()
manifest['experiments/evaluator-parallel-01/main.cpp']=hashlib.sha256(ep.read_bytes()).hexdigest()
(output/'source-manifest.json').write_text(json.dumps(manifest,indent=2))
