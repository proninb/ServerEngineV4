#pragma once

#include <cstdint>
#include <string_view>

namespace cw::server::task_runtime {
inline constexpr std::uint32_t variable_catalog_magic = 0x31524156;
inline constexpr std::string_view variable_catalog_suffix = "_vars";
inline constexpr std::uint8_t no_bit = 0xff;

struct variable_catalog_header {
    std::uint32_t magic;
    std::uint32_t count;
    std::uint64_t size;
};

struct variable_catalog_entry {
    std::uint64_t offset;
    std::uint32_t name_offset;
    std::uint32_t name_size;
    std::uint8_t size;
    std::uint8_t bit;
    std::uint8_t bit_count;
    std::uint8_t reserved[5];
};

static_assert(sizeof(variable_catalog_header) == 16);
static_assert(sizeof(variable_catalog_entry) == 24);
}
