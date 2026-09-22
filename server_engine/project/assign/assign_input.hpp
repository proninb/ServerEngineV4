/*
 * Construction Assign input boundary.
 *
 * Assign files are materialized as exact bytes and normalized into assign_table.
 * This syntax domain performs no identity_ref resolution, Graph mutation,
 * Runtime binding, or dependency-topology emission.
 */
#pragma once

#include "assign_table.hpp"
#include "../file/file_context.hpp"
#include "../../server_status.hpp"

#include <cstdint>
#include <string_view>

namespace cw::server {

struct assign_parse_failure final {
    file_id file{};
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    std::string_view detail;
};

[[nodiscard]] server_status materialize_assign_inputs(
    file_context& files) noexcept;

[[nodiscard]] server_status parse_assign_inputs(
    const file_context& files,
    assign_table& output,
    assign_parse_failure* failure = nullptr) noexcept;

}
