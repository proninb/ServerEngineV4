#pragma once
/* Child-side dispatch; user callbacks run on the Task main thread. */
namespace cw::server::task_runtime {
struct callbacks { bool (*init)(); bool (*main)(); void (*done)(); };
int run_host(callbacks entrypoints);
}
