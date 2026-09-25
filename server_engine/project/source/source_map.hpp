/*
 * Final Project source provenance and BUILD semantic dependency observations.
 *
 * Canonical Source Map ownership remains root-contiguous and is persisted in
 * compiled.bin. Semantic dependencies are BUILD-only acceleration state
 * persisted in source.bin; they never enter G and never alter the physical
 * File Context DAG.
 */
#pragma once

#include "../file/file_context.hpp"
#include "../graph/link_handle.hpp"
#include "../semantic/identity.hpp"
#include "../graph/graph.hpp"
#include "../../server_status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

namespace cw::server {

enum class source_data_kind : std::uint8_t {
    type_declaration = 0,
    type_definition = 1,
    object = 2,
    link = 3,
};

// Compact reference to one semantic datum in G used by source provenance.
class source_data_ref final {
public:
    constexpr source_data_ref() noexcept = default;

    [[nodiscard]] static constexpr source_data_ref type(
        identity_ref identity,
        bool definition) noexcept {

        if (!identity ||
            identity.kind() != identity_kind::type ||
            identity.slot() > slot_mask) {

            return {};
        }

        return source_data_ref{
            (static_cast<std::uint32_t>(
                 definition
                     ? source_data_kind::type_definition
                     : source_data_kind::type_declaration)
             << kind_shift) |
            identity.slot()};
    }

    [[nodiscard]] static constexpr source_data_ref type_declaration(
        identity_ref id) noexcept {

        return type(id, false);
    }

    [[nodiscard]] static constexpr source_data_ref type_definition(
        identity_ref id) noexcept {

        return type(id, true);
    }

    [[nodiscard]] static constexpr source_data_ref object(
        identity_ref identity) noexcept {

        if (!identity ||
            identity.kind() != identity_kind::object ||
            identity.slot() > slot_mask) {

            return {};
        }

        return source_data_ref{
            (static_cast<std::uint32_t>(
                 source_data_kind::object) << kind_shift) |
            identity.slot()};
    }

    [[nodiscard]] static constexpr source_data_ref link(
        link_handle value) noexcept {

        if (!value ||
            value.value() > slot_mask) {

            return {};
        }

        return source_data_ref{
            (static_cast<std::uint32_t>(
                 source_data_kind::link) << kind_shift) |
            value.value()};
    }

    [[nodiscard]] static constexpr source_data_ref from_raw(
        std::uint32_t value) noexcept {

        return (value & slot_mask) != 0
            ? source_data_ref{value}
            : source_data_ref{};
    }

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept {
        return value;
    }

    [[nodiscard]] constexpr source_data_kind kind() const noexcept {
        return static_cast<source_data_kind>(
            value >> kind_shift);
    }

    [[nodiscard]] constexpr std::uint32_t slot() const noexcept {
        return value & slot_mask;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return slot() != 0;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(
        source_data_ref,
        source_data_ref) noexcept = default;

    static constexpr std::uint32_t kind_shift = 30;
    static constexpr std::uint32_t slot_mask = 0x3fffffffu;

private:
    explicit constexpr source_data_ref(
        std::uint32_t raw_value) noexcept
        : value(raw_value) {
    }

    std::uint32_t value = 0;
};

static_assert(sizeof(source_data_ref) == 4);

enum class source_dependency_kind : std::uint8_t {
    type = 1,
    object = 2,
};

// BUILD-lineage reference to a semantic entity consumed by one semantic root.
class source_dependency_ref final {
public:
    constexpr source_dependency_ref() noexcept = default;

    [[nodiscard]] static constexpr source_dependency_ref type(
        type_handle value) noexcept {

        return value &&
            value.value() <= slot_mask
            ? source_dependency_ref{
                (static_cast<std::uint32_t>(
                     source_dependency_kind::type) << kind_shift) |
                value.value()}
            : source_dependency_ref{};
    }

    [[nodiscard]] static constexpr source_dependency_ref object(
        object_handle value) noexcept {

        return value &&
            value.value() <= slot_mask
            ? source_dependency_ref{
                (static_cast<std::uint32_t>(
                     source_dependency_kind::object) << kind_shift) |
                value.value()}
            : source_dependency_ref{};
    }

    [[nodiscard]] static constexpr source_dependency_ref from_raw(
        std::uint32_t value) noexcept {

        const auto kind =
            static_cast<source_dependency_kind>(
                value >> kind_shift);

        return (value & slot_mask) != 0 &&
            (kind == source_dependency_kind::type ||
             kind == source_dependency_kind::object)
            ? source_dependency_ref{value}
            : source_dependency_ref{};
    }

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept {
        return value;
    }

    [[nodiscard]] constexpr source_dependency_kind kind() const noexcept {
        return static_cast<source_dependency_kind>(
            value >> kind_shift);
    }

    [[nodiscard]] constexpr std::uint32_t slot() const noexcept {
        return value & slot_mask;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return slot() != 0 &&
            (kind() == source_dependency_kind::type ||
             kind() == source_dependency_kind::object);
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(
        source_dependency_ref,
        source_dependency_ref) noexcept = default;

    static constexpr std::uint32_t kind_shift = 30;
    static constexpr std::uint32_t slot_mask = 0x3fffffffu;

private:
    explicit constexpr source_dependency_ref(
        std::uint32_t raw_value) noexcept
        : value(raw_value) {
    }

    std::uint32_t value = 0;
};

static_assert(sizeof(source_dependency_ref) == 4);

[[nodiscard]] inline std::uint32_t source_dependency_hash(
    source_dependency_ref dependency) noexcept {

    auto value =
        dependency.raw();

    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;

    return value;
}

struct source_map_range final {
    std::uint32_t begin = 0;
    std::uint32_t count = 0;
};

static_assert(sizeof(source_map_range) == 8);

struct source_contribution_record final {
    file_id file{};
    source_data_ref data{};
};

static_assert(sizeof(source_contribution_record) == 8);

struct source_dependency_index_slot final {
    source_dependency_ref dependency{};
    source_map_range roots{};
};

static_assert(sizeof(source_dependency_index_slot) == 12);

// Read-only physical-file projection over canonical root-owned contributions.
class source_map_file_view final {
public:
    [[nodiscard]] bool empty() const noexcept {
        return indices.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return indices.size();
    }

    [[nodiscard]] source_contribution_record operator[](
        std::size_t index) const noexcept {

        if (index >= indices.size()) {
            return {};
        }

        const auto contribution =
            static_cast<std::size_t>(
                indices[index]);

        return contribution < contributions.size()
            ? contributions[contribution]
            : source_contribution_record{};
    }

private:
    friend class source_map;

    std::span<const source_contribution_record> contributions;
    std::span<const std::uint32_t> indices;
};

struct source_type_presence final {
    std::uint32_t declarations = 0;
    std::uint32_t definitions = 0;

    friend bool operator==(
        const source_type_presence&,
        const source_type_presence&) = default;
};

static_assert(sizeof(source_type_presence) == 8);

// Owns root-contiguous provenance plus root-local semantic dependency observations.
// finalize() derives physical provenance, semantic presence, and one sparse reverse
// dependency hash index sized by unique dependency targets; root adjacency is O(edges).
class source_map final {
public:
    void set_build_acceleration_capture(
        bool enabled) noexcept {
        capture_build_acceleration = enabled;
    }

    [[nodiscard]] server_status reset(
        std::size_t file_count = 0) noexcept;

    [[nodiscard]] server_status begin_root(
        file_id root) noexcept;

    [[nodiscard]] server_status add(
        file_id file,
        source_data_ref data) noexcept;

    [[nodiscard]] server_status add_dependency(
        type_handle type) noexcept;

    [[nodiscard]] server_status add_dependency(
        object_handle object) noexcept;

    [[nodiscard]] server_status end_root() noexcept;

    [[nodiscard]] server_status finalize(
        std::size_t file_count,
        const identity_space& identities,
        const graph& G) noexcept;

    [[nodiscard]] bool finalized() const noexcept {
        return finalized_value;
    }

    [[nodiscard]] std::span<const source_contribution_record> root(
        file_id id) const noexcept;

    [[nodiscard]] bool file(
        file_id id,
        source_map_file_view& output) const noexcept;

    [[nodiscard]] std::span<const source_dependency_ref> root_dependencies(
        file_id root) const noexcept;

    [[nodiscard]] std::span<const file_id> dependents(
        source_dependency_ref dependency) const noexcept;

    [[nodiscard]] std::span<const source_map_range>
    root_entries() const noexcept {
        return root_ranges;
    }

    [[nodiscard]] std::span<const source_map_range>
    file_entries() const noexcept {
        return file_ranges;
    }

    [[nodiscard]] std::span<const source_contribution_record>
    contribution_entries() const noexcept {
        return contributions;
    }

    [[nodiscard]] std::span<const std::uint32_t>
    file_index_entries() const noexcept {
        return file_indices;
    }

    [[nodiscard]] std::span<const source_map_range>
    root_dependency_entries() const noexcept {
        return root_dependency_ranges;
    }

    [[nodiscard]] std::span<const source_dependency_ref>
    dependency_entries() const noexcept {
        return dependencies;
    }

    [[nodiscard]] std::span<const source_dependency_index_slot>
    dependency_index_entries() const noexcept {
        return dependency_index;
    }

    [[nodiscard]] std::span<const file_id>
    dependent_root_entries() const noexcept {
        return dependent_roots;
    }

    [[nodiscard]] std::span<const source_type_presence>
    type_presence_entries() const noexcept {
        return type_presence;
    }

    [[nodiscard]] std::span<const std::uint32_t>
    object_presence_entries() const noexcept {
        return object_presence;
    }

    [[nodiscard]] std::span<const std::uint32_t>
    link_presence_entries() const noexcept {
        return link_presence;
    }

private:
    [[nodiscard]] server_status add_dependency(
        source_dependency_ref dependency) noexcept;

    struct contribution_slot final {
        std::uint64_t key = 0;
        std::uint32_t position = 0;
        std::uint32_t generation = 0;
    };

    static_assert(sizeof(contribution_slot) == 16);
    [[nodiscard]] static std::uint64_t contribution_hash(
        std::uint64_t key) noexcept;

    [[nodiscard]] std::size_t contribution_position(
        std::uint64_t key,
        std::uint64_t hash) const noexcept;

    void grow_contribution_index();
    void begin_contribution_generation() noexcept;

    std::vector<source_map_range> root_ranges;
    std::vector<source_map_range> file_ranges;
    std::vector<source_contribution_record> contributions;
    std::vector<std::uint32_t> file_indices;

    std::vector<source_map_range> root_dependency_ranges;
    std::vector<source_dependency_ref> dependencies;
    std::vector<source_dependency_index_slot> dependency_index;
    std::vector<file_id> dependent_roots;

    std::vector<source_type_presence> type_presence;
    std::vector<std::uint32_t> object_presence;
    std::vector<std::uint32_t> link_presence;

    // Scratch lookup only: persisted contributions keep their original order.
    std::vector<contribution_slot> root_seen;
    std::uint32_t root_seen_generation = 0;
    std::unordered_set<std::uint32_t> root_dependency_seen;
    std::vector<bool> completed_roots;

    file_id active_root{};
    std::uint32_t active_root_begin = 0;
    std::uint32_t active_dependency_begin = 0;
    bool capture_build_acceleration = true;
    bool finalized_value = false;
};

}
