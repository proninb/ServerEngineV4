#!/usr/bin/env python3
"""Build benchmark-only V3-24 source variant. Never edits the user repository."""
from pathlib import Path
import argparse, shutil, re

HERE=Path(__file__).resolve().parent.parent

def once(text,old,new,label):
    number=text.count(old)
    if number!=1: raise RuntimeError(f'{label}: expected 1 anchor, found {number}')
    return text.replace(old,new,1)

def producer_v324():
    file=HERE/'benchmarks/v3_21_graph_producer.inc'
    s=file.read_text(encoding='utf-8')
    s=once(s,'            std::vector<member_record> fields;\n            std::vector<construction_value> values;', '''#ifdef CW_V324_DIRECT_GRAPH_WRITE
            const auto record_handle=parsed.handles[ri];
            if(!succeeded(out.G.begin_direct_record(record_handle,by_owner[ri].size()))){
                reject(out,root,rec.begin,"direct_begin_failed");continue;
            }
            std::size_t field_count=0;
#else
            std::vector<member_record> fields;
            std::vector<construction_value> values;
#endif''','field declarations')
    s=once(s,'            fields.reserve(by_owner[ri].size());\n            values.reserve(by_owner[ri].size());', '''#ifndef CW_V324_DIRECT_GRAPH_WRITE
            fields.reserve(by_owner[ri].size());
            values.reserve(by_owner[ri].size());
#endif''','reserves')
    anchor='            graph_member_access access=initial_access(rec.kind);'
    s=once(s,anchor,'''            const auto size_fields=[&]()->std::size_t {
#ifdef CW_V324_DIRECT_GRAPH_WRITE
                return field_count;
#else
                return fields.size();
#endif
            };
            const auto field_type=[&](std::size_t i)->type_ref {
#ifdef CW_V324_DIRECT_GRAPH_WRITE
                const auto* member=out.G.direct_record_member(record_handle,i);
                return member?member->type:type_ref{};
#else
                return i<fields.size()?fields[i].type:type_ref{};
#endif
            };
            const auto push_field=[&](member_record m,construction_value v)->bool {
#ifdef CW_V324_DIRECT_GRAPH_WRITE
                if(!succeeded(out.G.append_direct_member(record_handle,m,v)))return false;
                ++field_count;return true;
#else
                fields.push_back(m);values.push_back(v);return true;
#endif
            };
            const auto set_construction=[&](std::size_t i,construction_value v)->bool {
#ifdef CW_V324_DIRECT_GRAPH_WRITE
                return succeeded(out.G.set_direct_construction(record_handle,i,v));
#else
                if(i>=values.size())return false;
                values[i]=v;return true;
#endif
            };
'''+anchor,'record access')
    s=once(s,'''                    if(!register_member(name.value(),fields.size())){
                        reject(out,root,d.name,"field_index_insert_failed");continue;
                    }
                    fields.push_back({name,type,access,{}});
                    const auto decoded=v318::parse_field(root.effective,d.initializer_begin,decl.end,
                        [&](std::size_t i){return symbol(root,i);},
                        [&](std::size_t i){return decoded_number(root,i);});
                    if(!decoded.complete){reject(out,root,d.name,"field_init_"+decoded.reason);values.push_back({});continue;}
                    values.push_back(constant(decoded.value));
                    if(decoded.value.kind==v317::operand_kind::member_reference){
                        reference_fields.emplace_back(fields.size()-1,decoded.value.reference);
                        values.back()={};
                    }
                    ++out.fields;''','''                    const auto current_index=size_fields();
                    if(!register_member(name.value(),current_index)){
                        reject(out,root,d.name,"field_index_insert_failed");continue;
                    }
                    if(!push_field({name,type,access,{}},{})){
                        reject(out,root,d.name,"direct_member_append_failed");continue;
                    }
                    const auto decoded=v318::parse_field(root.effective,d.initializer_begin,decl.end,
                        [&](std::size_t i){return symbol(root,i);},
                        [&](std::size_t i){return decoded_number(root,i);});
                    if(!decoded.complete){reject(out,root,d.name,"field_init_"+decoded.reason);continue;}
                    if(decoded.value.kind==v317::operand_kind::member_reference){
                        reference_fields.emplace_back(current_index,decoded.value.reference);
                    }else if(!set_construction(current_index,constant(decoded.value))){
                        reject(out,root,d.name,"field_construction_failed");continue;
                    }
                    ++out.fields;''','field insert and value')
    s=once(s,'if(source_index==no_member||to>=fields.size())return false;',
           'if(source_index==no_member||to>=size_fields())return false;','reference bounds')
    s=once(s,'if(!out.G.derived(fields[to].type,target_ref)||',
           'if(!out.G.derived(field_type(to),target_ref)||','reference target type')
    s=once(s,'auto source_type=fields[source_index].type;',
           'auto source_type=field_type(source_index);','reference source type')
    s=once(s,'''            for(const auto& [index,src]:reference_fields)
                if(!reference_binding(index,src,values[index]))
                    reject(out,root,rec.begin,"field_reference_failed");''','''            for(const auto& [index,src]:reference_fields){
                construction_value value{};
                if(!reference_binding(index,src,value) ||
                   !set_construction(index,value))
                    reject(out,root,rec.begin,"field_reference_failed");
            }''','reference loop')
    s=once(s,'                    values[target_index]=value; // last assignment wins',
           '''                    if(!set_construction(target_index,value)){
                        reject(out,root,op.token,"constructor_write_failed");continue;
                    } // last assignment wins''','constructor value write')
    s=once(s,'''            if(!succeeded(out.G.define_resolved_record(parsed.handles[ri],kind_of(rec.kind),
                fields,values,bases,declares_virtual))){
                reject(out,root,rec.begin,"record_definition_failed");
                continue;
            }''','''#ifdef CW_V324_DIRECT_GRAPH_WRITE
            const auto defined=out.G.commit_direct_record(record_handle,kind_of(rec.kind),
                bases,declares_virtual);
            if(!succeeded(defined)){
                out.G.abort_direct_record(record_handle);
                reject(out,root,rec.begin,"record_direct_commit_failed");continue;
            }
#else
            if(!succeeded(out.G.define_resolved_record(parsed.handles[ri],kind_of(rec.kind),
                fields,values,bases,declares_virtual))){
                reject(out,root,rec.begin,"record_definition_failed");continue;
            }
#endif''','record commit')
    s=once(s,'                auto type=fields[target_index].type;',
           '                auto type=field_type(target_index);','nested type')
    # Assert no additional V3 direct references to soon-removed scratch fields.
    first=s.index('        for(std::size_t ri=0;ri<parsed.syntax.records.size();++ri)')
    last=s.index('    return out.unsupported==0;',first)
    region=s[first:last]
    for m in re.finditer(r'\bfields\b|\bvalues\b',region):
        ln=region.count('\n',0,m.start())+1
        print('scratch reference:',ln,region[max(0,m.start()-55):m.start()+65].replace('\n',' '))
    return s

def create_overlay(repo: Path,target:Path):
    if target.exists(): raise RuntimeError('Overlay must be new: '+str(target))
    if not (repo/'server_engine/project/graph/graph.cpp').is_file():
        raise RuntimeError('Expected ServerEngineV4 repository root')
    # CMake creates the MSVC ABI oracle under if(MSVC), even with
    # BUILD_TESTING=OFF. Keep that configure-only source in the overlay.
    oracle = repo/'tests/msvc_class_abi_oracle.cpp'
    if not oracle.is_file():
        raise RuntimeError('Missing source in repository: '+str(oracle))
    target.mkdir(parents=True)
    shutil.copy2(repo/'CMakeLists.txt',target/'CMakeLists.txt')
    shutil.copytree(repo/'server_engine',target/'server_engine')
    (target/'tests').mkdir()
    shutil.copy2(oracle,target/'tests/msvc_class_abi_oracle.cpp')
    if not (target/'tests/msvc_class_abi_oracle.cpp').is_file():
        raise RuntimeError('Overlay oracle copy failed')
    h=target/'server_engine/project/graph/graph.hpp'
    c=target/'server_engine/project/graph/graph.cpp'
    header=h.read_text(encoding='utf-8')
    sig='    [[nodiscard]] server_status add_object(\n'
    header=once(header,sig,(HERE/'tools/v324_graph_api.hpp.txt').read_text(encoding='utf-8')+'\n'+sig,'Graph.hpp add_object')
    h.write_text(header,encoding='utf-8',newline='')
    # Copy the EXACT tested reserve helper into the isolated Graph build.
    helper=HERE/'tools/v324_capacity.hpp'
    if not helper.is_file(): raise RuntimeError('Missing V3-24 FIX3 capacity helper')
    shutil.copy2(helper,target/'server_engine/project/graph/v324_capacity.hpp')
    cpp=c.read_text(encoding='utf-8')
    if '#include "graph.hpp"' in cpp:
        cpp=once(cpp,'#include "graph.hpp"',
            '#include "graph.hpp"\n#include "v324_capacity.hpp"',
            'Graph.cpp include FIX3 growth helper')
    else:
        # Synthetic configure-only fixture does not have production includes.
        cpp='#include "v324_capacity.hpp"\n'+cpp
    sig='server_status graph::add_object(\n'
    cpp=once(cpp,sig,(HERE/'tools/v324_graph_api.cpp.txt').read_text(encoding='utf-8')+'\n'+sig,'Graph.cpp add_object')
    c.write_text(cpp,encoding='utf-8',newline='')
    (target/'v324_capacity_fix3.marker').write_text('V324_FIX3_GEOMETRIC_RESERVE\n',encoding='ascii')
    print('V3-24: created isolated engine overlay:',target)
    print('V3-24: MSVC CMake oracle source:',target/'tests/msvc_class_abi_oracle.cpp')

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--repo',type=Path)
    ap.add_argument('--overlay',type=Path)
    ap.add_argument('--generate',action='store_true')
    args=ap.parse_args()
    if args.generate:
        dest=HERE/'benchmarks/v3_24_graph_producer.inc'
        dest.write_text(producer_v324(),encoding='utf-8')
        print('Generated:',dest)
    if args.repo or args.overlay:
        if not args.repo or not args.overlay:ap.error('both --repo and --overlay required')
        create_overlay(args.repo.resolve(),args.overlay.resolve())
if __name__=='__main__':main()
