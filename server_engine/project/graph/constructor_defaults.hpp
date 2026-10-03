#pragma once

#include "construction_value.hpp"
#include "../semantic/identity.hpp"
#include "../../server_status.hpp"
#include "../../string_id.hpp"
#include <algorithm>
#include <charconv>
#include <span>
#include <string_view>
#include <vector>

namespace cw::server {
// A scalar constructor override owned by a containing type. Path spelling is
// interned (field names and decimal array indices), never an ABI offset or process pointer.
struct constructor_default final {
    identity_ref owner{};
    string_id path{};
    construction_value value{};
};
static_assert(sizeof(constructor_default) == 24);

// Shared decoder for persisted paths. Validation and Runtime use the same grammar.
struct constructor_path_reader final {
    std::string_view remaining;
    bool first = true;
    bool next(std::string_view& name, std::uint64_t& index) noexcept {
        name = {};
        index = 0;
        if (remaining.empty()) { return false; }
        if (!first && remaining.front() == '[') {
            const auto close = remaining.find(']');
            if (close == std::string_view::npos) { return false; }
            const auto digits = remaining.substr(1, close - 1);
            const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), index);
            if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) { return false; }
            remaining.remove_prefix(close + 1);
            return true;
        }
        if (!first) {
            if (remaining.front() != '.') { return false; }
            remaining.remove_prefix(1);
        }
        first = false;
        const auto end = remaining.find_first_of(".[");
        name = remaining.substr(0, end);
        if (name.empty()) { return false; }
        remaining.remove_prefix(name.size());
        return true;
    }
};

class constructor_default_table final {
public:
    std::span<const constructor_default> entries() const noexcept { return values; }
    server_status add(constructor_default value) noexcept {
        if (!value.owner || !value.path || !valid_construction(value.value)) {
            return server_status::project_configuration_invalid;
        }
        const auto less = [](const constructor_default& a, const constructor_default& b) {
            return a.owner.value() < b.owner.value() ||
                (a.owner == b.owner && a.path.value() < b.path.value());
        };
        const auto position = std::lower_bound(values.begin(), values.end(), value, less);
        if (position != values.end() && position->owner == value.owner && position->path == value.path) {
            return position->value == value.value ? server_status::success
                : server_status::project_configuration_invalid;
        }
        try { values.insert(position, value); }
        catch (...) { return server_status::io_error; }
        return server_status::success;
    }
    void erase(identity_ref owner) noexcept {
        std::erase_if(values, [owner](const auto& value) { return value.owner == owner; });
    }
private:
    std::vector<constructor_default> values;
};
}
