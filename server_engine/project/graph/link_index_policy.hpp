#pragma once
namespace cw::server::detail {
#ifdef CW_GRAPH_LINK_INDEX_AB
inline bool link_index_growth_reference = true;
#endif
#ifdef CW_GRAPH_LINK_INDEX_TEST_FAILURE
inline bool fail_next_link_wide_allocation = false;
#endif
}
