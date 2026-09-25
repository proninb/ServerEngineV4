#include "project_runtime.hpp"

#include "runtime_layout.hpp"

#include "../../diagnostics/diagnostic_builder.hpp"
#include "../../diagnostics/diagnostic_descriptor.hpp"
#include "../../fixed_shared_memory.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>

namespace cw::server {
namespace {

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
    std::unique_ptr<project>& output) {

    output.reset();

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

    runtime_layout layout;

    const auto prepared =
        prepare_runtime_layout(
            compiled,
            settings.abi,
            layout);

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

    fixed_shared_memory shared_memory;

    const auto created =
        shared_memory.create(
            settings.shm.name,
            mapping_size,
            static_cast<std::uintptr_t>(
                settings.shm.fixed_base_address));

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

    try {
        output =
            std::make_unique<project>(
                project_path,
                std::move(compiled_mapping),
                compiled,
                std::move(shared_memory),
                layout.size());
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
