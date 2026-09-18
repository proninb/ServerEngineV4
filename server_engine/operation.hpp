/*
 * Operation identity shared by diagnostics, logging correlation, telemetry,
 * and future command responses.
 */
#pragma once

#include <cstdint>

namespace cw::server {

// Identifies one externally visible Server operation.
class operation_id final {
public:
    // Invalid / unspecified operation.
    constexpr operation_id() noexcept = default;

    // Constructs an operation identifier from its stable process-local value.
    explicit constexpr operation_id(std::uint64_t value) noexcept
        : value_storage(value) {
    }

    // Returns the raw operation identifier.
    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_storage;
    }

    // Returns true when this object identifies a real operation.
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return value_storage != 0;
    }

    friend constexpr bool operator==(
        operation_id,
        operation_id) noexcept = default;

private:
    // Zero is reserved for "no operation".
    std::uint64_t value_storage = 0;
};

}
