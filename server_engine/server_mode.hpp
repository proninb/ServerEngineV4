/*
 * Process-level Server access mode.
 */
#pragma once
#include <cstdint>
namespace cw::server {
enum class server_mode : std::uint8_t { full = 0, demo };
}
