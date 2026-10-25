#pragma once
namespace cw::server::detail {
#ifdef CW_GRAPH_MEMBER_INDEX_AB
// Benchmark/test-only configuration, fixed before Graph construction.
inline bool member_index_growth_reference=false;
#endif
#ifdef CW_GRAPH_MEMBER_INDEX_TEST_FAILURE
inline bool fail_next_wide_allocation=false;
#endif
}
