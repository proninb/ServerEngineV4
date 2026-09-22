#include "string_table.hpp"

#include "../persistence/compiled_project.hpp"

#include <cstdint>
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

server_status string_table::bind_baseline(
    const compiled_project_view& baseline_value) noexcept {

    if (!baseline_value.valid() ||
        baseline != nullptr ||
        !records.empty() ||
        !bytes.empty() ||
        !index.empty()) {

        return server_status::
            project_artifact_invalid;
    }

    const auto maximum_u32 =
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)());

    if (baseline_value.string_count() >
            maximum_u32 ||
        baseline_value.string_byte_size() >
            maximum_u32) {

        return server_status::
            project_artifact_invalid;
    }

    baseline =
        &baseline_value;

    baseline_string_count =
        baseline_value.string_count();

    baseline_byte_count =
        baseline_value.string_byte_size();

    return server_status::success;
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

    if (!id) {
        return false;
    }

    if (baseline != nullptr &&
        id.value() <=
            baseline_string_count) {

        return !baseline->string(id).empty();
    }

    if (id.value() <=
        baseline_string_count) {

        return false;
    }

    const auto local =
        static_cast<std::size_t>(
            id.value()) -
        baseline_string_count -
        1;

    return local < records.size();
}

std::string_view string_table::get(
    string_id id) const noexcept {

    if (!id) {
        return {};
    }

    if (baseline != nullptr &&
        id.value() <=
            baseline_string_count) {

        return baseline->string(id);
    }

    if (id.value() <=
        baseline_string_count) {

        return {};
    }

    const auto local =
        static_cast<std::size_t>(
            id.value()) -
        baseline_string_count -
        1;

    if (local >= records.size()) {
        return {};
    }

    const auto& record =
        records[local];

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

std::string_view string_table::spelling(
    std::uint32_t slot) const noexcept {

    if (slot == 0 ||
        slot > size()) {

        return {};
    }

    return get(
        string_id{slot});
}

string_id string_table::find(
    std::string_view value) const noexcept {

    if (value.empty()) {
        return {};
    }

    if (!index.empty()) {
        const auto hash =
            hash_text(value);

        const auto mask =
            index.size() - 1;

        auto position =
            static_cast<std::size_t>(
                hash) &
            mask;

        for (std::size_t probe = 0;
             probe < index.size();
             ++probe) {

            const auto& slot =
                index[position];

            if (!slot.id) {
                break;
            }

            if (slot.hash == hash &&
                get(slot.id) == value) {

                return slot.id;
            }

            position =
                (position + 1) &
                mask;
        }
    }

    return baseline != nullptr
        ? baseline->find_string(value)
        : string_id{};
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
                        baseline_string_count +
                        current +
                        1)},
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

    if (baseline_string_count >
            maximum_u32 ||
        records.size() >=
            maximum_u32 -
                baseline_string_count ||
        value.size() > maximum_u32 ||
        baseline_byte_count >
            maximum_u32 ||
        bytes.size() >
            maximum_u32 -
                baseline_byte_count ||
        value.size() >
            maximum_u32 -
                baseline_byte_count -
                bytes.size()) {

        return server_status::io_error;
    }

    const auto hash =
        hash_text(value);

    std::string stable_value;

    if (!bytes.empty() &&
        value.data() != nullptr) {

        const auto arena_begin =
            reinterpret_cast<std::uintptr_t>(
                bytes.data());

        const auto value_begin =
            reinterpret_cast<std::uintptr_t>(
                value.data());

        if (value_begin >= arena_begin) {
            const auto offset =
                value_begin - arena_begin;

            if (offset < bytes.size() &&
                value.size() <=
                    bytes.size() -
                        static_cast<std::size_t>(
                            offset)) {

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
                baseline_string_count +
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
