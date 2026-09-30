/*
 * Server-owned Runtime System object.
 *
 * System occupies the first bytes of FIXED_DIRECT Project SHM. It is not a
 * compiled-G object and therefore does not consume string_id, identity_ref,
 * object_handle, Source Map, or compiled.bin storage.
 *
 * Server/Runtime owns writes. Tasks and Runtime Query read the same native
 * bytes directly from the fixed shared-memory mapping.
 */
#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace cw::server {

inline constexpr std::string_view
runtime_system_object_name = "System";

struct runtime_system final {
    std::uint32_t state = 0;
    std::uint32_t flags = 0;

    std::uint64_t current_cycle = 0;
    std::uint64_t completed_cycle = 0;
    std::uint64_t target_cycle = 0;

    std::int64_t server_datetime_ns = 0;
    std::int64_t model_datetime_ns = 0;

    std::uint64_t value_generation = 0;
    std::uint64_t snapshot_generation = 0;
    std::uint64_t current_ic_generation = 0;
};

static_assert(sizeof(runtime_system) == 72);
static_assert(alignof(runtime_system) == 8);
static_assert(std::is_trivially_copyable_v<runtime_system>);
static_assert(std::is_standard_layout_v<runtime_system>);

}
