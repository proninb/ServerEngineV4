#pragma once
/* Child-side dispatch; user callbacks run on the Task main thread. */
#include <cstddef>
namespace cw::server::task_runtime {
// Valid only while the Task host has attached a Project mapping.
void* shared_memory_data() noexcept;
std::size_t shared_memory_size() noexcept;

struct callbacks { bool (*init)(); bool (*main)(); void (*done)(); };
int run_host(callbacks entrypoints);
}
