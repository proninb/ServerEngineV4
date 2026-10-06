/*
 * ABI-specific persisted Runtime construction image.
 *
 * compiled.bin owns only semantic/query G. runtime.bin owns the disposable
 * physical construction program derived from G + ABI. Both files are written
 * only by Server; runtime.bin introduces no semantic identity space and no
 * generation identifier.
 */
#pragma once

#include "compiled_project.hpp"
#include "../../configuration/server_configuration.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace cw::server {

inline constexpr std::uint32_t runtime_project_format_version = 1;
inline constexpr std::size_t runtime_project_header_size = 64;
inline constexpr std::size_t runtime_project_directory_count = 32;
inline constexpr std::size_t runtime_project_directory_entry_size = 24;

inline constexpr std::size_t runtime_project_prefix_size =
    (runtime_project_header_size +
     runtime_project_directory_count *
         runtime_project_directory_entry_size +
     63u) &
    ~std::size_t{63u};

enum class runtime_project_image_result : std::uint8_t {
    success = 0,
    invalid_state,
    invalid_image,
    io_failed,
    failed,
};

class runtime_project_layout final {
public:
    [[nodiscard]] std::size_t size() const noexcept {
        return size_value;
    }

private:
    struct section_record final {
        compiled_project_section kind{};
        std::uint32_t record_size = 0;
        std::uint64_t count = 0;
        std::uint64_t offset = 0;
    };

    std::array<section_record, runtime_project_directory_count> sections{};
    std::size_t size_value = 0;

    friend runtime_project_image_result
    prepare_runtime_project_layout(
        const compiled_project_view&,
        const compiled_project_runtime_type_counts&,
        runtime_project_layout&) noexcept;

    friend runtime_project_image_result
    encode_runtime_project_image(
        const runtime_project_layout&,
        std::span<std::byte>) noexcept;
};

class runtime_project_view final {
public:
    runtime_project_view() noexcept = default;

    [[nodiscard]] runtime_project_image_result
    bind(std::span<const std::byte> image) noexcept;

    void attach(std::span<const std::byte> image) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool valid() const noexcept {
        return bytes.data() != nullptr;
    }

    [[nodiscard]] std::span<const std::byte>
    section(compiled_project_section kind) const noexcept;

private:
    struct section_view final {
        const std::byte* data = nullptr;
        std::uint64_t count = 0;
        std::uint32_t record_size = 0;
    };

    std::span<const std::byte> bytes;
    std::array<section_view, runtime_project_directory_count> sections{};
};

[[nodiscard]] runtime_project_image_result
prepare_runtime_project_layout(
    const compiled_project_view& project,
    const compiled_project_runtime_type_counts& type_counts,
    runtime_project_layout& output) noexcept;

[[nodiscard]] runtime_project_image_result
encode_runtime_project_image(
    const runtime_project_layout& layout,
    std::span<std::byte> output) noexcept;

[[nodiscard]] std::span<std::byte>
runtime_project_mutable_section(
    std::span<std::byte> image,
    compiled_project_section kind) noexcept;

[[nodiscard]] runtime_project_image_result
persist_runtime_project(
    const std::filesystem::path& path,
    const compiled_project_view& project,
    const server_abi_configuration& abi) noexcept;

}
