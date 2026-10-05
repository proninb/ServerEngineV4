#include "project_runtime.hpp"

#include "runtime_binding.hpp"

#include "../abi/abi_layout.hpp"
#include "../construction/execution_lanes.hpp"
#include "../shm/shm_layout.hpp"
#include "../shm/shm_runtime_v2.hpp"

#include "../../diagnostics/diagnostic_builder.hpp"
#include "../../diagnostics/diagnostic_descriptor.hpp"
#include "../../fixed_shared_memory.hpp"

#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace cw::server {

namespace {

[[nodiscard]] bool runtime_host_compatible(
    const server_abi_configuration& abi) noexcept {

    if (std::endian::native !=
            std::endian::little ||
        sizeof(bool) != 1 ||
        sizeof(char8_t) != 1 ||
        sizeof(char16_t) != 2 ||
        sizeof(char32_t) != 4 ||
        sizeof(short) != 2 ||
        sizeof(int) != 4 ||
        sizeof(long long) != 8 ||
        sizeof(float) != 4 ||
        sizeof(double) != 8) {

        return false;
    }

#if defined(_WIN32)
    return
        (abi.target ==
             abi_target::windows_x86 ||
         abi.target ==
             abi_target::windows_x64) &&
        sizeof(wchar_t) == 2 &&
        sizeof(long) == 4 &&
        sizeof(long double) == 8;
#else
    return
        abi.target ==
            abi_target::posix_x64 &&
        sizeof(void*) == 8 &&
        sizeof(wchar_t) == 4 &&
        sizeof(long) == 8 &&
        sizeof(long double) == 16;
#endif
}

[[nodiscard]] bool runtime_target_range_compatible(
    const server_abi_configuration& abi,
    std::uint64_t target_base_address,
    std::uint64_t runtime_size) noexcept {

    abi_properties properties;

    if (!abi_layout_properties(
            abi.target,
            properties) ||
        target_base_address == 0) {

        return false;
    }

    const auto mask =
        properties.reference_size == 4
        ? static_cast<std::uint64_t>(
            (std::numeric_limits<
                std::uint32_t>::max)())
        : (std::numeric_limits<
            std::uint64_t>::max)();

    if (target_base_address > mask) {
        return false;
    }

    std::uint64_t last_address =
        target_base_address;

    if (runtime_size != 0) {
        const auto tail =
            runtime_size - 1;

        if (tail >
            mask - target_base_address) {

            return false;
        }

        last_address += tail;
    }

    const auto reserved_begin =
        mask -
        static_cast<std::uint64_t>(
            link_handle::maximum_slot);

    return last_address <
        reserved_begin;
}

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
shm_layout_failure_detail(
    shm_layout_result result) noexcept {

    switch (result) {
    case shm_layout_result::success:
        return "SHM Runtime V2 layout prepared successfully";

    case shm_layout_result::invalid_input:
        return "compiled.bin cannot produce a valid SHM Runtime V2 layout";

    case shm_layout_result::unsupported_type:
        return "SHM Runtime V2 layout contains an unsupported native type";

    case shm_layout_result::overflow:
        return "SHM Runtime V2 layout size or alignment overflowed";

    case shm_layout_result::failed:
        return "SHM Runtime V2 layout workspace allocation failed";
    }

    return "SHM Runtime V2 layout preparation failed";
}

[[nodiscard]] constexpr std::string_view
runtime_v2_failure_detail(
    shm_runtime_v2_result result) noexcept {

    switch (result) {
    case shm_runtime_v2_result::success:
        return "SHM Runtime V2 materialized successfully";

    case shm_runtime_v2_result::invalid_input:
        return "Compiled Runtime V2 construction data is not materializable";

    case shm_runtime_v2_result::unsupported_type:
        return "Runtime V2 contains a type not supported by FIXED_DIRECT materialization";

    case shm_runtime_v2_result::incompatible_abi:
        return "Server process/compiler representation cannot encode configured Runtime V2 ABI";

    case shm_runtime_v2_result::overflow:
        return "Runtime V2 preparation or materialization overflowed";

    case shm_runtime_v2_result::failed:
        return "Runtime V2 physical metadata allocation failed";
    }

    return "SHM Runtime V2 operation failed";
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

void add_runtime_object_telemetry(
    shm_runtime_v2_execute_telemetry& target,
    const shm_runtime_v2_execute_telemetry& source) noexcept {

    target.api_applications += source.api_applications;
    target.canonical_roots += source.canonical_roots;
    target.objects += source.objects;
    target.reference_writes += source.reference_writes;
    target.relative_reference_writes +=
        source.relative_reference_writes;
    target.absolute_reference_writes +=
        source.absolute_reference_writes;
    target.object_reference_writes +=
        source.object_reference_writes;
    target.store_writes += source.store_writes;
    target.constructor_default_writes +=
        source.constructor_default_writes;
    target.pending_link_preserves +=
        source.pending_link_preserves;
    target.batch_api_applications +=
        source.batch_api_applications;
    target.child_visits += source.child_visits;
    target.repeat_visits += source.repeat_visits;
    target.repeat_iterations += source.repeat_iterations;
    target.object_batches += source.object_batches;
    target.canonical_batches += source.canonical_batches;
    target.object_patch_writes +=
        source.object_patch_writes;
}

struct runtime_object_job final {
    const shm_runtime_v2* runtime = nullptr;
    const server_abi_configuration* abi = nullptr;
    const shm_layout* layout = nullptr;
    std::span<std::byte> shm;
    std::size_t object_count = 0;
    std::size_t active_lanes = 1;
    std::span<shm_runtime_v2_result> results;
    std::span<shm_runtime_v2_execute_telemetry> telemetry;
};

void runtime_object_lane(
    void* value,
    std::size_t lane) noexcept {

    auto& job =
        *static_cast<runtime_object_job*>(
            value);

    if (lane >=
            job.active_lanes ||
        job.active_lanes == 0) {

        return;
    }

    const auto base =
        job.object_count /
        job.active_lanes;

    const auto remainder =
        job.object_count %
        job.active_lanes;

    const auto preceding_extra =
        lane < remainder
        ? lane
        : remainder;

    const auto begin =
        lane *
            base +
        preceding_extra;

    const auto count =
        base +
        static_cast<std::size_t>(
            lane < remainder);

    job.results[lane] =
        materialize_shm_runtime_v2_objects_range(
            *job.runtime,
            *job.abi,
            *job.layout,
            job.shm,
            begin,
            count,
            &job.telemetry[lane]);
}

[[nodiscard]] shm_runtime_v2_result
runtime_materialize_objects_parallel(
    const shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_execute_telemetry* telemetry,
    std::size_t& lanes_used) noexcept {

    lanes_used = 1;

    if (telemetry != nullptr) {
        *telemetry = {};
    }

    const auto object_count =
        runtime.object_count();

    const auto capacity =
        execution_lane_capacity();

    const auto active_lanes =
        object_count <
            capacity
        ? object_count
        : capacity;

    if (active_lanes <= 1) {
        return materialize_shm_runtime_v2_objects(
            runtime,
            abi,
            layout,
            shm,
            telemetry);
    }

    try {
        std::vector<shm_runtime_v2_result>
            results(
                active_lanes,
                shm_runtime_v2_result::success);

        std::vector<shm_runtime_v2_execute_telemetry>
            lane_telemetry(
                active_lanes);

        execution_lanes lanes;

        if (!succeeded(
                lanes.start(
                    active_lanes))) {

            return materialize_shm_runtime_v2_objects(
                runtime,
                abi,
                layout,
                shm,
                telemetry);
        }

        runtime_object_job job{
            &runtime,
            &abi,
            &layout,
            shm,
            object_count,
            active_lanes,
            results,
            lane_telemetry,
        };

        if (!succeeded(
                lanes.run(
                    active_lanes,
                    runtime_object_lane,
                    &job))) {

            if (telemetry != nullptr) {
                *telemetry = {};
            }

            return materialize_shm_runtime_v2_objects(
                runtime,
                abi,
                layout,
                shm,
                telemetry);
        }

        for (const auto result :
             results) {

            if (result !=
                shm_runtime_v2_result::success) {

                return result;
            }
        }

        if (telemetry != nullptr) {
            for (const auto& value :
                 lane_telemetry) {

                add_runtime_object_telemetry(
                    *telemetry,
                    value);
            }
        }

        lanes_used =
            active_lanes;

        return shm_runtime_v2_result::
            success;
    }
    catch (...) {
        if (telemetry != nullptr) {
            *telemetry = {};
        }

        return materialize_shm_runtime_v2_objects(
            runtime,
            abi,
            layout,
            shm,
            telemetry);
    }
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
    project_runtime_telemetry* telemetry) {

    output.reset();

    if (telemetry != nullptr) {
        *telemetry = {};
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

    if (!runtime_host_compatible(
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

    // RUNTIME-V2-INITIALIZATIONS-04:
    // Constructor defaults, static Graph links and Source scalar
    // initializations are all physical Runtime V2 construction phases.
    const auto constructor_defaults =
        compiled.constructor_default_count();

    const auto link_count =
        compiled.live_link_count();

    const auto initialization_count =
        compiled.initialization_count();

    if (telemetry != nullptr) {
        telemetry->runtime_v2 = true;

        telemetry->
            runtime_v2_constructor_defaults =
                constructor_defaults;

        telemetry->runtime_v2_link_count =
            link_count;

        telemetry->
            runtime_v2_initialization_count =
                initialization_count;
    }

    shm_layout v2_layout;
    shm_runtime_v2 v2_runtime;

    std::uint64_t runtime_size = 0;

    const auto layout_started =
        clock_type::now();

    attach_shm_layout_columns(
        compiled,
        v2_layout);

    const auto layout_finished =
        clock_type::now();

    if (telemetry != nullptr) {
        telemetry->layout_ns =
            elapsed_ns(
                layout_started,
                layout_finished);

        telemetry->runtime_v2_persisted_layout =
            true;
    }

    runtime_size =
        v2_layout.size();

    const auto prepare_started =
        clock_type::now();

    const auto prepared_runtime =
        prepare_shm_runtime_v2_persisted(
            compiled,
            settings.abi,
            v2_layout,
            v2_runtime,
            telemetry != nullptr
                ? &telemetry->
                    runtime_v2_prepare
                : nullptr);

    const auto prepare_finished =
        clock_type::now();

    if (telemetry != nullptr) {
        telemetry->runtime_v2_prepare_ns =
            elapsed_ns(
                prepare_started,
                prepare_finished);

        telemetry->runtime_v2_persisted_execution =
            v2_runtime.persisted_physical();

        telemetry->runtime_v2_metadata_bytes =
            static_cast<std::uint64_t>(
                v2_runtime.construction_bytes());

        telemetry->
            runtime_v2_link_dereferences =
                static_cast<std::uint64_t>(
                    v2_runtime.
                        link_dereference_count());

        telemetry->runtime_v2_links.links_prepared =
            static_cast<std::uint64_t>(
                v2_runtime.link_count());

        telemetry->runtime_v2_links.endpoint_programs =
            static_cast<std::uint64_t>(
                v2_runtime.
                    link_endpoint_program_count());

        telemetry->runtime_v2_links.dereference_steps =
            static_cast<std::uint64_t>(
                v2_runtime.
                    link_dereference_count());

        telemetry->
            runtime_v2_initialization_dereferences =
                static_cast<std::uint64_t>(
                    v2_runtime.
                        initialization_dereference_count());

        telemetry->
            runtime_v2_initializations.
                initializations_prepared =
            static_cast<std::uint64_t>(
                v2_runtime.
                    initialization_count());

        telemetry->
            runtime_v2_initializations.
                endpoint_programs =
            static_cast<std::uint64_t>(
                v2_runtime.
                    initialization_count());

        telemetry->
            runtime_v2_initializations.
                dereference_steps =
            static_cast<std::uint64_t>(
                v2_runtime.
                    initialization_dereference_count());
    }

    if (prepared_runtime !=
        shm_runtime_v2_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    runtime_v2_failure_detail(
                        prepared_runtime))
                .build());

        return prepared_runtime ==
                    shm_runtime_v2_result::
                        unsupported_type ||
                prepared_runtime ==
                    shm_runtime_v2_result::
                        incompatible_abi
            ? server_status::unsupported
            : server_status::
                project_runtime_failed;
    }

    if (telemetry != nullptr) {
        telemetry->runtime_bytes =
            runtime_size;
    }

    if (!runtime_target_range_compatible(
            settings.abi,
            settings.shm.fixed_base_address,
            runtime_size)) {

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
            runtime_size,
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
            runtime_size);

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

    const auto canonical_started =
        clock_type::now();

    const auto canonical =
        materialize_shm_runtime_v2_canonical(
            v2_runtime,
            settings.abi,
            v2_layout,
            shared_memory.bytes(),
            telemetry != nullptr
                ? &telemetry->
                    runtime_v2_canonical
                : nullptr);

    const auto canonical_finished =
        clock_type::now();

    if (telemetry != nullptr) {
        telemetry->phases.canonical_ns =
            elapsed_ns(
                canonical_started,
                canonical_finished);
    }

    if (canonical !=
        shm_runtime_v2_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    runtime_v2_failure_detail(
                        canonical))
                .build());

        return canonical ==
                    shm_runtime_v2_result::
                        unsupported_type ||
                canonical ==
                    shm_runtime_v2_result::
                        incompatible_abi
            ? server_status::unsupported
            : server_status::
                project_runtime_failed;
    }

    const auto links_mark_started =
        clock_type::now();

    const auto links_marked =
        mark_shm_runtime_v2_links(
            v2_runtime,
            settings.abi,
            v2_layout,
            shared_memory.bytes(),
            telemetry != nullptr
                ? &telemetry->
                    runtime_v2_links
                : nullptr);

    const auto links_mark_finished =
        clock_type::now();

    if (telemetry != nullptr) {
        telemetry->phases.links_mark_ns =
            elapsed_ns(
                links_mark_started,
                links_mark_finished);
    }

    if (links_marked !=
        shm_runtime_v2_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    runtime_v2_failure_detail(
                        links_marked))
                .build());

        return links_marked ==
                    shm_runtime_v2_result::
                        unsupported_type ||
                links_marked ==
                    shm_runtime_v2_result::
                        incompatible_abi
            ? server_status::unsupported
            : server_status::
                project_runtime_failed;
    }

    const auto objects_started =
        clock_type::now();

    std::size_t object_lanes = 1;

    const auto objects =
        runtime_materialize_objects_parallel(
            v2_runtime,
            settings.abi,
            v2_layout,
            shared_memory.bytes(),
            telemetry != nullptr
                ? &telemetry->
                    runtime_v2_objects
                : nullptr,
            object_lanes);

    const auto objects_finished =
        clock_type::now();

    if (telemetry != nullptr) {
        telemetry->phases.objects_ns =
            elapsed_ns(
                objects_started,
                objects_finished);

        telemetry->runtime_v2_object_lanes =
            object_lanes;
    }

    if (objects !=
        shm_runtime_v2_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    runtime_v2_failure_detail(
                        objects))
                .build());

        return objects ==
                    shm_runtime_v2_result::
                        unsupported_type ||
                objects ==
                    shm_runtime_v2_result::
                        incompatible_abi
            ? server_status::unsupported
            : server_status::
                project_runtime_failed;
    }

    const auto links_started =
        clock_type::now();

    const auto links =
        materialize_shm_runtime_v2_links(
            v2_runtime,
            settings.abi,
            v2_layout,
            shared_memory.bytes(),
            telemetry != nullptr
                ? &telemetry->
                    runtime_v2_links
                : nullptr);

    const auto links_finished =
        clock_type::now();

    if (telemetry != nullptr) {
        telemetry->
            phases.
                links_materialize_ns =
            elapsed_ns(
                links_started,
                links_finished);
    }

    if (links !=
        shm_runtime_v2_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    runtime_v2_failure_detail(
                        links))
                .build());

        return links ==
                    shm_runtime_v2_result::
                        unsupported_type ||
                links ==
                    shm_runtime_v2_result::
                        incompatible_abi
            ? server_status::unsupported
            : server_status::
                project_runtime_failed;
    }

    const auto initializations_started =
        clock_type::now();

    const auto initializations =
        materialize_shm_runtime_v2_initializations(
            v2_runtime,
            settings.abi,
            v2_layout,
            shared_memory.bytes(),
            telemetry != nullptr
                ? &telemetry->
                    runtime_v2_initializations
                : nullptr);

    const auto initializations_finished =
        clock_type::now();

    if (telemetry != nullptr) {
        telemetry->
            phases.
                initializations_ns =
            elapsed_ns(
                initializations_started,
                initializations_finished);
    }

    if (initializations !=
        shm_runtime_v2_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_runtime_failed,
                operation)
                .file(project_path)
                .detail(
                    runtime_v2_failure_detail(
                        initializations))
                .build());

        return initializations ==
                    shm_runtime_v2_result::
                        unsupported_type ||
                initializations ==
                    shm_runtime_v2_result::
                        incompatible_abi
            ? server_status::unsupported
            : server_status::
                project_runtime_failed;
    }

    const auto materialization_finished =
        clock_type::now();

    if (telemetry != nullptr) {
        telemetry->materialization_ns =
            elapsed_ns(
                materialization_started,
                materialization_finished);
    }

    runtime_binding_index bindings;

    const auto bindings_ready =
        prepare_runtime_bindings(
            compiled,
            v2_layout,
            settings.abi,
            settings.shm.fixed_base_address,
            bindings);

    if (!bindings_ready) {

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

    // Construction-only V2 metadata must not overlap the resident Project
    // lifetime. runtime_size and runtime_binding_index are already detached
    // from shm_layout / shm_runtime_v2 at this point.
    v2_runtime = shm_runtime_v2{};
    v2_layout = shm_layout{};

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
