#define SB03_MODEL_HEADER "../semantic-bind-03/model.hpp"
#define main unused_sb03_consumer_cli
#include "../../semantic-bind-01/main.cpp"
#undef main
#include <random>
#include <sstream>

namespace {
std::uint64_t consume(const bound_storage& types,const std::vector<std::size_t>& order,bool shuffled){
    std::uint64_t hash=14695981039346656037ull;
    for(std::size_t i=0;i<types.size();++i){const auto t=types[shuffled?order[i]:i];
        hash=(hash^t.entity)*1099511628211ull;hash=(hash^static_cast<unsigned>(t.builtin))*1099511628211ull;
        hash=(hash^t.layers.size())*1099511628211ull;
        for(const auto& layer:t.layers){hash=(hash^static_cast<unsigned>(layer.kind))*1099511628211ull;hash=(hash^layer.payload)*1099511628211ull;}
    }return hash;
}
double median(std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];}
}
int main(int argc,char** argv){
    try{
        if(argc!=3){std::cerr<<"Usage: SemanticBind03Consumer <project.json> <variant>\n";return 2;}
        selected_variant=parse_variant(argv[2]);auto w=std::make_unique<world>();input_times prep;auto input=load(*w,fs::path(argv[1]),prep);ca01::pool workers(16);
        auto m=run(input,workers,false);
        {semantic_world o;const auto expected=oracle(*w,o);check(expected.accepted,"consumer benchmark requires valid corpus");verify_positive(m,o);}
        std::weak_ptr<const request_storage::arena> weak=m.data.requests.owner();
        std::size_t arena_bytes=m.data.requests.capacity()*sizeof(request)+m.data.requests.metadata_bytes();
        for(const auto& r:m.data.requests)arena_bytes+=r.type.layers.capacity()*sizeof(modifier);
        std::size_t result_bytes=m.binding.types.record_bytes()+m.binding.types.copied_modifier_bytes()+m.binding.failures.capacity()*sizeof(issue);
        if(m.binding.types.shares_shapes())result_bytes+=arena_bytes;
        m.data=batch{};std::vector<sb01::stream>{}.swap(input);w.reset();
        check(weak.expired()!=m.binding.types.shares_shapes(),"shared shape owner lifetime differs from contract");
        std::vector<std::size_t> order(m.binding.types.size());std::iota(order.begin(),order.end(),0);std::mt19937 rng(703);std::shuffle(order.begin(),order.end(),rng);
        const auto expected_linear=consume(m.binding.types,order,false),expected_shuffled=consume(m.binding.types,order,true);
        std::vector<double> linear,shuffled;
        for(unsigned trial=0;trial<5;++trial)for(unsigned task=0;task<2;++task){const bool random=((trial+task)&1)!=0;const auto start=clock_type::now();const auto checksum=consume(m.binding.types,order,random);const auto ms=elapsed_ms(start);
            check(checksum==(random?expected_shuffled:expected_linear),"result-only consumer checksum changed");(random?shuffled:linear).push_back(ms);}
        std::ostringstream result;
        result<<std::fixed<<std::setprecision(6)<<"variant="<<variant_name(selected_variant)<<",requests="<<m.binding.types.size()<<",sequential_ms="<<median(linear)<<",shuffled_ms="<<median(shuffled)<<",result_only_capacity_bytes="<<result_bytes<<",linear_checksum="<<expected_linear<<",shuffled_checksum="<<expected_shuffled<<",validation=passed\n";
        m.binding=binding_result{};check(weak.expired(),"request arena survived its last consumer");
        std::cout<<result.str();
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
