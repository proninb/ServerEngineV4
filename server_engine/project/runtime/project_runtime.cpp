#include "project_runtime.hpp"

#include "fixed_direct_materializer.hpp"
#include "runtime_layout.hpp"

#include "../construction/execution_lanes.hpp"

#include "../../diagnostics/diagnostic_builder.hpp"
#include "../../diagnostics/diagnostic_descriptor.hpp"
#include "../../fixed_shared_memory.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace cw::server {
namespace {

[[nodiscard]] bool has_reserved_system_object(
    const compiled_project_view& project) noexcept {

    const auto name =
        project.find_string(
            runtime_system_object_name);

    if (!name) {
        return false;
    }

    const auto root =
        project.identity_root();

    if (!root) {
        return true;
    }

    const auto identity =
        project.find_identity(
            root,
            name,
            identity_kind::object);

    return identity &&
        static_cast<bool>(
            project.find_object(
                identity));
}

[[nodiscard]] constexpr std::string_view
layout_failure_detail(
    runtime_layout_result result) noexcept {

    switch (result) {
    case runtime_layout_result::success:
        return "Runtime layout prepared successfully";

    case runtime_layout_result::invalid_input:
        return "compiled.bin cannot produce a valid Runtime layout";

    case runtime_layout_result::unsupported_type:
        return "Runtime layout contains a type unsupported by native materialization";

    case runtime_layout_result::overflow:
        return "Runtime layout size or alignment overflowed";

    case runtime_layout_result::failed:
        return "Runtime layout workspace allocation failed";
    }

    return "Runtime layout preparation failed";
}

[[nodiscard]] constexpr std::string_view
materialization_failure_detail(
    fixed_direct_materialization_result result) noexcept {

    switch (result) {
    case fixed_direct_materialization_result::success:
        return "FIXED_DIRECT Runtime materialized successfully";

    case fixed_direct_materialization_result::invalid_input:
        return "Compiled Runtime construction data is not materializable";

    case fixed_direct_materialization_result::unsupported_type:
        return "Runtime contains a type not supported by FIXED_DIRECT materialization";

    case fixed_direct_materialization_result::incompatible_abi:
        return "Server process/compiler representation cannot encode configured FIXED_DIRECT target ABI";

    case fixed_direct_materialization_result::overflow:
        return "FIXED_DIRECT Runtime materialization overflowed";

    case fixed_direct_materialization_result::failed:
        return "FIXED_DIRECT Runtime materialization workspace allocation failed";
    }

    return "FIXED_DIRECT Runtime materialization failed";
}

[[nodiscard]] constexpr std::string_view
shared_memory_failure_detail(
    fixed_shared_memory_result result) noexcept {

    switch (result) {
    case fixed_shared_memory_result::success:
        return "Project SHM created successfully";

    case fixed_shared_memory_result::invalid_argument:
        return "Project SHM request violates the exact-address mapping contract";

    case fixed_shared_memory_result::already_exists:
        return "Configured Project SHM name already exists";

    case fixed_shared_memory_result::not_found:
        return "Project SHM creation unexpectedly reported not_found";

    case fixed_shared_memory_result::address_unavailable:
        return "Configured Project SHM virtual address is unavailable";

    case fixed_shared_memory_result::size_mismatch:
        return "Project SHM creation reported an unexpected size mismatch";

    case fixed_shared_memory_result::failed:
        return "Project SHM operating-system creation failed";
    }

    return "Project SHM creation failed";
}

struct runtime_pretouch_job final {
    std::span<std::byte> bytes;
    std::size_t page_size = 0;
    std::size_t page_count = 0;
    std::size_t active_lanes = 1;
};

void runtime_pretouch_lane(
    void* value,
    std::size_t lane) noexcept {

    auto& job =
        *static_cast<runtime_pretouch_job*>(
            value);

    if (lane >=
            job.active_lanes ||
        job.active_lanes == 0 ||
        job.page_size == 0) {

        return;
    }

    const auto base_pages =
        job.page_count /
        job.active_lanes;

    const auto remainder =
        job.page_count %
        job.active_lanes;

    const auto preceding_extra =
        lane < remainder
        ? lane
        : remainder;

    const auto begin =
        lane *
            base_pages +
        preceding_extra;

    const auto count =
        base_pages +
        static_cast<std::size_t>(
            lane < remainder);

    const auto end =
        begin + count;

    for (auto page = begin;
         page < end;
         ++page) {

        job.bytes[
            page *
            job.page_size] =
                std::byte{0};
    }
}

void runtime_pretouch_sequential(
    std::span<std::byte> bytes,
    std::size_t logical_size,
    std::size_t page_size) noexcept {

    for (std::size_t offset = 0;
         offset < logical_size;
         offset += page_size) {

        bytes[offset] =
            std::byte{0};
    }
}

[[nodiscard]] std::size_t
runtime_pretouch(
    std::span<std::byte> bytes,
    std::size_t logical_size,
    std::size_t page_size) noexcept {

    if (logical_size == 0) {
        return 0;
    }

    const auto whole_pages =
        logical_size /
        page_size;

    const auto page_count =
        whole_pages +
        static_cast<std::size_t>(
            logical_size %
                page_size !=
            0);

    const auto lane_capacity =
        execution_lane_capacity();

    const auto active_lanes =
        page_count <
            lane_capacity
        ? page_count
        : lane_capacity;

    if (active_lanes <= 1) {
        runtime_pretouch_sequential(
            bytes,
            logical_size,
            page_size);

        return 1;
    }

    execution_lanes lanes;

    const auto started =
        lanes.start(
            active_lanes);

    if (!succeeded(started)) {
        runtime_pretouch_sequential(
            bytes,
            logical_size,
            page_size);

        return 1;
    }

    runtime_pretouch_job job{
        bytes,
        page_size,
        page_count,
        active_lanes,
    };

    const auto dispatched =
        lanes.run(
            active_lanes,
            runtime_pretouch_lane,
            &job);

    if (!succeeded(dispatched)) {
        runtime_pretouch_sequential(
            bytes,
            logical_size,
            page_size);

        return 1;
    }

    return active_lanes;
}

[[nodiscard]] bool runtime_mapping_size(
    std::uint64_t runtime_size,
    std::size_t page_size,
    std::size_t& output) noexcept {

    output = 0;

    if (page_size == 0) {
        return false;
    }

    const auto page =
        static_cast<std::uint64_t>(
            page_size);

    std::uint64_t value =
        runtime_size == 0
        ? page
        : runtime_size;

    const auto remainder =
        value % page;

    if (remainder != 0) {
        const auto padding =
            page - remainder;

        if (value >
            (std::numeric_limits<std::uint64_t>::max)() -
                padding) {

            return false;
        }

        value += padding;
    }

    if (value >
        static_cast<std::uint64_t>(
            (std::numeric_limits<std::size_t>::max)())) {

        return false;
    }

    output =
        static_cast<std::size_t>(
            value);

    return output != 0;
}

}

server_status create_resident_project(
    const std::filesystem::path& project_path,
    const server_settings_configuration& settings,
    operation_id operation,
    diagnostic_collection& diagnostics,
    read_only_file_mapping&& compiled_mapping,
    compiled_project_view compiled,
    std::unique_ptr<project>& output,
    project_runtime_telemetry* telemetry,
    project_runtime_profile* profile) {

    output.reset();

    if (telemetry != nullptr) {
        *telemetry = {};
    }

    if (profile != nullptr) {
        *profile = {};
    }

    using clock_type =
        std::chrono::steady_clock;

    const auto elapsed_ns =
        [](clock_type::time_point begin,
           clock_type::time_point end) noexcept {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<
                    std::chrono::nanoseconds>(
                        end - begin)
                    .count());
        };

    if (settings.shm.mode !=
        shm_runtime_mode::fixed_direct) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_unsupported,
                operation)
                .file(project_path)
                .detail(
                    "Configured SHM Runtime mode is not implemented")
                .build());

        return server_status::unsupported;
    }

    if (!fixed_direct_host_compatible(
            settings.abi)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_unsupported,
                operation)
                .file(project_path)
                .detail(
                    "Server process/compiler representation cannot encode the configured FIXED_DIRECT target ABI")
                .build());

        return server_status::unsupported;
    }

    if (has_reserved_system_object(
            compiled)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    "Root Runtime object name 'System' is reserved by Server Runtime")
                .build());

        return server_status::project_runtime_failed;
    }

    runtime_layout layout;

    const auto layout_started =
        clock_type::now();

    const auto prepared =
        prepare_runtime_layout(
            compiled,
            settings.abi,
            layout);

    const auto layout_finished =
        clock_type::now();

    if (telemetry != nullptr) {
        telemetry->layout_ns =
            elapsed_ns(
                layout_started,
                layout_finished);

        telemetry->runtime_bytes =
            layout.size();
    }

    if (prepared !=
        runtime_layout_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    layout_failure_detail(
                        prepared))
                .build());

        return server_status::project_runtime_failed;
    }

    if (!fixed_direct_target_range_compatible(
            settings.abi,
            settings.shm.fixed_base_address,
            layout.size())) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    "Configured Project SHM address range is not representable by the FIXED_DIRECT target ABI")
                .build());

        return server_status::project_runtime_failed;
    }

    std::size_t mapping_size = 0;

    if (!runtime_mapping_size(
            layout.size(),
            fixed_shared_memory::
                size_alignment(),
            mapping_size)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    "Runtime size cannot be represented as an OS-aligned Project SHM mapping")
                .build());

        return server_status::project_runtime_failed;
    }

    if (settings.shm.fixed_base_address >
        static_cast<std::uint64_t>(
            (std::numeric_limits<std::uintptr_t>::max)())) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    "Configured Project SHM virtual address is not representable by this process")
                .build());

        return server_status::project_runtime_failed;
    }

    if (telemetry != nullptr) {
        telemetry->shm_bytes =
            static_cast<std::uint64_t>(
                mapping_size);
    }

    fixed_shared_memory shared_memory;

    const auto shm_create_started =
        clock_type::now();

    const auto created =
        shared_memory.create(
            settings.shm.name,
            mapping_size,
            static_cast<std::uintptr_t>(
                settings.shm.fixed_base_address));

    const auto shm_create_finished =
        clock_type::now();

    if (telemetry != nullptr) {
        telemetry->shm_create_ns =
            elapsed_ns(
                shm_create_started,
                shm_create_finished);
    }

    if (created !=
        fixed_shared_memory_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    shared_memory_failure_detail(
                        created))
                .build());

        return server_status::project_runtime_failed;
    }

    // A brand-new mapping is logically zero, but leaving every page untouched
    // pushes first-write faults into the sparse construction traversal.
    //
    // PARALLEL-PRETOUCH-01:
    // Establish pages through the existing fixed execution-lane abstraction.
    // Lanes own disjoint contiguous page ranges; there is no atomic page
    // index, work queue, mutex, or per-page task object. One barrier completes
    // before materialization starts.
    const auto page_size =
        fixed_shared_memory::
            size_alignment();

    if (page_size == 0) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    "Cannot determine Project SHM page size for Runtime pre-touch")
                .build());

        return server_status::project_runtime_failed;
    }

    const auto pretouch_started =
        clock_type::now();

    auto shared_bytes =
        shared_memory.bytes();

    const auto logical_size =
        static_cast<std::size_t>(
            layout.size());

    const auto pretouch_lanes =
        runtime_pretouch(
            shared_bytes,
            logical_size,
            page_size);

    const auto pretouch_finished =
        clock_type::now();

    if (telemetry != nullptr) {
        telemetry->shm_pretouch_ns =
            elapsed_ns(
                pretouch_started,
                pretouch_finished);

        telemetry->shm_pretouch_lanes =
            pretouch_lanes;
    }

    const auto materialization_started =
        clock_type::now();

    const auto materialized =
        materialize_fixed_direct_zeroed(
            compiled,
            layout,
            settings.abi,
            settings.shm.fixed_base_address,
            shared_memory.bytes(),
            telemetry != nullptr
                ? &telemetry->materializer
                : nullptr,
            profile != nullptr
                ? &profile->materializer
                : nullptr);

    const auto materialization_finished =
        clock_type::now();

    if (telemetry != nullptr) {
        telemetry->materialization_ns =
            elapsed_ns(
                materialization_started,
                materialization_finished);
    }

    if (materialized !=
        fixed_direct_materialization_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    materialization_failure_detail(
                        materialized))
                .build());

        return materialized ==
                fixed_direct_materialization_result::
                    incompatible_abi ||
            materialized ==
                fixed_direct_materialization_result::
                    unsupported_type
            ? server_status::unsupported
            : server_status::project_runtime_failed;
    }

    const auto runtime_size =
        layout.size();

    runtime_binding_index bindings;

    if (!layout.release_bindings(
            bindings,
            settings.shm.fixed_base_address)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    "Cannot publish Runtime binding index")
                .build());

        return server_status::project_runtime_failed;
    }

    try {
        output =
            std::make_unique<project>(
                project_path,
                std::move(compiled_mapping),
                compiled,
                std::move(shared_memory),
                std::move(bindings),
                runtime_size);
    }
    catch (...) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    "Cannot allocate resident Project state after Runtime/SHM creation")
                .build());

        return server_status::project_runtime_failed;
    }

    return server_status::success;
}

}
