#include "extract.hpp"
#include "planner.hpp"
#include "aliases.hpp"
#include "../../server_engine/project/parser/header_parser_v2.hpp"
#include "../../server_engine/project/frontend/source_discovery.hpp"
#include "../../server_engine/project/project_configuration_manifest.hpp"
#include "../../server_engine/project/persistence/compiled_project.hpp"
#include "../../server_engine/read_only_file_mapping.hpp"
#include <chrono>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
using namespace pls02;
using clock_type=std::chrono::steady_clock;
double ms(clock_type::time_point start){return std::chrono::duration<double,std::milli>(clock_type::now()-start).count();}
void check(bool ok,const char* text){if(!ok)throw failure(text);}
std::string quote(std::string_view text){std::ostringstream out;out<<'"';for(unsigned char c:text){
    if(c=='"'||c=='\\')out<<'\\'<<char(c);else if(c=='\n')out<<"\\n";else if(c=='\r')out<<"\\r";else if(c=='\t')out<<"\\t";
    else if(c<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;else out<<char(c);}out<<'"';return out.str();}
std::string qualified(const identity_space& ids,const string_table& strings,identity_ref who){std::vector<std::string> pieces;
    for(auto p=who;p&&p!=ids.root();){identity_record r;if(!ids.record(p,r))break;pieces.emplace_back(strings.get(r.name));p=r.parent;}
    std::string out;for(auto it=pieces.rbegin();it!=pieces.rend();++it){if(!out.empty())out+="::";out+=*it;}return out;}
std::string key(identity_ref who,std::span<const endpoint_path_step> pp){
    std::string s(reinterpret_cast<const char*>(&who),sizeof who);
    for(const auto& p:pp){const auto k=static_cast<std::uint32_t>(p.kind);s.append(reinterpret_cast<const char*>(&k),sizeof k);s.append(reinterpret_cast<const char*>(&p.value),sizeof p.value);}return s;
}
std::string oracle_key(const compiled_project_view& oracle,object_endpoint ep){std::vector<endpoint_path_step> path;
    if(ep.member.is_member())path.push_back({ep.member.direct_member().value(),endpoint_path_step_kind::member,{}});
    else {endpoint_path_record r;check(oracle.endpoint_path(ep.member.path(),r),"Oracle path unavailable");for(id i=0;i<r.steps.count;++i){endpoint_path_step s;check(oracle.endpoint_path_step_at(r.steps.begin+i,s),"Oracle step unavailable");path.push_back(s);}}
    return key(ep.object,path);
}
void oracle_projection(const std::filesystem::path& image,const graph& G,const string_table& strings,const identity_space& ids,const bound_model& bound){
    read_only_file_mapping file;check(file.open(image)==read_only_file_mapping_result::success,"Cannot open oracle image");compiled_project_view view;
    check(view.bind(file.bytes())==compiled_project_image_result::success,"Cannot bind oracle image");
    check(G.object_count()==view.object_count(),"Oracle object count mismatch");
    for(std::size_t i=0;i<G.object_count();++i){const auto handle=G.object_at(i);object_entry expected;
        const auto who=G.identity(handle);const auto old=view.find_object(who);
        check(view.object(old,expected),"Oracle WHO missing");check(G.find(handle)->type==expected.type,"Oracle object type mismatch");
        identity_record identity;check(ids.record(who,identity),"New WHO unavailable");
        check(identity.parent==view.identity_parent(who)&&strings.get(identity.name)==view.string(view.identity_name(who)),"Oracle WHO spelling/parent mismatch");
        check((G.find(handle)->state&graph_object_flag_mask)==(expected.state&graph_object_flag_mask),"Oracle object flags mismatch");
        construction_value now,before;check(G.construction(handle,now)&&view.construction(old,before)&&now==before,"Oracle declaration construction mismatch");}
    std::unordered_map<std::string,construction_value> values;
    for(std::size_t i=0;i<view.initialization_count();++i){object_initialization_record r;check(view.initialization_at(i,r),"Oracle initialization missing");values.emplace(oracle_key(view,r.target),r.value);}
    // This gate is for corpora without repeated logical writes; declaration
    // initializers are represented separately by old V2 object construction.
    std::uint64_t count=0;
    for(const auto& w:bound.writes){if(w.declaration)continue;const auto& ep=bound.endpoints[w.target];const auto path=std::span<const endpoint_path_step>{bound.paths}.subspan(ep.steps.begin,ep.steps.count);
        const auto found=values.find(key(ep.who,path));check(found!=values.end()&&found->second==w.value,"Oracle typed write mismatch");values.erase(found);++count;}
    check(count==view.initialization_count()&&values.empty(),"Oracle typed write count mismatch");
    std::unordered_map<std::string,std::string> links;
    for(std::size_t i=0;i<view.link_count();++i){link_record r;check(view.link(view.link_at(i),r),"Oracle link missing");links.emplace(oracle_key(view,r.target),oracle_key(view,r.source));}
    for(const auto& b:bound.bindings){const auto& t=bound.endpoints[b.target];const auto& s=bound.endpoints[b.source];
        const auto found=links.find(key(t.who,std::span<const endpoint_path_step>{bound.paths}.subspan(t.steps.begin,t.steps.count)));
        check(found!=links.end()&&found->second==key(s.who,std::span<const endpoint_path_step>{bound.paths}.subspan(s.steps.begin,s.steps.count)),"Oracle typed binding mismatch");links.erase(found);}
    check(bound.bindings.size()==view.link_count()&&links.empty(),"Oracle binding count mismatch");
}
std::string location(origin at){return "{\"file_id\":"+std::to_string(at.file.value())+",\"offset\":"+std::to_string(at.source.offset)+",\"length\":"+std::to_string(at.source.length)+"}";}
std::uint64_t fingerprint(const result& final,const bound_model& bound){
    std::uint64_t h=14695981039346656037ULL;
    const auto add=[&](std::uint64_t v){for(unsigned i=0;i<8;++i){h=(h^static_cast<unsigned char>(v))*1099511628211ULL;v>>=8;}};
    const auto origin_=[&](origin a){add(a.file.value());add(a.source.offset);add(a.source.length);};
    const auto cell_=[&](id n){const auto& c=final.cells[n];add(c.who.value());add(c.type.value());add(c.steps.count);
        for(const auto& s:std::span<const endpoint_path_step>{bound.paths}.subspan(c.steps.begin,c.steps.count)){add(static_cast<unsigned>(s.kind));add(s.value);}};
    add(bound.declaration_origins.size());for(const auto& d:bound.declaration_origins){add(d.who.value());origin_(d.name);origin_(d.statement);add(d.event.source);add(d.event.local);add(d.is_extern);}
    add(final.values.size());for(const auto& v:final.values){cell_(v.cell);const auto& w=bound.writes[v.write];add(w.value.bits());add(w.value.operand);add(static_cast<unsigned>(w.value.kind));add(w.event.source);add(w.event.local);origin_(w.at);origin_(w.value_at);}
    add(final.bindings.size());for(const auto& b:final.bindings){cell_(b.target);cell_(b.source);}
    add(final.warnings.size());for(const auto& w:final.warnings){cell_(w.cell);origin_(w.previous);origin_(w.current);add(w.previous_event.source);add(w.previous_event.local);add(w.current_event.source);add(w.current_event.local);add(w.rebind);}
    return h;
}
}

int main(int argc,char** argv){using namespace pls02;
    if(argc<3){std::cerr<<"usage: ParallelLocalSource02 project.json output.json [--reverse] [--oracle compiled.bin]\n";return 2;}
    const auto project=std::filesystem::absolute(argv[1]);const auto output=std::filesystem::path(argv[2]);bool reverse=false;std::filesystem::path oracle;
    for(int i=3;i<argc;++i){if(std::string_view(argv[i])=="--reverse")reverse=true;
        else if(std::string_view(argv[i])=="--oracle"&&i+1<argc)oracle=argv[++i];else return 2;}
    try {
        file_context files;check(succeeded(files.configure_publish_acquisition())&&succeeded(files.enable_owned_content()),"File setup failed");
        project_configuration_manifest manifest;preprocessor_configuration config;diagnostic_collection diagnostics;lexical_generation lexical;
        check(succeeded(compose_project_configuration(project,operation_id{1},diagnostics,manifest,files,config)),"Project compose failed");
        const auto roots=files.size();auto tasks=plan_tasks(files,project,diagnostics);
        source_preparation_failure preparation;
        if(!succeeded(prepare_source_lexical_state(files,lexical,&preparation,initial_lexical_scope::source_only)))
            throw failure(lexical_error_message(preparation.lexical.reason),{preparation.file,{preparation.lexical.offset,preparation.lexical.length}});
        std::vector<file_id> headers;for(id i=1;i<=roots;++i)if(files.kind(file_id{i})==file_kind::header)headers.push_back(file_id{i});
        prepared_include_closure_v2 closure;prepared_include_failure_v2 physical;
        check(succeeded(prepare_header_include_closure_v2(files,headers,config,closure,&physical)),"Header closure failed");
        string_table strings;identity_space ids{strings};graph G;preprocessor_v2_failure pp_failure;
        semantic_preprocessor_v2 pp{files,closure.view(),config,strings,&pp_failure};
        for(auto root:headers){check(succeeded(pp.start(root)),"Header preprocessing failed");parser_v2_failure f;header_parser_v2 parser{pp,ids,G,&f};
            if(!succeeded(parser.parse()))throw failure(std::string(f.detail),{f.file,f.source});}
        std::vector<model> locals(tasks.size());const auto parse_start=clock_type::now();std::uint64_t token_reads=0,extracted_endpoints=0;std::size_t model_bytes=0;
        for(std::size_t j=0;j<tasks.size();++j){const auto i=reverse?tasks.size()-1-j:j;const auto task=tasks[i];
            locals[i]=extractor(task,files.content(task.file),lexical.source_tokens(task.file)).run();token_reads+=locals[i].token_reads;extracted_endpoints+=locals[i].endpoints.size();model_bytes+=locals[i].retained_bytes();}
        const auto extraction_ms=ms(parse_start);
        // Mechanical no-reparse boundary: lexical input is destroyed before
        // global linking, whose API accepts only model records + Header catalog.
        lexical.release_source_tokens();
        for(const auto& task:tasks)check(lexical.source_tokens(task.file).empty(),"Source token release failed");
        const auto link_start=clock_type::now();auto bound=semantic_linker(G,strings,ids).run(locals);const auto semantic_ms=ms(link_start);
        locals.clear();locals.shrink_to_fit();
        const auto valid_origin=[&](origin o){check(o.file&&files.contains(o.file)&&o.source.length&&o.source.offset<=files.content(o.file).size()&&
            o.source.length<=files.content(o.file).size()-o.source.offset,"Invalid retained Source origin");};
        for(const auto& d:bound.declaration_origins){valid_origin(d.name);valid_origin(d.statement);}
        for(const auto& w:bound.writes){valid_origin(w.at);valid_origin(w.value_at);}
        for(const auto& b:bound.bindings){valid_origin(b.at);valid_origin(b.value_at);}
        bool oracle_checked=false;if(!oracle.empty()){oracle_projection(oracle,G,strings,ids,bound);oracle_checked=true;}
        const auto alias_start=clock_type::now();auto final=alias_linker(G,bound).run();const auto alias_ms=ms(alias_start);
        std::ofstream report(output);check(bool(report),"Cannot write report");
        report<<"{\n\"tasks\":[";for(std::size_t i=0;i<tasks.size();++i){if(i)report<<',';report<<"{\"file_id\":"<<tasks[i].file.value()<<",\"source_order\":"<<tasks[i].source_order<<",\"path\":"<<quote(std::filesystem::path(files.path(tasks[i].file)).generic_string())<<'}';}
        report<<"],\n\"counts\":{\"declarations\":"<<bound.declarations<<",\"events\":"<<bound.semantic_events<<",\"writes\":"<<bound.writes.size()<<",\"bindings\":"<<bound.bindings.size()<<",\"cells\":"<<final.values.size()<<",\"warnings\":"<<final.warnings.size()<<",\"source_token_reads\":"<<token_reads<<",\"linker_token_reads\":0,\"provenance_records\":"<<bound.declaration_origins.size()+bound.writes.size()+bound.bindings.size()<<",\"model_bytes\":"<<model_bytes<<",\"bound_bytes\":"<<bound.bytes()<<",\"final_array_bytes\":"<<final.cells.capacity()*sizeof(cell)+final.values.capacity()*sizeof(winner)+final.bindings.capacity()*sizeof(final_binding)+final.warnings.capacity()*sizeof(warning)<<",\"reference_follows\":"<<final.reference_follows<<",\"default_follows\":"<<final.default_follows<<",\"link_rounds\":"<<final.link_rounds<<"},\n";
        report<<"\"endpoint_requests\":{\"extracted\":"<<extracted_endpoints<<",\"typed\":"<<bound.endpoint_requests<<"},\n\"timings_ms\":{\"extraction\":"<<extraction_ms<<",\"semantic\":"<<semantic_ms<<",\"aliases\":"<<alias_ms<<"},\n\"oracle_projection_checked\":"<<(oracle_checked?"true":"false")<<",\n\"semantic_fingerprint\":"<<quote(std::to_string(fingerprint(final,bound)))<<",\n";
        // Full per-cell detail is only emitted for fixtures. Large-corpus IR
        // remains in memory until final reduction and is summarized here.
        report<<"\"values\":[";
        if(final.values.size()<10000)for(std::size_t i=0;i<final.values.size();++i){if(i)report<<',';const auto& w=final.values[i];const auto& c=final.cells[w.cell];const auto& op=bound.writes[w.write];
            report<<"{\"object\":"<<quote(qualified(ids,strings,c.who))<<",\"steps\":[";id n=0;for(const auto& s:std::span<const endpoint_path_step>{bound.paths}.subspan(c.steps.begin,c.steps.count)){if(n++)report<<',';report<<'['<<static_cast<unsigned>(s.kind)<<','<<s.value<<']';}
            report<<"],\"value\":"<<op.value.bits()<<",\"kind\":"<<static_cast<unsigned>(op.value.kind)<<",\"order\":["<<op.event.source<<','<<op.event.local<<"],\"origin\":"<<location(op.at)<<",\"value_origin\":"<<location(op.value_at)<<'}';}
        report<<"],\n\"warnings\":[";for(std::size_t i=0;i<final.warnings.size();++i){if(i)report<<',';const auto& w=final.warnings[i];report<<"{\"previous\":"<<location(w.previous)<<",\"current\":"<<location(w.current)<<",\"order\":["<<w.current_event.source<<','<<w.current_event.local<<"],\"rebind\":"<<(w.rebind?"true":"false")<<'}';}
        report<<"]\n}\n";
        std::cout<<"parallel_local_source02,tasks="<<tasks.size()<<",events="<<bound.semantic_events<<",writes="<<bound.writes.size()<<",bindings="<<bound.bindings.size()<<",cells="<<final.values.size()<<",warnings="<<final.warnings.size()<<",linker_token_reads=0,oracle="<<oracle_checked<<'\n';
    }catch(const failure& f){std::ofstream report(output);report<<"{\"error\":"<<quote(f.what())<<",\"origin\":"<<location(f.at)<<",\"order\":["<<f.event.source<<','<<f.event.local<<"]}\n";std::cerr<<f.what()<<" file="<<f.at.file.value()<<" offset="<<f.at.source.offset<<'\n';return 1;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
