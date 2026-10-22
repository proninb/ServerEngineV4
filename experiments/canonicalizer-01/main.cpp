#include "kernel.hpp"
#include "capture.hpp"
#include <array>
#include <chrono>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace ca01 {
using clock_type=std::chrono::steady_clock;
inline double ms(clock_type::time_point a,clock_type::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
struct meta {id parent=0,name=0,kind=0;std::uint64_t event=import_event;};
struct corpus {
    std::vector<char> bytes;
    std::vector<std::string_view> strings{1};
    std::vector<meta> identities{2};
    std::uint64_t intern_calls=0,resolve_calls=0,lookup_calls=0;
    std::uint32_t footer_strings=0,footer_identities=0;
    template<class F>void each(F callback)const {
        std::size_t at=8;std::uint64_t ordinal=0;bool footer=false;
        while(at<bytes.size()){
            require(bytes.size()-at>=sizeof(canonicalizer01::trace_record),"truncated trace record");
            canonicalizer01::trace_record r;std::memcpy(&r,bytes.data()+at,sizeof(r));at+=sizeof(r);
            require(r.event==++ordinal&&r.reserved==0,"invalid trace event order");
            require(r.length<=bytes.size()-at,"truncated trace spelling");
            const std::string_view text{bytes.data()+at,r.length};at+=r.length;
            require(!footer,"data after footer");
            if(r.type==255){require(r.length==0&&at==bytes.size(),"invalid footer");footer=true;}
            else require(r.type>=1&&r.type<=3,"unknown trace record");
            callback(r,text);
        }
        require(footer,"missing trace footer");
    }
    explicit corpus(const std::string& path){
        std::ifstream f(path,std::ios::binary|std::ios::ate);require(bool(f),"cannot open corpus");
        const auto length=f.tellg();require(length>=8&&length<8ll*1024*1024*1024,"invalid corpus length");
        bytes.resize(static_cast<std::size_t>(length));f.seekg(0);f.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));require(bool(f),"cannot read corpus");
        require(std::memcmp(bytes.data(),"CWCA0101",8)==0,"invalid corpus magic");
        identities[1]={0,0,0,0};
        each([&](auto r,std::string_view text){
            if(r.type==1){++intern_calls;require(r.result&&r.result<=10000000&&!text.empty(),"invalid string event");
                if(strings.size()<=r.result)strings.resize(r.result+1);
                if(strings[r.result].empty())strings[r.result]=text;else require(strings[r.result]==text,"conflicting captured string_id");
            }else if(r.type==2||r.type==3){require(r.name<strings.size()&&!strings[r.name].empty()&&r.kind>=1&&r.kind<=3,"invalid identity event");
                const auto p=r.parent&0x3fffffffu;require(p<identities.size()&&identities[p].event!=import_event,"unknown parent in capture");
                if(r.type==2){++resolve_calls;auto slot=r.result&0x3fffffffu;
                    require(slot&&slot<=10000000&&(r.result>>30)==r.kind,"invalid captured WHO");
                    if(identities.size()<=slot)identities.resize(slot+1);
                    auto& m=identities[slot];
                    if(m.event==import_event)m={r.parent,r.name,r.kind,r.event};
                    else require(m.parent==r.parent&&m.name==r.name&&m.kind==r.kind,"conflicting captured WHO");
                }else ++lookup_calls;
            }else {footer_strings=r.parent;footer_identities=r.name;}
        });
        require(strings.size()==static_cast<std::size_t>(footer_strings)+1&&identities.size()==static_cast<std::size_t>(footer_identities)+1,"capture inventory mismatch");
        for(id i=1;i<strings.size();++i)require(!strings[i].empty(),"hole in captured strings");
        for(id i=1;i<identities.size();++i)require(identities[i].event!=import_event,"hole in captured identities");
    }
};
struct dataset {input names,entities;};
inline dataset localize(const corpus& trace,std::size_t workers){
    dataset data;data.names.strings=true;
    std::vector<id> name_roots,entity_roots;
    std::vector<std::unordered_map<std::string_view,id>> local_names(workers);
    std::vector<std::unordered_map<id,id>> local_entities(workers);
    for(id w=0;w<workers;++w){name_roots.push_back(static_cast<id>(data.names.symbols.size()));
        data.names.symbols.push_back({none,w,0,{},0,0,0});
        entity_roots.push_back(static_cast<id>(data.entities.symbols.size()));
        data.entities.symbols.push_back({none,w,0,{},0,0,1});local_entities[w][1]=entity_roots.back();}
    // Imports express the captured scope context; they do not create declarations.
    std::function<id(id,id)> import_parent=[&](id w,id who)->id{
        auto it=local_entities[w].find(who);if(it!=local_entities[w].end())return it->second;
        const auto slot=who&0x3fffffffu;require(slot<trace.identities.size(),"invalid imported parent");
        const auto& m=trace.identities[slot];const auto p=import_parent(w,m.parent);
        const auto local=static_cast<id>(data.entities.symbols.size());
        data.entities.symbols.push_back({p,w,m.kind,trace.strings[m.name],import_event,0,who});
        local_entities[w][who]=local;return local;
    };
    trace.each([&](auto r,std::string_view text){const id w=r.unit%static_cast<id>(workers);
        if(r.type==1){auto [it,added]=local_names[w].emplace(text,static_cast<id>(data.names.symbols.size()));
            if(added)data.names.symbols.push_back({name_roots[w],w,1,text,r.event,0,r.result});
            else require(data.names.symbols[it->second].expected==r.result,"local string identity conflict");
        }else if(r.type==2){auto parent=import_parent(w,r.parent);const auto local=static_cast<id>(data.entities.symbols.size());
            data.entities.symbols.push_back({parent,w,r.kind,trace.strings[r.name],r.event,0,r.result});
            local_entities[w][r.result]=local;
        }else if(r.type==3){data.entities.queries.push_back({import_parent(w,r.parent),r.kind,r.result,trace.strings[r.name],r.event});}
    });
    data.names.prepare();data.entities.prepare();return data;
}
inline std::size_t cross_duplicates(const input& in,const result& out){
    std::vector<std::uint32_t> masks(in.symbols.size());
    for(id i=0;i<in.symbols.size();++i)if(in.symbols[i].parent!=none&&in.symbols[i].event!=import_event)
        masks[out.representative[i]]|=1u<<in.symbols[i].worker;
    std::size_t total=0;for(auto m:masks)if(m)total+=std::popcount(m)-1;return total;
}
using image=std::vector<char>;
inline void append(image& out,id value){for(unsigned i=0;i<4;++i)out.push_back(static_cast<char>(value>>(8*i)));}
inline image oracle_image(const corpus& c){image out;
    for(id i=1;i<c.strings.size();++i){append(out,i);append(out,static_cast<id>(c.strings[i].size()));out.insert(out.end(),c.strings[i].begin(),c.strings[i].end());}
    for(id i=1;i<c.identities.size();++i){const auto& m=c.identities[i];append(out,(m.kind<<30)|i);append(out,m.parent);append(out,m.name);}return out;
}
inline image actual_image(const dataset& d,const numbering& names,const numbering& entities){
    std::vector<std::string_view> strings(names.classes+1);
    for(id i=0;i<d.names.symbols.size();++i)if(d.names.symbols[i].parent!=none){auto n=names.who[i];require(n<strings.size(),"string projection bounds");strings[n]=d.names.symbols[i].name;}
    std::unordered_map<std::string_view,id> ids;
    image out;for(id i=1;i<strings.size();++i){require(!strings[i].empty(),"string projection hole");ids.emplace(strings[i],i);append(out,i);append(out,static_cast<id>(strings[i].size()));out.insert(out.end(),strings[i].begin(),strings[i].end());}
    std::vector<std::array<id,3>> records(entities.classes+1);
    records[1]={1,0,0};
    for(id i=0;i<d.entities.symbols.size();++i){const auto& s=d.entities.symbols[i];if(s.parent==none)continue;
        auto who=entities.who[i];auto it=ids.find(s.name);require(it!=ids.end(),"missing name in numbered strings");
        std::array<id,3> record{who,entities.who[s.parent],it->second};auto slot=who&0x3fffffffu;
        require(slot<records.size(),"identity projection bounds");
        if(records[slot][0])require(records[slot]==record,"projection entity disagreement");else records[slot]=record;
    }
    for(id i=1;i<records.size();++i){require(records[i][0]!=0,"identity projection hole");for(auto v:records[i])append(out,v);}return out;
}
struct synthetic {
    std::deque<std::string> storage;
    dataset data;
    std::vector<std::string_view> strings{1};
    std::vector<meta> identities{2};
    explicit synthetic(bool nested,std::size_t count=10000){
        data.names.strings=true;identities[1]={0,0,0,0};
        constexpr id producers=16;
        std::array<id,producers> nr{},er{};
        std::array<std::array<id,256>,producers> scopes{};
        for(auto& a:scopes)a.fill(none);
        std::array<id,256> global_scopes{};
        std::vector<std::unordered_map<std::string_view,id>> local_names(producers);
        std::unordered_map<std::string_view,id> global_names;
        std::uint64_t event=0;
        for(id w=0;w<producers;++w){nr[w]=static_cast<id>(data.names.symbols.size());data.names.symbols.push_back({none,w,0,{},0,0,0});er[w]=static_cast<id>(data.entities.symbols.size());data.entities.symbols.push_back({none,w,0,{},0,0,1});}
        auto name=[&](id w,std::string_view text)->id{
            ++event;auto [g,new_global]=global_names.emplace(text,static_cast<id>(strings.size()));if(new_global)strings.push_back(text);
            auto [l,new_local]=local_names[w].emplace(text,static_cast<id>(data.names.symbols.size()));
            if(new_local)data.names.symbols.push_back({nr[w],w,1,text,event,0,g->second});return g->second;
        };
        auto create=[&](id parent,id kind,id sid)->id{auto slot=static_cast<id>(identities.size());identities.push_back({parent,sid,kind,event});return (kind<<30)|slot;};
        for(std::size_t i=0;i<count;++i){const id bucket=static_cast<id>(i%256);
            storage.push_back(nested?"N"+std::to_string(bucket):"unused");const auto ns=std::string_view{storage.back()};
            storage.push_back("T"+std::to_string(nested?i/256:i));const auto type_name=std::string_view{storage.back()};id global_type=0;
            for(id w=0;w<producers;++w){id parent=er[w],global_parent=1;
                if(nested){if(scopes[w][bucket]==none){const auto sid=name(w,ns);++event;if(!global_scopes[bucket])global_scopes[bucket]=create(1,1,sid);
                        scopes[w][bucket]=static_cast<id>(data.entities.symbols.size());data.entities.symbols.push_back({er[w],w,1,ns,event,0,global_scopes[bucket]});}
                    parent=scopes[w][bucket];global_parent=global_scopes[bucket];}
                const auto sid=name(w,type_name);++event;if(!global_type)global_type=create(global_parent,2,sid);
                data.entities.symbols.push_back({parent,w,2,type_name,event,0,global_type});
                if(w==0){data.entities.queries.push_back({parent,2,0,type_name,event-1});data.entities.queries.push_back({parent,2,global_type,type_name,event+1});}
            }
        }
        storage.push_back("unused-parameter-name");for(id w=0;w<producers;++w)(void)name(w,storage.back());
        data.names.prepare();data.entities.prepare();
    }
    image expected_image()const{image out;
        for(id i=1;i<strings.size();++i){append(out,i);append(out,static_cast<id>(strings[i].size()));out.insert(out.end(),strings[i].begin(),strings[i].end());}
        for(id i=1;i<identities.size();++i){const auto& m=identities[i];append(out,(m.kind<<30)|i);append(out,m.parent);append(out,m.name);}return out;
    }
};
inline input shuffled(input in,unsigned seed){std::mt19937 rng(seed);std::vector<id> order(in.symbols.size()),inverse(order.size());
    std::iota(order.begin(),order.end(),0);std::shuffle(order.begin(),order.end(),rng);
    for(id i=0;i<order.size();++i)inverse[order[i]]=i;
    auto original=in.symbols;for(id i=0;i<order.size();++i){in.symbols[i]=original[order[i]];if(in.symbols[i].parent!=none)in.symbols[i].parent=inverse[in.symbols[i].parent];}
    for(auto& q:in.queries)q.parent=inverse[q.parent];in.prepare();return in;
}
inline std::vector<input> fixtures(){std::vector<input> all;
    input n;n.symbols={{none,0,0,{},0,0,1},{none,1,0,{},0,0,1},
        {0,0,1,"N",1,0,0x40000002},{2,0,2,"A",2,0,0x80000003},
        {1,1,1,"N",3,0,0x40000002},{4,1,2,"A",4,0,0x80000003},
        {0,0,2,"A",5,0,0x80000004},{4,1,1,"M",6,0,0x40000005},
        {2,0,1,"M",7,0,0x40000005},{7,1,2,"A",8,0,0x80000006},
        {8,0,2,"A",9,0,0x80000006},{0,0,3,"A",10,0,0xc0000007}};
    n.queries={{2,2,0,"A",1},{4,2,0x80000003,"A",3},{0,2,0,"A",4},
        {0,2,0x80000004,"A",6},{4,3,0,"A",11},{8,2,0x80000006,"A",10},
        {0,3,0xc0000007,"A",11},
        {8,2,0x80000003,"A",7,true},{8,2,0x80000006,"A",10,true},
        {8,3,0,"A",9,true},{8,3,0xc0000007,"A",11,true}};all.push_back(n);
    input order;order.symbols={{none,0,0,{},0,0,1},{0,0,2,"Z",1,0,0x80000002},{0,1,2,"A",2,0,0x80000003}};all.push_back(order);
    input strings;strings.strings=true;strings.symbols={{none,0,0,{},0,0,0},{none,1,0,{},0,0,0},
        {0,0,1,"Z",1,0,1},{0,0,1,"unused-parameter",2,0,2},{1,1,1,"A",3,0,3},{1,1,1,"Z",4,0,1},
        {0,0,1,"a",5,0,4},{1,1,1,"same-prefix-aaaaaaaa-X",6,0,5},{0,0,1,"same-prefix-aaaaaaaa-Y",7,0,6}};all.push_back(strings);
    auto collision=n;collision.collisions=true;all.push_back(collision);
    for(auto& in:all)in.prepare();return all;
}
inline void self_test(){auto cases=fixtures();std::size_t passed=0;
    for(auto workers:{1u,2u,4u,8u,16u}){pool p(workers);
        for(const auto& c:cases)for(unsigned seed=0;seed<4;++seed){auto in=shuffled(c,seed);memory mem(true);
            auto ref=reference(in,&mem);auto expected=number(in,ref,mem);verify(in,ref,expected);
            for(auto algorithm:{"sort","index","hierarchical"}){std::vector<comparisons> counts(workers);
                auto actual=canonicalize(in,algorithm,p,mem,counts,true);auto numbered=number(in,actual,mem);verify(in,actual,numbered);
                require(numbered.who==expected.who,"reference disagreement");++passed;}
        }
    }
    for(auto bad:cases){bad.symbols[0].parent=0;bool rejected=false;try{bad.prepare();}catch(...){rejected=true;}require(rejected,"cycle accepted");}
    auto invalid=cases[0];invalid.symbols[3].parent=999999;bool rejected=false;try{invalid.prepare();}catch(...){rejected=true;}require(rejected,"invalid parent accepted");
    auto late=cases[0];late.symbols[2].event=100;late.symbols[4].event=101;late.prepare();memory mem(false);auto r=reference(late,&mem);rejected=false;try{(void)number(late,r,mem);}catch(...){rejected=true;}require(rejected,"late parent accepted");
    auto wrong=cases[0];wrong.queries[0].expected=0x80000003;wrong.prepare();auto ref=reference(wrong,&mem);auto nums=number(wrong,ref,mem);rejected=false;try{verify(wrong,ref,nums);}catch(...){rejected=true;}require(rejected,"forward visibility violation accepted");
    std::cout<<"self_test=passed,algorithm_model_checks="<<passed<<",workers=1/2/4/8/16\n";
}
inline std::uint64_t peak_ws(){
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX c{};c.cb=sizeof(c);if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c),sizeof(c)))return c.PeakWorkingSetSize;
#endif
    return 0;
}
inline int run(int argc,char** argv){
    if(argc==2&&std::string_view(argv[1])=="self-test"){self_test();return 0;}
    require(argc>=4,"usage: Canonicalizer01 <capture.bin> <sort|index|hierarchical|reference> <workers> [diagnostic]");
    const auto begin=clock_type::now();const auto workers=static_cast<std::size_t>(std::stoul(argv[3]));
    require(workers==1||workers==2||workers==4||workers==8||workers==16,"unsupported worker count");
    // Fixed logical producer streams: scaling changes execution, not the workload.
    constexpr std::size_t producers=16;
    std::unique_ptr<corpus> c;std::unique_ptr<synthetic> generated;dataset data;
    if(std::string_view(argv[1])=="synthetic-flat"||std::string_view(argv[1])=="synthetic-nested"){
        generated=std::make_unique<synthetic>(std::string_view(argv[1])=="synthetic-nested");data=std::move(generated->data);
    }else {c=std::make_unique<corpus>(argv[1]);data=localize(*c,producers);}
    const auto loaded=clock_type::now();const auto input_peak=peak_ws();
    const bool diagnostic=argc>4&&std::string_view(argv[4])=="diagnostic";
    memory mem(diagnostic);std::vector<comparisons> counts(workers);
    const auto start_pool=clock_type::now();pool p(workers);const auto ready=clock_type::now();
    const std::string_view algorithm=argv[2];
    auto invoke=[&](const input& in){return algorithm=="reference"?reference(in,&mem):canonicalize(in,algorithm,p,mem,counts,diagnostic);};
    const auto a=clock_type::now();auto names=invoke(data.names);const auto b=clock_type::now();
    auto name_ids=number(data.names,names,mem);const auto d=clock_type::now();
    auto entities=invoke(data.entities);const auto e=clock_type::now();
    auto who=number(data.entities,entities,mem);const auto f=clock_type::now();
    const auto kernel_peak=peak_ws();
    verify(data.names,names,name_ids);verify(data.entities,entities,who);
    auto expected=c?oracle_image(*c):generated->expected_image(),actual=actual_image(data,name_ids,who);
    require(expected==actual,"identity/string projection byte parity failed");
    std::uint64_t calls=0,bytes=0;for(auto s:counts){calls+=s.calls;bytes+=s.span_bytes;}
    std::cout<<"algorithm="<<algorithm<<",workers="<<workers<<",producers="<<producers<<",diagnostic="<<diagnostic
        <<",input_ms="<<ms(begin,loaded)<<",pool_start_ms="<<ms(start_pool,ready)
        <<",strings_canonical_ms="<<ms(a,b)<<",entities_canonical_ms="<<ms(d,e)
        <<",canonical_ms="<<ms(a,b)+ms(d,e)<<",numbering_ms="<<ms(b,d)+ms(e,f)
        <<",kernel_ms="<<ms(a,f)<<",tracked_peak_bytes="<<mem.peak.load()
        <<",input_peak_ws_bytes="<<input_peak<<",kernel_peak_ws_bytes="<<kernel_peak
        <<",string_comparisons="<<calls<<",comparison_span_bytes="<<bytes
        <<",strings="<<name_ids.classes<<",identities="<<who.classes
        <<",local_string_symbols="<<data.names.symbols.size()<<",local_identity_symbols="<<data.entities.symbols.size()
        <<",max_identity_depth="<<data.entities.levels.size()-1
        <<",cross_worker_string_duplicates="<<cross_duplicates(data.names,names)
        <<",cross_worker_identity_duplicates="<<cross_duplicates(data.entities,entities)
        <<",captured_intern_calls="<<(c?c->intern_calls:0)<<",captured_resolve_calls="<<(c?c->resolve_calls:0)
        <<",binding_queries="<<data.entities.queries.size()<<",byte_parity=identity_string_projection,validation=passed\n";
    return 0;
}
} // namespace ca01
int main(int argc,char** argv){try{return ca01::run(argc,argv);}catch(const std::exception& e){std::cerr<<"canonicalizer error: "<<e.what()<<'\n';return 1;}}
