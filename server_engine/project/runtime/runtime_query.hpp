/*
 * Read-only Runtime Query Service.
 *
 * Semantic lookup is performed through mmap-native compiled G. Runtime values
 * are read directly from the published FIXED_DIRECT Runtime image.
 */
#pragma once

#include "runtime_binding.hpp"

#include <cstdint>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace cw::server {

enum class runtime_query_result : std::uint8_t {
    success = 0,
    invalid_input,
    not_found,
    unsupported_type,
    invalid_runtime,
};

struct runtime_value final {
    intrinsic_type type = intrinsic_type::none;
    std::uint8_t size = 0;
    std::uint16_t reserved16 = 0;
    std::uint32_t reserved32 = 0;
    std::uint64_t bits = 0;
};

static_assert(sizeof(runtime_value) == 16);
static_assert(std::is_trivially_copyable_v<runtime_value>);
static_assert(std::is_standard_layout_v<runtime_value>);

struct runtime_object_match final {
    std::string name;
    std::string file;
};

struct runtime_object_query final {
    bool pattern = false;
    std::string name;
    std::string type;
    std::string file;
    std::vector<runtime_object_match> objects;
};

struct runtime_type_member final {
    std::string name;
    std::string type;
};

struct runtime_type_spec final {
    std::string name;
    std::vector<std::string> bases;
    std::vector<runtime_type_member> members;
};

struct runtime_object_type final {
    std::string object;
    runtime_type_spec type;
};

struct runtime_type_query final {
    std::vector<runtime_object_type> types;
};

struct runtime_link_match final {
    std::string source;
    std::string target;
    std::string file;
};

struct runtime_assign_match final {
    std::string source;
    std::string target;
    std::string file;
};

struct runtime_object_links final {
    std::string object;
    std::vector<runtime_link_match> links;
    std::vector<runtime_assign_match> assigns;
};

struct runtime_link_query final {
    std::vector<runtime_object_links> objects;
};

// Owned by one resident immutable Project. Never shared across project replacement.
struct runtime_link_index final {
    const compiled_project_view* owner = nullptr;
    std::vector<std::uint32_t> offsets, incidents, files, assign_offsets, assign_incidents;
    [[nodiscard]] std::size_t storage_bytes() const noexcept {
        return (offsets.capacity()+incidents.capacity()+files.capacity()+assign_offsets.capacity()+assign_incidents.capacity())*sizeof(std::uint32_t);
    }
};
// May allocate; caller owns exception policy. Publishes only on complete success.
[[nodiscard]] runtime_query_result prepare_runtime_link_index(
    const compiled_project_view& project, runtime_link_index& index, std::stop_token stop = {});
[[nodiscard]] runtime_query_result get_runtime_link_indexed(
    const compiled_project_view& project, std::span<const std::string> objects,
    runtime_link_query& output, runtime_link_index& index) noexcept;

[[nodiscard]] runtime_query_result get_runtime_object(
    const compiled_project_view& project,
    std::string_view name,
    std::string_view type,
    runtime_object_query& output) noexcept;

[[nodiscard]] runtime_query_result get_runtime_type(
    const compiled_project_view& project,
    std::span<const std::string> objects,
    runtime_type_query& output) noexcept;

[[nodiscard]] runtime_query_result get_runtime_link(
    const compiled_project_view& project,
    std::span<const std::string> objects,
    runtime_link_query& output) noexcept;

[[nodiscard]] bool build_task_variable_catalog(const compiled_project_view& project,
    const runtime_binding_index& bindings, std::span<const std::byte> runtime,
    std::vector<std::byte>& output) noexcept;

[[nodiscard]] runtime_query_result set_runtime_value(const compiled_project_view& project,
    const runtime_binding_index& bindings, std::span<std::byte> runtime,
    std::string_view name, std::string_view input, runtime_value& output) noexcept;

[[nodiscard]] runtime_query_result get_runtime_value(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<const std::byte> runtime,
    std::string_view name,
    runtime_value& output) noexcept;

}
