#pragma once
namespace sb01 {
// One deterministic minimum per Binding task, written once after its range.
// Ties retain first request order exactly as the dense reference reduction.
template<bool Sparse>class diagnostics_writer {
    std::vector<issue>& output;std::size_t task;issue minimum;
public:
    diagnostics_writer(std::vector<issue>& values,std::size_t t):output(values),task(t){}
    void fail(std::size_t index,issue value){if constexpr(Sparse)earliest(minimum,value);else output[index]=value;}
    void finish(){if constexpr(Sparse)output[task]=minimum;}
};
}
