/*
 * Project IC catalog.
 *
 * IC.json is mutable Project-owned Runtime metadata. IC identity is the
 * resolved filesystem path of the persisted snapshot.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cw::server {

struct ic_catalog_entry final {
    std::string description;
    std::uint64_t size = 0;
    std::filesystem::path path;
};

struct ic_catalog final {
    std::vector<ic_catalog_entry> items;
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

[[nodiscard]] const ic_catalog_entry* find_ic_catalog_entry(
    const ic_catalog& catalog,
    const std::filesystem::path& catalog_path,
    const std::filesystem::path& path) noexcept;

[[nodiscard]] ic_catalog_entry* find_ic_catalog_entry(
    ic_catalog& catalog,
    const std::filesystem::path& catalog_path,
    const std::filesystem::path& path) noexcept;

[[nodiscard]] bool upsert_ic_catalog_entry(
    ic_catalog& catalog,
    const std::filesystem::path& catalog_path,
    const std::filesystem::path& path,
    std::uint64_t size) noexcept;

[[nodiscard]] bool erase_ic_catalog_entry(
    ic_catalog& catalog,
    const std::filesystem::path& catalog_path,
    const std::filesystem::path& path,
    std::filesystem::path& removed_path) noexcept;

}
