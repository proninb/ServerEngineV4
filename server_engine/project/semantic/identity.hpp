/*
 * Project semantic identity foundation.
 *
 * identity_space owns deterministic Project-local WHO identity for REBUILD.
 * It canonicalizes (parent, string_id, identity_kind) without carrying
 * declaration state, source locations, ABI data, Graph handles, or persistence.
 */
#pragma once

#include "../string/string_table.hpp"
#include "../../server_status.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cw::server {

enum class identity_kind : std::uint8_t {
    root = 0,
    namespace_scope = 1,
    type = 2,
    object = 3,
};

class identity_space;

class identity_ref final {
public:
    constexpr identity_ref() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return packed;
    }

    [[nodiscard]] constexpr std::uint32_t slot() const noexcept {
        return packed & slot_mask;
    }

    [[nodiscard]] constexpr identity_kind kind() const noexcept {
        return static_cast<identity_kind>(
            packed >> kind_shift);
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return slot() != 0;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(
        identity_ref,
        identity_ref) noexcept = default;

    static constexpr std::uint32_t kind_shift = 30;
    static constexpr std::uint32_t slot_mask = 0x3fffffffu;
    static constexpr std::uint32_t maximum_slot = slot_mask;

private:
    [[nodiscard]] static constexpr identity_ref make(
        std::uint32_t slot,
        identity_kind kind) noexcept {

        return slot != 0 &&
            slot <= maximum_slot
            ? identity_ref{
                (static_cast<std::uint32_t>(kind) << kind_shift) |
                slot}
            : identity_ref{};
    }

    explicit constexpr identity_ref(
        std::uint32_t value) noexcept
        : packed(value) {
    }

    std::uint32_t packed = 0;

    friend class identity_space;
};

static_assert(sizeof(identity_ref) == 4);

struct identity_record final {
    identity_ref parent{};
    string_id name{};
};

static_assert(sizeof(identity_record) == 8);

// Fresh-REBUILD semantic identity space. Lookup acceleration is construction
// state only; numeric identity_ref assignment follows deterministic caller order.
class identity_space final {
public:
    explicit identity_space(
        const string_table& strings) noexcept;

    identity_space(const identity_space&) = delete;
    identity_space& operator=(const identity_space&) = delete;

    [[nodiscard]] identity_ref root() const noexcept {
        return identity_ref::make(
            1,
            identity_kind::root);
    }

    [[nodiscard]] server_status resolve(
        identity_ref parent,
        string_id name,
        identity_kind kind,
        identity_ref& output) noexcept;

    [[nodiscard]] identity_ref find(
        identity_ref parent,
        string_id name,
        identity_kind kind) const noexcept;

    [[nodiscard]] bool contains(
        identity_ref identity) const noexcept;

    [[nodiscard]] const identity_record* record(
        identity_ref identity) const noexcept;

    [[nodiscard]] identity_ref at_slot(
        std::uint32_t slot) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return records.size();
    }

private:
    struct identity_slot final {
        std::uint32_t fingerprint = 0;
        identity_ref identity{};
    };

    static_assert(sizeof(identity_slot) == 8);

    [[nodiscard]] static std::uint64_t hash_key(
        identity_ref parent,
        string_id name,
        identity_kind kind) noexcept;

    [[nodiscard]] static std::uint32_t fingerprint(
        std::uint64_t hash) noexcept;

    [[nodiscard]] bool same_key(
        identity_ref identity,
        identity_ref parent,
        string_id name,
        identity_kind kind) const noexcept;

    [[nodiscard]] identity_ref find_key(
        identity_ref parent,
        string_id name,
        identity_kind kind,
        std::uint64_t hash,
        std::uint32_t fingerprint) const noexcept;

    [[nodiscard]] server_status ensure_index_capacity(
        std::size_t additional) noexcept;

    void insert_index(
        std::vector<identity_slot>& target,
        identity_ref identity,
        std::uint64_t hash,
        std::uint32_t fingerprint) const noexcept;

    const string_table& strings;
    std::vector<identity_record> records;
    std::vector<identity_kind> kinds;
    std::vector<identity_slot> index;
};

}

