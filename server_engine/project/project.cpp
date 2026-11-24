#include "project.hpp"

#include <utility>

namespace cw::server {

project::~project() {
    if (link_index_worker.joinable()) {
        link_index_worker.request_stop();
        link_index_worker.join();
    }
}

void project::configure_link_queries(link_query_index_mode mode) noexcept {
    if (link_index_worker.joinable()) { link_index_worker.request_stop(); link_index_worker.join(); }
    link_index_mode = mode;
    link_index_ready_value.store(false, std::memory_order_release);
    link_query_index_value = {};
    if (mode != link_query_index_mode::background) return;
    try {
        link_index_worker = std::jthread([this](std::stop_token stop) {
            try {
                if (prepare_runtime_link_index(compiled_view, link_query_index_value, stop) == runtime_query_result::success)
                    link_index_ready_value.store(true, std::memory_order_release);
            } catch (...) { /* Optional cache: foreground query can retry/fallback. */ }
        });
    } catch (...) { link_index_mode = link_query_index_mode::lazy; }
}

runtime_query_result project::query_links(std::span<const std::string> objects, runtime_link_query& result) noexcept {
    if (link_index_mode == link_query_index_mode::disabled) return get_runtime_link(compiled_view,objects,result);
    if (link_index_worker.joinable()) link_index_worker.join();
    const auto status = get_runtime_link_indexed(compiled_view,objects,result,link_query_index_value);
    link_index_ready_value.store(link_query_index_value.owner == &compiled_view, std::memory_order_release);
    return status;
}

project::project(
    std::filesystem::path path,
    read_only_file_mapping&& mapping,
    compiled_project_view compiled,
    fixed_shared_memory&& runtime_memory,
    runtime_binding_index&& runtime_bindings,
    std::uint64_t runtime_size)
    : project_path(std::move(path)),
      compiled_mapping(std::move(mapping)),
      compiled_view(compiled),
      shared_memory(std::move(runtime_memory)),
      runtime_bindings_value(
          std::move(runtime_bindings)),
      runtime_size_value(runtime_size) {
}

}
