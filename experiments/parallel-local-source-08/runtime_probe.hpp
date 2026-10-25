#pragma once
#include "writer.hpp"
#include "../../server_engine/filesystem_path.hpp"
#include "../../server_engine/project/assign/assign_table.hpp"
#include "../../server_engine/project/persistence/compiled_project.hpp"
#include "../../server_engine/project/shm/shm_runtime_v2.hpp"
#include "../../server_engine/project/shm/shm_layout.hpp"
#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace pls08 {
struct probe_report {std::size_t objects=0,links=0,initializations=0,query_values=0,query_links=0,runtime_values=0,runtime_links=0,compiled_bytes=0,shm_bytes=0,contributions=0,provenance_checks=0;
    double writer_ms=0,encode_ms=0,runtime_ms=0;std::size_t cold_declarations=0,disjoint_writes=0,source_roots_checked=0,source_files_checked=0;std::string fields="{}",addresses="{}",pointers="{}";};
using clock=std::chrono::steady_clock;
inline double elapsed(clock::time_point t){return std::chrono::duration<double,std::milli>(clock::now()-t).count();}
inline type_ref plain(const compiled_project_view& v,type_ref t){derived_type_record d;
    while(v.derived(t,d)&&(d.kind==derived_type_kind::const_qualified||d.kind==derived_type_kind::volatile_qualified))t=d.child;return t;}
inline bool value_layout(const compiled_project_view& v,const shm_layout& layout,type_ref t,shm_value_layout& output){
    if(t.kind()==type_ref_kind::named){type_handle h;return v.named(t,h)&&layout.type(h,output);}return layout.value(t,output);}
inline shm_offset offset(const compiled_project_view& v,const shm_layout& layout,identity_ref who,std::span<const endpoint_path_step> steps){
    const auto object=v.find_object(who);object_entry entry;shm_offset result=0;require(v.object(object,entry)&&layout.object_offset(object,result),"Runtime object offset missing");auto type=entry.type;
    for(const auto& s:steps){type=plain(v,type);derived_type_record d;type_handle h;type_entry named;shm_record_offset relative=0;
        if(s.kind==endpoint_path_step_kind::array_index){shm_value_layout value;require(v.derived(type,d)&&d.kind==derived_type_kind::bounded_array&&s.value<d.payload&&value_layout(v,layout,d.child,value),"Runtime array address invalid");result+=s.value*value.size;type=d.child;}
        else {require(v.named(type,h)&&v.type(h,named),"Runtime path owner invalid");
            if(s.kind==endpoint_path_step_kind::member){member_record m;require(s.value<named.members.count&&v.member_at(named.members.begin+s.value,m)&&layout.member_offset(named.members.begin+s.value,relative),"Runtime member offset missing");type=m.type;}
            else {base_record b;require(s.kind==endpoint_path_step_kind::base&&s.value<named.bases.count&&v.base_at(named.bases.begin+s.value,b)&&layout.base_offset(named.bases.begin+s.value,relative),"Runtime base offset missing");type=v.named(v.find_type(b.type));}
            result+=relative;}
    }return result;
}
inline std::vector<std::byte> expected_value(const compiled_project_view& v,const shm_layout& layout,type_ref t,construction_value value){
    shm_value_layout size;require(value_layout(v,layout,t,size)&&size.size<=16,"Runtime scalar width unsupported by probe");t=plain(v,t);intrinsic_type intrinsic=intrinsic_type::none;
    if(t.kind()==type_ref_kind::intrinsic)intrinsic=static_cast<intrinsic_type>(t.payload());
    else {type_handle h;type_entry e;if(v.named(t,h)&&v.type(h,e)){if(e.kind==graph_type_kind::enumeration)intrinsic=e.enum_underlying();else if(e.kind==graph_type_kind::intrinsic_alias)intrinsic=e.alias_intrinsic();}}
    std::vector<std::byte> bytes(static_cast<std::size_t>(size.size));
    if(intrinsic==intrinsic_type::float_type||intrinsic==intrinsic_type::double_type||intrinsic==intrinsic_type::long_double_type){
        const auto x=value.kind==construction_kind::real?std::bit_cast<double>(value.bits()):value.kind==construction_kind::signed_integer?static_cast<double>(static_cast<std::int64_t>(value.bits())):static_cast<double>(value.bits());
        if(intrinsic==intrinsic_type::float_type){const auto f=static_cast<float>(x);require(bytes.size()==sizeof f,"Probe float width mismatch");std::memcpy(bytes.data(),&f,sizeof f);}
        else {require(bytes.size()==sizeof x,"Probe double width mismatch");std::memcpy(bytes.data(),&x,sizeof x);}}
    else {require(bytes.size()<=8,"Probe integer width unsupported");auto x=value.kind==construction_kind::real?static_cast<std::uint64_t>(static_cast<std::int64_t>(std::bit_cast<double>(value.bits()))):value.bits();if(intrinsic==intrinsic_type::bool_type)x=bool(x);std::memcpy(bytes.data(),&x,bytes.size());}return bytes;
}
inline std::string qualified(const identity_space& ids,const string_table& strings,identity_ref who){std::vector<std::string> parts;identity_record r;
    while(who&&who!=ids.root()){require(ids.record(who,r),"Runtime dump WHO absent");parts.emplace_back(strings.get(r.name));who=r.parent;}std::string result;
    for(auto i=parts.rbegin();i!=parts.rend();++i){if(!result.empty())result+="::";result+=*i;}return result;}
struct dumper {
    const compiled_project_view& view;const shm_layout& layout;std::span<const std::byte> memory;
    std::ostringstream fields,addresses,pointers;bool first_field=true,first_address=true,first_pointer=true;
    void walk(type_ref type,shm_offset at,const std::string& name,unsigned depth=0){require(depth<40,"Runtime dump depth unsupported");type=plain(view,type);
        if(!first_address)addresses<<',';first_address=false;addresses<<'"'<<name<<"\":"<<at;
        derived_type_record d;type_handle h;type_entry e;
        if(view.derived(type,d)){
            if(d.kind==derived_type_kind::lvalue_reference||d.kind==derived_type_kind::rvalue_reference){std::uintptr_t p=0;require(at+sizeof p<=memory.size(),"Reference slot out of bounds");std::memcpy(&p,memory.data()+at,sizeof p);
                const auto base=reinterpret_cast<std::uintptr_t>(memory.data());require(p>=base&&p<base+memory.size(),"Reference points outside SHM");if(!first_pointer)pointers<<',';first_pointer=false;pointers<<'"'<<name<<"\":"<<(p-base);return;}
            if(d.kind==derived_type_kind::bounded_array){require(d.payload<=128,"Runtime dump array too large");shm_value_layout item;require(value_layout(view,layout,d.child,item),"Array stride absent");for(std::uint64_t i=0;i<d.payload;++i)walk(d.child,at+i*item.size,name+'['+std::to_string(i)+']',depth+1);return;}}
        if(view.named(type,h)&&view.type(h,e)&&e.kind==graph_type_kind::record){
            for(std::size_t i=0;i<e.bases.count;++i){base_record b;shm_record_offset r=0;require(view.base_at(e.bases.begin+i,b)&&layout.base_offset(e.bases.begin+i,r),"Dump base absent");walk(view.named(view.find_type(b.type)),at+r,name+".<base"+std::to_string(i)+'>',depth+1);}
            for(std::size_t i=0;i<e.members.count;++i){member_record m;shm_record_offset r=0;require(view.member_at(e.members.begin+i,m)&&layout.member_offset(e.members.begin+i,r),"Dump member absent");walk(m.type,at+r,name+'.'+std::string(view.string(m.name)),depth+1);}return;}
        shm_value_layout value;require(value_layout(view,layout,type,value)&&value.size<=8&&at+value.size<=memory.size(),"Dump scalar unsupported");std::uint64_t bits=0;std::memcpy(&bits,memory.data()+at,static_cast<std::size_t>(value.size));if(!first_field)fields<<',';first_field=false;fields<<'"'<<name<<"\":"<<bits;
    }
};
template<FinalStateReader R>probe_report probe(const R& reader,const catalog& c,graph& G,source_map& sources,const identity_space& ids,const string_table& strings,
    std::size_t header_links,std::size_t header_initializations,bool reverse,const std::filesystem::path& artifact){
    probe_report report;const auto started=clock::now();auto written=write_supported_projection(reader,c,G,sources,header_links,header_initializations,reverse);
    status(sources.finalize(c.files.size(),ids,G),"SourceMap finalize");report.writer_ms=elapsed(started);
    report.objects=G.object_count();report.links=G.link_count();report.initializations=G.initialization_count();report.contributions=sources.contribution_entries().size();
    for(std::uint32_t i=1;i<=c.files.size();++i)require(c.files.kind(file_id{i})!=file_kind::assign,"Assign input is outside PLS08-A feasibility scope");
    const auto encode_start=clock::now();assign_table assigns;compiled_project_layout image_layout;
    require(prepare_compiled_project_layout(strings,ids,G,assigns,c.files,sources,image_layout)==compiled_project_image_result::success,"compiled layout failed");
    std::vector<std::byte> image(image_layout.size());require(encode_compiled_project_image(strings,ids,G,assigns,c.files,sources,image_layout,image)==compiled_project_image_result::success,"compiled encode failed");
    compiled_project_view view;require(view.bind(image)==compiled_project_image_result::success&&view.verify_contents()==compiled_project_image_result::success,"compiled query integrity failed");
    // PLS08-B: round-trip every Header/Source root and the inverse physical-file
    // index, including empty roots. Source-only producer checks below are a
    // separate oracle and must not substitute for Header provenance coverage.
    require(view.source_file_count()==c.files.size(),"SourceMap file domain mismatch");
    for(std::uint32_t i=1;i<=c.files.size();++i){const file_id id{i};source_map_range root,file;std::string_view path;file_kind kind;
        require(view.source_root(id,root),"Persisted root absent");const auto native_root=sources.root(id);
        require(root.count==native_root.size(),"Header/Source root size mismatch");
        for(std::size_t j=0;j<native_root.size();++j){source_contribution_record item;
            require(view.source_contribution(root.begin+static_cast<std::uint32_t>(j),item)&&item.file==native_root[j].file&&item.data==native_root[j].data,"Header/Source root serialization mismatch");}
        ++report.source_roots_checked;source_map_file_view native_file;
        require(sources.file(id,native_file)&&view.source_file(id,path,kind,file),"SourceMap physical file absent");
        std::filesystem::path decoded_path;
        require(filesystem_path_from_utf8(path,decoded_path)==filesystem_path_result::success&&decoded_path.lexically_normal()==std::filesystem::path{c.files.path(id)}.lexically_normal()&&kind==c.files.kind(id)&&file.count==native_file.size(),"SourceMap physical file metadata mismatch");
        for(std::size_t j=0;j<native_file.size();++j){std::uint32_t index=0;source_contribution_record item;
            require(view.source_file_index(file.begin+static_cast<std::uint32_t>(j),index)&&view.source_contribution(index,item)&&item.file==native_file[j].file&&item.data==native_file[j].data,"SourceMap inverse file index mismatch");}
        ++report.source_files_checked;
    }
    // Validate native and persisted root/file ownership independently of the
    // physical insertion order. Sets are cold verification state, not writer IR.
    std::vector<std::unordered_set<std::uint64_t>> contributions(reader.tasks().size()),initializations(reader.tasks().size());
    for(std::size_t i=0;i<reader.tasks().size();++i){const auto root=reader.tasks()[i].file;source_map_range range;
        require(view.source_root(root,range),"Persisted SourceMap root missing");const auto native=sources.root(root);require(range.count==native.size(),"SourceMap root count mismatch");
        for(std::size_t j=0;j<native.size();++j){source_contribution_record persisted;require(view.source_contribution(range.begin+static_cast<std::uint32_t>(j),persisted)&&persisted.file==native[j].file&&persisted.data==native[j].data,"SourceMap serialization mismatch");
            contributions[i].insert((static_cast<std::uint64_t>(persisted.file.value())<<32)|persisted.data.raw());}
        for(const auto& ep:sources.root_initializations(root))initializations[i].insert((static_cast<std::uint64_t>(ep.object.value())<<32)|ep.member.value());
    }
    const auto check_provenance=[&](event_key event,location physical,source_data_ref datum,object_endpoint ep={},bool value=false){
        require(event.source_order<contributions.size()&&contributions[event.source_order].contains((static_cast<std::uint64_t>(physical.file.value())<<32)|datum.raw()),"Final producer missing from SourceMap");
        if(value)require(initializations[event.source_order].contains((static_cast<std::uint64_t>(ep.object.value())<<32)|ep.member.value()),"Initialization producer missing from SourceMap");++report.provenance_checks;};
    for(std::size_t i=0;i<reader.declaration_count();++i){const auto d=reader.declaration_at(i);check_provenance(d.event,d.name,source_data_ref::object(d.who));}
    for(std::size_t i=0;i<reader.value_count();++i){const auto v=reader.value_at(i);const auto ep=written.endpoints[v.target.value];check_provenance(v.source.event,v.source.target,source_data_ref::object(ep.object),ep,!written.cold_values[v.target.value]);}
    for(std::size_t i=0;i<reader.binding_count();++i){const auto b=reader.binding_at(i);check_provenance(b.declaration.event,b.declaration.target,source_data_ref::link(written.links[b.target.value]));}
    for(std::size_t i=0;i<reader.warning_count();++i){const auto w=reader.warning_at(i);const auto ep=written.endpoints[w.target.value];const auto datum=w.rebind?source_data_ref::link(written.links[w.target.value]):source_data_ref::object(ep.object);
        check_provenance(w.previous_event,w.previous,datum,ep,!w.rebind&&!written.cold_values[w.target.value]);check_provenance(w.current_event,w.current,datum,ep,!w.rebind&&!written.cold_values[w.target.value]);}
    report.cold_declarations=static_cast<std::size_t>(std::count(written.cold_values.begin(),written.cold_values.end(),true));
    report.compiled_bytes=image.size();report.encode_ms=elapsed(encode_start);
    for(std::size_t i=0;i<reader.value_count();++i){const auto v=reader.value_at(i);object_initialization_record stored;
        if(written.cold_values[v.target.value]){construction_value cold;require(view.construction(view.find_object(reader.cell_at(v.target.value).who),cold)&&cold==v.value,"Compiled query cold declaration mismatch");}
        else require(view.initialization(written.endpoints[v.target.value],stored)&&stored.value==v.value,"Compiled query winner mismatch");++report.query_values;}
    for(std::size_t i=0;i<reader.binding_count();++i){const auto b=reader.binding_at(i);link_record stored;const auto handle=view.find_link_target(written.endpoints[b.target.value]);require(handle&&view.link(handle,stored)&&stored.source==written.endpoints[b.source.value],"Compiled query binding mismatch");++report.query_links;}
    const auto runtime_start=clock::now();const server_abi_configuration abi{abi_target::windows_x64,8};shm_layout layout;shm_runtime_v2 runtime;
    require(prepare_shm_layout(view,abi,layout)==shm_layout_result::success,"Runtime layout failed");
    struct storage_range {shm_offset begin,end;std::uint32_t cell;};std::vector<storage_range> ranges;ranges.reserve(reader.value_count()+reader.binding_count());
    const auto range=[&](cell_id id,std::size_t width){const auto cell=reader.cell_at(id.value);const auto at=offset(view,layout,cell.who,cell.steps);
        require(at<=layout.size()&&width<=layout.size()-at,"Final Source storage range invalid");ranges.push_back({at,at+width,id.value});};
    for(std::size_t i=0;i<reader.value_count();++i){const auto v=reader.value_at(i);shm_value_layout width;require(value_layout(view,layout,reader.cell_at(v.target.value).value_type,width),"Final scalar size absent");range(v.target,static_cast<std::size_t>(width.size));}
    for(std::size_t i=0;i<reader.binding_count();++i)range(reader.binding_at(i).target,sizeof(std::uintptr_t));
    std::sort(ranges.begin(),ranges.end(),[](auto a,auto b){return std::tie(a.begin,a.end,a.cell)<std::tie(b.begin,b.end,b.cell);});
    for(std::size_t i=1;i<ranges.size();++i)require(ranges[i-1].end<=ranges[i].begin,"Overlapping final Source storage ranges require semantic canonicalization; PLS08-A refuses physical-order winners");report.disjoint_writes=ranges.size();
    require(prepare_shm_runtime_v2(view,abi,layout,runtime)==shm_runtime_v2_result::success,"Runtime plan failed");
    std::vector<std::byte> memory(static_cast<std::size_t>(layout.size()));report.shm_bytes=memory.size();
    require(materialize_shm_runtime_v2_canonical(runtime,abi,layout,memory)==shm_runtime_v2_result::success,"Runtime canonical materialization failed");
    require(mark_shm_runtime_v2_links(runtime,abi,layout,memory)==shm_runtime_v2_result::success,"Runtime link marking failed");
    require(materialize_shm_runtime_v2_objects(runtime,abi,layout,memory)==shm_runtime_v2_result::success,"Runtime object materialization failed");
    require(materialize_shm_runtime_v2_links(runtime,abi,layout,memory)==shm_runtime_v2_result::success,"Runtime binding materialization failed");
    require(materialize_shm_runtime_v2_initializations(runtime,abi,layout,memory)==shm_runtime_v2_result::success,"Runtime value materialization failed");report.runtime_ms=elapsed(runtime_start);
    for(std::size_t i=0;i<reader.value_count();++i){const auto v=reader.value_at(i);const auto cell=reader.cell_at(v.target.value);const auto at=offset(view,layout,cell.who,cell.steps);const auto expected=expected_value(view,layout,cell.value_type,v.value);
        require(at+expected.size()<=memory.size()&&std::equal(expected.begin(),expected.end(),memory.begin()+at),"Runtime final value mismatch");++report.runtime_values;}
    for(std::size_t i=0;i<reader.binding_count();++i){const auto b=reader.binding_at(i);const auto target=reader.cell_at(b.target.value),source=reader.cell_at(b.source.value);
        const auto ta=offset(view,layout,target.who,target.steps),sa=offset(view,layout,source.who,source.steps);std::uintptr_t pointer=0;require(ta+sizeof pointer<=memory.size(),"Runtime reference out of bounds");std::memcpy(&pointer,memory.data()+ta,sizeof pointer);
        require(pointer==reinterpret_cast<std::uintptr_t>(memory.data()+sa),"Runtime final binding mismatch");++report.runtime_links;}
    if(G.object_count()<100){dumper dump{view,layout,memory};for(std::size_t i=0;i<G.object_count();++i){const auto h=G.object_at(i);shm_offset at=0;require(layout.object_offset(h,at),"Dump object missing");dump.walk(G.find(h)->type,at,qualified(ids,strings,G.identity(h)));}
        report.fields='{'+dump.fields.str()+'}';report.addresses='{'+dump.addresses.str()+'}';report.pointers='{'+dump.pointers.str()+'}';}
    // Write only verified experimental artifacts. Nothing is installed or published.
    if(!artifact.empty()){std::ofstream file(artifact,std::ios::binary);file.write(reinterpret_cast<const char*>(image.data()),static_cast<std::streamsize>(image.size()));require(bool(file),"Experimental compiled image write failed");}
    return report;
}
inline void json(std::ostream& f,const probe_report& r){f<<"{\"full_publish_benchmark\":false,\"objects\":"<<r.objects<<",\"links\":"<<r.links<<",\"initializations\":"<<r.initializations<<",\"cold_declarations\":"<<r.cold_declarations<<",\"disjoint_source_ranges\":"<<r.disjoint_writes<<",\"query_values\":"<<r.query_values<<",\"query_links\":"<<r.query_links<<",\"runtime_values\":"<<r.runtime_values<<",\"runtime_links\":"<<r.runtime_links<<",\"compiled_bytes\":"<<r.compiled_bytes<<",\"shm_bytes\":"<<r.shm_bytes<<",\"contributions\":"<<r.contributions<<",\"provenance_checks\":"<<r.provenance_checks<<",\"writer_ms\":"<<r.writer_ms<<",\"encode_ms\":"<<r.encode_ms<<",\"runtime_ms\":"<<r.runtime_ms<<",\"runtime_fields\":"<<r.fields<<",\"runtime_addresses\":"<<r.addresses<<",\"runtime_pointers\":"<<r.pointers<<",\"source_roots_checked\":"<<r.source_roots_checked<<",\"source_files_checked\":"<<r.source_files_checked<<'}';}
}
