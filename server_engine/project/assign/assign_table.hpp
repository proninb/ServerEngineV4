/*
 * Compiled Project Assign user-data table.
 *
 * Assign records are Studio-facing user data. They are not C++ semantic
 * identities, Graph links, Runtime connections, or File Context dependencies.
 * Records preserve declaration order and store text in one compact arena.
 */
#pragma once

#include "../../server_status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cw::server {

struct assign_record final {
    std::uint32_t source_offset = 0;
    std::uint32_t source_length = 0;
    std::uint32_t target_offset = 0;
    std::uint32_t target_length = 0;
};

static_assert(sizeof(assign_record) == 16);

// Ordered compact storage for normalized Assign source/target text pairs.
class assign_table final {
public:
    assign_table() = default;

    assign_table(const assign_table&) = delete;
    assign_table& operator=(const assign_table&) = delete;

    [[nodiscard]] server_status add(
        std::string_view source,
        std::string_view target) noexcept;

    void clear() noexcept;

    [[nodiscard]] std::span<const assign_record> records() const noexcept {
        return entries;
    }

    [[nodiscard]] std::string_view source(
        const assign_record& record) const noexcept;

    [[nodiscard]] std::string_view target(
        const assign_record& record) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return entries.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return entries.empty();
    }

private:
    [[nodiscard]] std::string_view text(
        std::uint32_t offset,
        std::uint32_t length) const noexcept;

    std::vector<assign_record> entries;
    std::vector<char> bytes;
};

}
