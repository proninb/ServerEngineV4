// V3-26: compare unchanged V3-25 direct Graph producer against fast type resolution.
#include "v3_25_reference_for_v326.inc"
#define CW_V323_DIRECT_INDEX 1
#define CW_V324_DIRECT_GRAPH_WRITE 1
#define CW_V326_FAST_TYPE_RESOLVE 1
#define v319 v326
#include "v3_26_graph_producer.inc"
#undef v319
#undef CW_V326_FAST_TYPE_RESOLVE
#undef CW_V324_DIRECT_GRAPH_WRITE
#undef CW_V323_DIRECT_INDEX

