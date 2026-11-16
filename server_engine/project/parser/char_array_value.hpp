#pragma once

#include "../graph/graph.hpp"
#include "../string/string_table.hpp"
#include <limits>
#include <string>
#include <string_view>

namespace cw::server {

inline bool char_scalar_value(type_ref type, construction_value value) noexcept {
    return type.kind() == type_ref_kind::intrinsic &&
        type.payload() == static_cast<std::uint32_t>(intrinsic_type::char_type) &&
        ((value.kind == construction_kind::unsigned_integer && value.bits() <= 255) ||
         (value.kind == construction_kind::signed_integer &&
          static_cast<std::int64_t>(value.bits()) >= -128 && static_cast<std::int64_t>(value.bits()) <= 127) ||
         value.kind == construction_kind::zero);
}

inline server_status char_array_value(string_table& strings, std::string_view data,
    std::string_view mask, std::uint64_t extent, construction_value& value) noexcept {
    if (!extent || extent > (std::numeric_limits<std::uint32_t>::max)() || data.size() > extent)
        return server_status::project_configuration_invalid;
    while (!data.empty() && data.back() == 0) data.remove_suffix(1);
    string_id bytes, writes;
    auto status = data.empty() ? server_status::success : strings.intern(data, bytes);
    if (!succeeded(status)) return status;
    status = mask.empty() ? server_status::success : strings.intern(mask, writes);
    if (!succeeded(status)) return status;
    value = {bytes.value(), writes.value(), static_cast<std::uint32_t>(extent), construction_kind::byte_array};
    return server_status::success;
}

// A scalar edit updates the single array value, preserving both overwrite order
// and untouched constructor bytes. Only partial arrays need a bitmap.
template<class Graph>
server_status edit_char_array(Graph& graph, string_table& strings, object_endpoint target,
    std::uint64_t extent, std::uint64_t index, unsigned char byte,
    construction_value& value, bool& overwritten) noexcept {
    if (index >= extent || extent > (std::numeric_limits<std::uint32_t>::max)())
        return server_status::project_configuration_invalid;
    try {
        object_initialization_record previous;
        const bool exists = graph.initialization(target, previous);
        if (exists && previous.value.kind != construction_kind::byte_array)
            return server_status::project_configuration_invalid;
        std::string data = exists ? std::string{strings.spelling(previous.value.low)} : std::string{};
        const bool full = exists && previous.value.high == 0;
        std::string mask = exists && !full ? std::string{strings.spelling(previous.value.high)} : std::string{};
        overwritten = full || (index / 8 < mask.size() &&
            (static_cast<unsigned char>(mask[index / 8]) & (1u << (index % 8))));
        if (index >= data.size()) data.resize(static_cast<std::size_t>(index + 1), 0);
        data[static_cast<std::size_t>(index)] = static_cast<char>(byte);
        if (!full) {
            if (index / 8 >= mask.size()) mask.resize(static_cast<std::size_t>(index / 8 + 1), 0);
            mask[index / 8] = static_cast<char>(static_cast<unsigned char>(mask[index / 8]) | (1u << (index % 8)));
        }
        return char_array_value(strings, data, mask, extent, value);
    } catch (...) { return server_status::io_error; }
}

}
