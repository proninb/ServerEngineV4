/*
 * Project IC catalog.
 *
 * IC.json is mutable Project-owned metadata. Logical IC identity is the
 * ordered group path plus leaf name; persisted Runtime values stay in the
 * existing semantic binary IC format.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cw::server {

enum class ic_catalog_node_kind : std::uint8_t {
    group = 0,
    ic,
};

struct ic_catalog_node final {
    ic_catalog_node_kind kind = ic_catalog_node_kind::group;
    std::string name;
    std::string description;
    std::uint64_t size = 0;
    std::filesystem::path path;
    std::vector<ic_catalog_node> children;
};

struct ic_catalog final {
    std::vector<ic_catalog_node> items;
};

enum class ic_catalog_result : std::uint8_t {
    success = 0,
    not_found,
    invalid,
    io_error,
    failed,
};

[[nodiscard]] ic_catalog_result load_ic_catalog(
    const std::filesystem::path& path,
    ic_catalog& output) noexcept;

[[nodiscard]] ic_catalog_result save_ic_catalog(
    const std::filesystem::path& path,
    const ic_catalog& catalog) noexcept;

[[nodiscard]] const ic_catalog_node* find_ic_catalog_node(
    const ic_catalog& catalog,
    const std::vector<std::string>& group,
    std::string_view name) noexcept;

[[nodiscard]] ic_catalog_node* find_ic_catalog_node(
    ic_catalog& catalog,
    const std::vector<std::string>& group,
    std::string_view name) noexcept;

[[nodiscard]] bool upsert_ic_catalog_node(
    ic_catalog& catalog,
    const std::vector<std::string>& group,
    std::string_view name,
    const std::filesystem::path& path,
    std::uint64_t size) noexcept;

[[nodiscard]] bool erase_ic_catalog_node(
    ic_catalog& catalog,
    const std::vector<std::string>& group,
    std::string_view name,
    std::filesystem::path& removed_path) noexcept;

}
