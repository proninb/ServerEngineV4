/*
 * Final Project source provenance map.
 *
 * source_map stores unique physical/data contributions with uint32 root ownership
 * and file indexes, while preserving
 * the physical file_id that supplied each declaration/object/link token. G stays
 * free of file ownership. A compact reverse file index answers "what data is in
 * this physical file" without duplicating semantic payload.
 */
#pragma once

#include "../file/file_context.hpp"
#include "../graph/link_handle.hpp"
#include "../semantic/identity.hpp"
#include "../graph/graph.hpp"
#include <unordered_map>
#include "../../server_status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cw::server {

enum class source_data_kind : std::uint8_t {
    type_declaration = 0,
    type_definition = 1,
    object = 2,
    link = 3,
};

class source_data_ref final {
  public:
    constexpr source_data_ref() noexcept = default;

    [[nodiscard]] static constexpr source_data_ref type(identity_ref identity,
                                                        bool definition) noexcept {

        if (!identity || identity.kind() != identity_kind::type || identity.slot() > slot_mask) {

            return {};
        }

        return source_data_ref{
            (static_cast<std::uint32_t>(definition ? source_data_kind::type_definition
                                                   : source_data_kind::type_declaration)
             << kind_shift) |
            identity.slot()};
    }

    [[nodiscard]] static constexpr source_data_ref type_declaration(identity_ref id) noexcept {
        return type(id, false);
    }
    [[nodiscard]] static constexpr source_data_ref type_definition(identity_ref id) noexcept {
        return type(id, true);
    }

    [[nodiscard]] static constexpr source_data_ref object(identity_ref identity) noexcept {

        if (!identity || identity.kind() != identity_kind::object || identity.slot() > slot_mask) {

            return {};
        }

        return source_data_ref{
            (static_cast<std::uint32_t>(source_data_kind::object) << kind_shift) | identity.slot()};
    }

    [[nodiscard]] static constexpr source_data_ref link(link_handle value) noexcept {

        if (!value || value.value() > slot_mask) {

            return {};
        }

        return source_data_ref{(static_cast<std::uint32_t>(source_data_kind::link) << kind_shift) |
                               value.value()};
    }

    [[nodiscard]] static constexpr source_data_ref from_raw(std::uint32_t value) noexcept {

        return (value & slot_mask) != 0 ? source_data_ref{value} : source_data_ref{};
    }

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept {
        return value;
    }

    [[nodiscard]] constexpr source_data_kind kind() const noexcept {
        return static_cast<source_data_kind>(value >> kind_shift);
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

    friend constexpr bool operator==(source_data_ref, source_data_ref) noexcept = default;

    static constexpr std::uint32_t kind_shift = 30;
    static constexpr std::uint32_t slot_mask = 0x3fffffffu;

  private:
    explicit constexpr source_data_ref(std::uint32_t raw_value) noexcept : value(raw_value) {}

    std::uint32_t value = 0;
};

static_assert(sizeof(source_data_ref) == 4);

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

class source_map_file_view final {
  public:
    [[nodiscard]] bool empty() const noexcept {
        return indices.empty();
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return indices.size();
    }

    [[nodiscard]] source_contribution_record operator[](std::size_t index) const noexcept {

        if (index >= indices.size()) {
            return {};
        }

        const auto contribution = static_cast<std::size_t>(indices[index]);

        return contribution < contributions.size() ? contributions[contribution]
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
    friend bool operator==(const source_type_presence &, const source_type_presence &) = default;
};
static_assert(sizeof(source_type_presence) == 8);

// All indices are zero-based contribution IDs. Only the construction indexes
// below are mutable hash tables; none survive in the persisted representation.
class source_map final {
  public:
    [[nodiscard]] server_status reset(std::size_t file_count = 0) noexcept;
    [[nodiscard]] server_status begin_root(file_id root) noexcept;
    [[nodiscard]] server_status add(file_id file, source_data_ref data) noexcept;
    [[nodiscard]] server_status end_root() noexcept;
    [[nodiscard]] server_status finalize(std::size_t file_count, const graph &G) noexcept;
    [[nodiscard]] bool finalized() const noexcept {
        return finalized_value;
    }
    [[nodiscard]] source_map_file_view root(file_id id) const noexcept;
    [[nodiscard]] bool file(file_id id, source_map_file_view &output) const noexcept;
    [[nodiscard]] std::span<const source_map_range> root_entries() const noexcept {
        return root_ranges;
    }
    [[nodiscard]] std::span<const source_map_range> file_entries() const noexcept {
        return file_ranges;
    }
    [[nodiscard]] std::span<const source_contribution_record>
    contribution_entries() const noexcept {
        return contributions;
    }
    [[nodiscard]] std::span<const std::uint32_t> root_index_entries() const noexcept {
        return root_indices;
    }
    [[nodiscard]] std::span<const std::uint32_t> file_index_entries() const noexcept {
        return file_indices;
    }
    [[nodiscard]] std::span<const source_type_presence> type_presence_entries() const noexcept {
        return type_presence;
    }
    [[nodiscard]] std::span<const std::uint32_t> object_presence_entries() const noexcept {
        return object_presence;
    }
    [[nodiscard]] std::span<const std::uint32_t> link_presence_entries() const noexcept {
        return link_presence;
    }

  private:
    std::vector<source_map_range> root_ranges, file_ranges;
    std::vector<source_contribution_record> contributions;
    std::vector<std::uint32_t> root_indices, file_indices;
    std::vector<source_type_presence> type_presence;
    std::vector<std::uint32_t> object_presence, link_presence;
    std::unordered_map<std::uint64_t, std::uint32_t> canonical;
    std::unordered_map<std::uint64_t, std::uint32_t> root_seen;
    std::vector<bool> completed_roots;
    file_id active_root{};
    std::uint32_t active_root_begin = 0;
    bool finalized_value = false;
};

} // namespace cw::server
