#pragma once
#include "project/runtime/project_runtime.hpp"
#include "project/shm/shm_type_batch.hpp"
#include "project/construction/execution_lanes.hpp"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace cw::server {
// Friendship exists only in the frozen experiment header. Moves vector owners;
// no operation records are cloned or rebuilt.
struct pipeline01_access {
    static void adopt(shm_runtime_v2& runtime, shm_type_batch&& types) noexcept {
        runtime.area = std::move(types);
    }
    static const shm_type_batch& types(const shm_runtime_v2& runtime) noexcept { return runtime.area; }
    static void release_encoded_execution(shm_runtime_v2& runtime) noexcept {
        decltype(runtime.links){}.swap(runtime.links);
        decltype(runtime.initializations){}.swap(runtime.initializations);
        decltype(runtime.endpoint_dereferences){}.swap(runtime.endpoint_dereferences);
    }
};
namespace pipeline01 {
inline int mode() noexcept {
    const auto* value = std::getenv("PUBLISH_PIPELINE_01_MODE");
    if (value && std::string_view{value} == "overlap") return 2;
    if (value && std::string_view{value} == "serial") return 1;
    return 0;
}
inline bool fail(std::string_view point) noexcept {
    const auto* value = std::getenv("PUBLISH_PIPELINE_01_FAIL");
    return value && std::string_view{value} == point;
}
inline std::uint64_t now() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct measurements {
    std::uint64_t image_setup_ns=0,compact_encode_ns=0,compact_setup_ns=0;
    std::uint64_t write_begin=0,write_end=0,resident_begin=0,resident_end=0;
    std::uint64_t window_ns=0,write_ns=0,resident_ns=0,overlap_ns=0;
    std::uint64_t type_adoptions=0,prepared_consumers=0,consumer_success=0,lanes=1;
    project_runtime_telemetry resident{};
};
inline thread_local measurements last;
struct borrowed_plan { shm_layout* layout; shm_runtime_v2* runtime; bool used=false; };
inline thread_local borrowed_plan* borrowed=nullptr;
struct borrow_scope {
    borrowed_plan* previous;
    explicit borrow_scope(borrowed_plan& plan) noexcept : previous(borrowed) { borrowed=&plan; }
    ~borrow_scope() { borrowed=previous; }
};
struct publication {
    const std::filesystem::path& path;
    operation_id operation;
    diagnostic_collection diagnostics;
    std::unique_ptr<project> staged;
    server_status status=server_status::project_runtime_failed;
    bool attempted=false;
    std::atomic<bool> finished=false;
    measurements stats{};
};
inline void finish_consumer(publication& state) noexcept {
    state.stats.consumer_success=succeeded(state.status) && state.staged != nullptr;
    state.stats.resident_end=now();
    state.stats.resident_ns=state.stats.resident_end-state.stats.resident_begin;
    state.finished.store(true,std::memory_order_release);
    state.finished.notify_all();
}
}
}
