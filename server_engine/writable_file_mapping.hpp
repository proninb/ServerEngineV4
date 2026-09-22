/*
 * Cross-platform writable file-mapping owner.
 *
 * The mapping creates/truncates the final file to its exact size and exposes
 * those bytes directly for construction. It owns only native file/mapping
 * handles and the mapped address; it does not create temporary artifact files.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace cw::server {

enum class writable_file_mapping_result : std::uint8_t {
    success,
    failed,
};

// Owns one final-path writable mmap used for direct artifact construction.
class writable_file_mapping final {
public:
    writable_file_mapping() noexcept = default;
    ~writable_file_mapping();

    writable_file_mapping(
        const writable_file_mapping&) = delete;

    writable_file_mapping& operator=(
        const writable_file_mapping&) = delete;

    writable_file_mapping(
        writable_file_mapping&& other) noexcept;

    writable_file_mapping& operator=(
        writable_file_mapping&& other) noexcept;

    [[nodiscard]] writable_file_mapping_result create(
        const std::filesystem::path& path,
        std::size_t size) noexcept;

    [[nodiscard]] writable_file_mapping_result flush() noexcept;

    void reset() noexcept;

    [[nodiscard]] std::span<std::byte> bytes() noexcept {
        return {
            address,
            length,
        };
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return {
            address,
            length,
        };
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return length;
    }

    [[nodiscard]] bool valid() const noexcept {
        return address != nullptr &&
            length != 0;
    }

private:
    std::intptr_t file_handle = -1;
    std::intptr_t mapping_handle = 0;
    std::byte* address = nullptr;
    std::size_t length = 0;
};

}
