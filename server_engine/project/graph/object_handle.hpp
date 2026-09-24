/*
 * Graph-local semantic object handle.
 *
 * object_handle identifies either Header internal-static storage or a Source
 * Project object inside one G. Semantic WHO remains identity_ref.
 */
#pragma once

#include <cstdint>
#include <type_traits>

namespace cw::server {

class graph;
class compiled_project_view;

class object_handle final {
public:
    constexpr object_handle() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return slot;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return slot != 0;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(
        object_handle,
        object_handle) noexcept = default;

    static constexpr std::uint32_t maximum_slot =
        0x3fffffffu;

private:
    explicit constexpr object_handle(
        std::uint32_t value) noexcept
        : slot(value) {
    }

    std::uint32_t slot = 0;

    friend class graph;
    friend class compiled_project_view;
};

static_assert(sizeof(object_handle) == 4);
static_assert(std::is_trivially_copyable_v<object_handle>);
static_assert(std::is_standard_layout_v<object_handle>);

}
