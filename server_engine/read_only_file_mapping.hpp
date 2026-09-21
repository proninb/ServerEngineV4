/*
 * Cross-platform immutable file-mapping owner.
 *
 * The mapping exposes file bytes directly from the OS page cache without a
 * whole-file heap copy. It owns only the native file/mapping handles and the
 * mapped address.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace cw::server {

enum class read_only_file_mapping_result : std::uint8_t {
    success,
    not_found,
    empty,
    failed,
};

class read_only_file_mapping final {
public:
    read_only_file_mapping() noexcept = default;
    ~read_only_file_mapping();

    read_only_file_mapping(
        const read_only_file_mapping&) = delete;

    read_only_file_mapping& operator=(
        const read_only_file_mapping&) = delete;

    read_only_file_mapping(
        read_only_file_mapping&& other) noexcept;

    read_only_file_mapping& operator=(
        read_only_file_mapping&& other) noexcept;

    [[nodiscard]] read_only_file_mapping_result open(
        const std::filesystem::path& path) noexcept;

    void reset() noexcept;

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
    const std::byte* address = nullptr;
    std::size_t length = 0;
};

}
