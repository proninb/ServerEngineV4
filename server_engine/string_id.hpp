/*
 * Compact Project-construction text identity.
 *
 * string_id identifies one canonical interned spelling. It carries no semantic
 * scope or declaration kind; semantic identity remains identity_ref.
 */
#pragma once

#include <cstdint>

namespace cw::server {

class string_table;
class compiled_project_view;

class string_id final {
public:
    constexpr string_id() noexcept = default;

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
        string_id,
        string_id) noexcept = default;

private:
    explicit constexpr string_id(
        std::uint32_t value) noexcept
        : slot(value) {
    }

    std::uint32_t slot = 0;

    friend class string_table;
    friend class compiled_project_view;
};

static_assert(sizeof(string_id) == 4);

}
