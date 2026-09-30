/*
 * Read-only Runtime Query Service.
 *
 * Semantic lookup is performed through mmap-native compiled G. Runtime values
 * are read directly from the published FIXED_DIRECT Runtime image.
 */
#pragma once

#include "runtime_layout.hpp"

#include <cstdint>
#include <span>
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

[[nodiscard]] runtime_query_result get_runtime_value(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<const std::byte> runtime,
    std::string_view name,
    runtime_value& output) noexcept;

}
