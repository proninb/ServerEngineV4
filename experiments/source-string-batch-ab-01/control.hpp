#pragma once
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string_view>
namespace source_batch01 {
inline const bool batch = [] {
    const auto* p = std::getenv("SOURCE_STRING_BATCH_01_MODE");
    return !(p && std::string_view{p} == "scalar");
}();
#ifdef CW_SOURCE_STRING_BATCH_AUDIT_01
inline std::uint64_t statements=0, elements=0, scalar_calls=0, batch_calls=0,
    string_scalar_calls=0, string_batch_calls=0, upserts=0, string_upserts=0,
    intern_calls=0, string_intern_calls=0, index_prepares=0;
inline bool in_string=false, fault_fired=false;
inline const std::string_view fault = [] { const auto* p=std::getenv("SOURCE_STRING_BATCH_01_FAIL");
    return std::string_view{p?p:""}; }();
struct scope {
    bool previous=in_string;
    scope() { in_string=true; ++statements; }
    ~scope() { in_string=previous; }
};
inline void print(std::ostream& out) {
    out << "batch_mode=" << batch << ",string_statements=" << statements
        << ",string_elements=" << elements << ",graph_scalar_calls=" << scalar_calls
        << ",graph_batch_calls=" << batch_calls << ",string_scalar_calls=" << string_scalar_calls
        << ",string_batch_calls=" << string_batch_calls << ",graph_upserts=" << upserts
        << ",string_upserts=" << string_upserts << ",graph_intern_calls=" << intern_calls
        << ",string_intern_calls=" << string_intern_calls << ",index_prepares=" << index_prepares
        << ",fault_fired=" << fault_fired << '\n';
}
#endif
}
