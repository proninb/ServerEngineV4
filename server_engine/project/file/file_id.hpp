/*
 * Dense construction-lineage identity of one physical Project input.
 */
#pragma once

#include <cstdint>

namespace cw::server {

class file_id final {
public:
    constexpr file_id() noexcept = default;

    explicit constexpr file_id(
        std::uint32_t value) noexcept
        : id(value) {
    }

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return id;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return id != 0;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(
        const file_id&,
        const file_id&) noexcept = default;

private:
    std::uint32_t id = 0;
};

static_assert(sizeof(file_id) == 4);

}
