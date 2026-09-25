/*
 * Exact-address named shared-memory owner for FIXED_DIRECT Runtime storage.
 *
 * The class owns only the OS shared-memory object, mapping handle, exact virtual
 * address, and lifetime. It has no knowledge of Graph, runtime_layout, objects,
 * or reference materialization. A requested address is never substituted.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace cw::server {

enum class fixed_shared_memory_result : std::uint8_t {
    success,
    invalid_argument,
    already_exists,
    not_found,
    address_unavailable,
    size_mismatch,
    failed,
};

// Owns one writable named shared-memory mapping at one exact virtual address.
class fixed_shared_memory final {
public:
    fixed_shared_memory() noexcept = default;
    ~fixed_shared_memory();

    fixed_shared_memory(
        const fixed_shared_memory&) = delete;

    fixed_shared_memory& operator=(
        const fixed_shared_memory&) = delete;

    fixed_shared_memory(
        fixed_shared_memory&& other) noexcept;

    fixed_shared_memory& operator=(
        fixed_shared_memory&& other) noexcept;

    [[nodiscard]] fixed_shared_memory_result create(
        std::string_view name,
        std::size_t size,
        std::uintptr_t fixed_address) noexcept;

    [[nodiscard]] fixed_shared_memory_result open(
        std::string_view name,
        std::size_t size,
        std::uintptr_t fixed_address) noexcept;

    void reset() noexcept;

    [[nodiscard]] static std::size_t
    address_alignment() noexcept;

    [[nodiscard]] static std::size_t
    size_alignment() noexcept;

    [[nodiscard]] std::span<std::byte> bytes() noexcept {
        return {
            address_value,
            length,
        };
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return {
            address_value,
            length,
        };
    }

    [[nodiscard]] std::byte* data() noexcept {
        return address_value;
    }

    [[nodiscard]] const std::byte* data() const noexcept {
        return address_value;
    }

    [[nodiscard]] std::uintptr_t address() const noexcept {
        return reinterpret_cast<std::uintptr_t>(
            address_value);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return length;
    }

    [[nodiscard]] bool valid() const noexcept {
        return address_value != nullptr &&
            length != 0 &&
            native_handle != 0;
    }

private:
    std::intptr_t native_handle = 0;
    std::byte* address_value = nullptr;
    std::size_t length = 0;
    std::string native_name;
    bool owns_name = false;
};

}
