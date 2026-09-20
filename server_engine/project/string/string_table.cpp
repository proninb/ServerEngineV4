#include "string_table.hpp"

#include <functional>
#include <limits>
#include <string>

namespace cw::server {
namespace {

[[nodiscard]] std::size_t next_index_capacity(
    std::size_t required) noexcept {

    if (required == 0) {
        return 0;
    }

    constexpr std::size_t minimum_capacity = 8;

    const auto maximum =
        (std::numeric_limits<std::size_t>::max)();

    if (required > maximum / 2) {
        return 0;
    }

    const auto minimum =
        required * 2;

    std::size_t capacity =
        minimum_capacity;

    while (capacity < minimum) {
        if (capacity > maximum / 2) {
            return 0;
        }

        capacity *= 2;
    }

    return capacity;
}

}

std::uint32_t string_table::hash_text(
    std::string_view value) noexcept {

    std::uint32_t hash = 2166136261u;

    for (const auto character : value) {
        hash ^=
            static_cast<std::uint8_t>(
                static_cast<unsigned char>(
                    character));

        hash *= 16777619u;
    }

    return hash == 0
        ? 1
        : hash;
}

bool string_table::contains(
    string_id id) const noexcept {

    return id &&
        id.value() <= records.size();
}

std::string_view string_table::get(
    string_id id) const noexcept {

    if (!contains(id)) {
        return {};
    }

    const auto& record =
        records[id.value() - 1];

    if (record.offset > bytes.size() ||
        record.length >
            bytes.size() - record.offset) {

        return {};
    }

    return {
        bytes.data() + record.offset,
        record.length,
    };
}

string_id string_table::find(
    std::string_view value) const noexcept {

    if (value.empty() ||
        index.empty()) {

        return {};
    }

    const auto hash =
        hash_text(value);

    const auto mask =
        index.size() - 1;

    auto position =
        static_cast<std::size_t>(hash) &
        mask;

    for (std::size_t probe = 0;
         probe < index.size();
         ++probe) {

        const auto& slot =
            index[position];

        if (!slot.id) {
            return {};
        }

        if (slot.hash == hash &&
            get(slot.id) == value) {

            return slot.id;
        }

        position =
            (position + 1) &
            mask;
    }

    return {};
}

void string_table::insert_index(
    std::vector<string_slot>& target,
    string_id id,
    std::uint32_t hash) const noexcept {

    const auto mask =
        target.size() - 1;

    auto position =
        static_cast<std::size_t>(hash) &
        mask;

    while (target[position].id) {
        position =
            (position + 1) &
            mask;
    }

    target[position] = {
        hash,
        id,
    };
}

server_status string_table::ensure_index_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
            records.size()) {

        return server_status::io_error;
    }

    const auto required =
        records.size() + additional;

    if (!index.empty() &&
        required <= index.size() / 2) {

        return server_status::success;
    }

    const auto capacity =
        next_index_capacity(required);

    if (capacity == 0) {
        return server_status::io_error;
    }

    try {
        std::vector<string_slot> candidate(
            capacity);

        for (std::size_t current = 0;
             current < records.size();
             ++current) {

            insert_index(
                candidate,
                string_id{
                    static_cast<std::uint32_t>(
                        current + 1)},
                records[current].hash);
        }

        index =
            std::move(candidate);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status string_table::intern(
    std::string_view value,
    string_id& output) noexcept {

    output = {};

    if (value.empty()) {
        return server_status::
            project_configuration_invalid;
    }

    if (const auto existing = find(value);
        existing) {

        output = existing;
        return server_status::success;
    }

    const auto prepared =
        ensure_index_capacity(1);

    if (!succeeded(prepared)) {
        return prepared;
    }

    const auto maximum_u32 =
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)());

    if (records.size() >= maximum_u32 ||
        value.size() > maximum_u32 ||
        bytes.size() > maximum_u32 ||
        value.size() >
            maximum_u32 - bytes.size()) {

        return server_status::io_error;
    }

    const auto hash =
        hash_text(value);

    std::string stable_value;

    if (!bytes.empty() &&
        value.data() != nullptr) {

        const auto* arena_begin =
            bytes.data();

        const auto* arena_end =
            arena_begin + bytes.size();

        const auto* value_begin =
            value.data();

        const std::less<const char*> less;

        if (!less(value_begin, arena_begin) &&
            less(value_begin, arena_end) &&
            value.size() <=
                static_cast<std::size_t>(
                    arena_end - value_begin)) {

            try {
                stable_value.assign(
                    value.data(),
                    value.size());
            }
            catch (...) {
                return server_status::io_error;
            }

            value = stable_value;
        }
    }

    const auto old_byte_count =
        bytes.size();

    const auto old_record_count =
        records.size();

    try {
        bytes.insert(
            bytes.end(),
            value.begin(),
            value.end());

        records.push_back({
            static_cast<std::uint32_t>(
                old_byte_count),
            static_cast<std::uint32_t>(
                value.size()),
            hash,
        });

        output = string_id{
            static_cast<std::uint32_t>(
                records.size())};

        insert_index(
            index,
            output,
            records.back().hash);

        return server_status::success;
    }
    catch (...) {
        records.resize(
            old_record_count);

        bytes.resize(
            old_byte_count);

        output = {};
        return server_status::io_error;
    }
}

}
