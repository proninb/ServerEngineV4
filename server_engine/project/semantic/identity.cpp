#include "identity.hpp"

#include <limits>
#include <utility>

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

[[nodiscard]] constexpr std::uint64_t mix64(
    std::uint64_t value) noexcept {

    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value;
}

}

identity_space::identity_space(
    const string_table& strings) noexcept
    : strings(strings) {
}

std::uint64_t identity_space::hash_key(
    identity_ref parent,
    string_id name,
    identity_kind kind) noexcept {

    const auto value =
        static_cast<std::uint64_t>(
            parent.value()) |
        (static_cast<std::uint64_t>(
             name.value()) << 32);

    return mix64(
        value ^
        (static_cast<std::uint64_t>(
             static_cast<std::uint8_t>(kind)) *
         0x9e3779b97f4a7c15ULL));
}

std::uint32_t identity_space::fingerprint(
    std::uint64_t hash) noexcept {

    auto value =
        static_cast<std::uint32_t>(
            hash ^ (hash >> 32));

    return value == 0
        ? 1
        : value;
}

bool identity_space::contains(
    identity_ref identity) const noexcept {

    if (!identity) {
        return false;
    }

    if (identity.slot() == 1) {
        return identity.kind() ==
            identity_kind::root;
    }

    const auto index_value =
        static_cast<std::size_t>(
            identity.slot() - 2);

    return identity.slot() >= 2 &&
        index_value < records.size() &&
        index_value < kinds.size() &&
        kinds[index_value] ==
            identity.kind();
}

const identity_record* identity_space::record(
    identity_ref identity) const noexcept {

    if (!contains(identity)) {
        return nullptr;
    }

    if (identity.slot() == 1) {
        return &root_record;
    }

    return &records[
        identity.slot() - 2];
}

identity_ref identity_space::at_slot(
    std::uint32_t slot) const noexcept {

    if (slot == 1) {
        return root();
    }

    if (slot < 2) {
        return {};
    }

    const auto index_value =
        static_cast<std::size_t>(
            slot - 2);

    if (index_value >= records.size() ||
        index_value >= kinds.size()) {

        return {};
    }

    return identity_ref::make(
        slot,
        kinds[index_value]);
}

bool identity_space::same_key(
    identity_ref identity,
    identity_ref parent,
    string_id name,
    identity_kind kind) const noexcept {

    if (!contains(identity) ||
        identity.kind() != kind ||
        identity.slot() == 1) {

        return false;
    }

    const auto& value =
        records[
            identity.slot() - 2];

    return value.parent == parent &&
        value.name == name;
}

identity_ref identity_space::find_key(
    identity_ref parent,
    string_id name,
    identity_kind kind,
    std::uint64_t hash,
    std::uint32_t expected_fingerprint) const noexcept {

    if (index.empty()) {
        return {};
    }

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

        if (!slot.identity) {
            return {};
        }

        if (slot.fingerprint ==
                expected_fingerprint &&
            same_key(
                slot.identity,
                parent,
                name,
                kind)) {

            return slot.identity;
        }

        position =
            (position + 1) &
            mask;
    }

    return {};
}

identity_ref identity_space::find(
    identity_ref parent,
    string_id name,
    identity_kind kind) const noexcept {

    if (!contains(parent) ||
        !strings.contains(name) ||
        kind == identity_kind::root) {

        return {};
    }

    const auto hash =
        hash_key(
            parent,
            name,
            kind);

    return find_key(
        parent,
        name,
        kind,
        hash,
        fingerprint(hash));
}

void identity_space::insert_index(
    std::vector<identity_slot>& target,
    identity_ref identity,
    std::uint64_t hash,
    std::uint32_t value_fingerprint) const noexcept {

    const auto mask =
        target.size() - 1;

    auto position =
        static_cast<std::size_t>(
            hash) &
        mask;

    while (target[position].identity) {
        position =
            (position + 1) &
            mask;
    }

    target[position] = {
        value_fingerprint,
        identity,
    };
}

server_status identity_space::ensure_index_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
            records.size()) {

        return server_status::io_error;
    }

    const auto required =
        records.size() +
        additional;

    if (!index.empty() &&
        required <= index.size() / 2) {

        return server_status::success;
    }

    const auto capacity =
        next_index_capacity(
            required);

    if (capacity == 0) {
        return server_status::io_error;
    }

    try {
        std::vector<identity_slot> candidate(
            capacity);

        for (std::size_t index_value = 0;
             index_value < records.size();
             ++index_value) {

            const auto slot =
                static_cast<std::uint32_t>(
                    index_value + 2);

            const auto identity =
                at_slot(slot);

            const auto& value =
                records[index_value];

            const auto hash =
                hash_key(
                    value.parent,
                    value.name,
                    identity.kind());

            insert_index(
                candidate,
                identity,
                hash,
                fingerprint(hash));
        }

        index =
            std::move(candidate);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status identity_space::resolve(
    identity_ref parent,
    string_id name,
    identity_kind kind,
    identity_ref& output) noexcept {

    output = {};

    if (!contains(parent) ||
        !strings.contains(name) ||
        kind == identity_kind::root) {

        return server_status::
            project_configuration_invalid;
    }

    const auto hash =
        hash_key(
            parent,
            name,
            kind);

    const auto value_fingerprint =
        fingerprint(hash);

    if (const auto existing =
            find_key(
                parent,
                name,
                kind,
                hash,
                value_fingerprint);
        existing) {

        output =
            existing;

        return server_status::success;
    }

    const auto prepared =
        ensure_index_capacity(1);

    if (!succeeded(prepared)) {
        return prepared;
    }

    if (records.size() >=
        static_cast<std::size_t>(
            identity_ref::maximum_slot - 1)) {

        return server_status::io_error;
    }

    const auto slot =
        static_cast<std::uint32_t>(
            records.size() + 2);

    const auto identity =
        identity_ref::make(
            slot,
            kind);

    if (!identity) {
        return server_status::io_error;
    }

    const auto old_record_count =
        records.size();

    const auto old_kind_count =
        kinds.size();

    try {
        records.push_back({
            parent,
            name,
        });

        try {
            kinds.push_back(
                kind);
        }
        catch (...) {
            records.resize(
                old_record_count);

            throw;
        }

        insert_index(
            index,
            identity,
            hash,
            value_fingerprint);

        output =
            identity;

        return server_status::success;
    }
    catch (...) {
        records.resize(
            old_record_count);

        kinds.resize(
            old_kind_count);

        output = {};

        return server_status::io_error;
    }
}

}

