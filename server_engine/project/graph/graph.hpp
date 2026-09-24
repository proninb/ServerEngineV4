/*
 * Final compiled semantic Graph.
 *
 * REBUILD owns dense local storage. BUILD may bind an immutable compiled.bin
 * baseline and keep only sparse baseline patches plus append-only new slots.
 * Graph handles remain stable for the BUILD lineage; REBUILD is the compaction
 * boundary.
 */
#pragma once

#include "construction_value.hpp"
#include "link_handle.hpp"
#include "member_index.hpp"
#include "object_handle.hpp"
#include "type_handle.hpp"
#include "type_ref.hpp"
#include "../semantic/identity.hpp"
#include "../../server_status.hpp"
#include "../../string_id.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace cw::server {

class compiled_project_view;

struct graph_range final {
    std::uint32_t begin = 0;
    std::uint32_t count = 0;
};

static_assert(sizeof(graph_range) == 8);

enum class graph_type_kind : std::uint8_t {
    record = 1,
};

enum class graph_record_kind : std::uint8_t {
    struct_type = 1,
    class_type = 2,
    union_type = 3,
};

enum class graph_member_access : std::uint8_t {
    public_access = 1,
    protected_access = 2,
    private_access = 3,
};

inline constexpr std::uint16_t graph_type_defined =
    0x0001u;

struct type_entry final {
    graph_range members;
    graph_type_kind kind =
        graph_type_kind::record;
    graph_record_kind record_kind =
        graph_record_kind::struct_type;
    std::uint16_t flags = 0;

    [[nodiscard]] constexpr bool defined() const noexcept {
        return (flags & graph_type_defined) != 0;
    }
};

static_assert(sizeof(type_entry) == 12);
static_assert(std::is_trivially_copyable_v<type_entry>);

struct member_record final {
    string_id name{};
    type_ref type{};
    graph_member_access access =
        graph_member_access::public_access;
    std::uint8_t reserved[3]{};
};

static_assert(sizeof(member_record) == 12);
static_assert(std::is_trivially_copyable_v<member_record>);

inline constexpr std::uint32_t graph_object_construction_slot_mask =
    0x3fffffffu;

inline constexpr std::uint32_t graph_object_non_default_initializer =
    0x40000000u;

inline constexpr std::uint32_t graph_object_internal_static =
    0x80000000u;

inline constexpr std::uint32_t graph_object_flag_mask =
    graph_object_non_default_initializer |
    graph_object_internal_static;

struct object_entry final {
    type_ref type{};
    std::uint32_t state = 0;

    [[nodiscard]] constexpr bool non_default_initializer() const noexcept {
        return (state & graph_object_non_default_initializer) != 0;
    }

    [[nodiscard]] constexpr bool internal_static() const noexcept {
        return (state & graph_object_internal_static) != 0;
    }

    [[nodiscard]] constexpr std::uint32_t construction_slot() const noexcept {
        return state & graph_object_construction_slot_mask;
    }
};

static_assert(sizeof(object_entry) == 8);
static_assert(std::is_trivially_copyable_v<object_entry>);

struct object_endpoint final {
    object_handle object{};
    member_index member{};

    friend constexpr bool operator==(
        const object_endpoint&,
        const object_endpoint&) noexcept = default;
};

static_assert(sizeof(object_endpoint) == 8);

struct link_record final {
    object_endpoint source{};
    object_endpoint target{};
};

static_assert(sizeof(link_record) == 16);
static_assert(std::is_trivially_copyable_v<link_record>);

// One logical G. In BUILD mode unchanged slots are read directly from the
// compiled baseline; only invalidated/replayed slots allocate overlay state.
class graph final {
public:
    graph() = default;

    graph(const graph&) = delete;
    graph& operator=(const graph&) = delete;

    graph(graph&&) noexcept = default;
    graph& operator=(graph&&) noexcept = default;

    [[nodiscard]] server_status bind_baseline(
        const compiled_project_view& baseline) noexcept;

    [[nodiscard]] bool baseline_bound() const noexcept {
        return baseline != nullptr;
    }

    [[nodiscard]] server_status declare_record(
        identity_ref identity,
        graph_record_kind kind,
        type_handle& output) noexcept;

    [[nodiscard]] server_status define_record(
        type_handle type,
        graph_record_kind kind,
        std::span<const member_record> definition,
        std::span<const construction_value> construction = {}) noexcept;

    [[nodiscard]] server_status clear_definition(
        type_handle type) noexcept;

    [[nodiscard]] server_status retire(
        type_handle type) noexcept;

    [[nodiscard]] server_status add_object(
        identity_ref identity,
        type_ref type,
        object_handle& output,
        std::uint32_t flags = 0,
        construction_value construction = {}) noexcept;

    [[nodiscard]] server_status retire(
        object_handle object) noexcept;

    [[nodiscard]] server_status add_link(
        object_endpoint source,
        object_endpoint target,
        link_handle& output) noexcept;

    [[nodiscard]] server_status retire(
        link_handle link) noexcept;

    [[nodiscard]] type_ref intrinsic(
        intrinsic_type type) const noexcept;

    [[nodiscard]] type_ref named(
        type_handle type) const noexcept;

    [[nodiscard]] server_status derive(
        type_ref child,
        derived_type_kind kind,
        std::uint64_t payload,
        type_ref& output) noexcept;

    [[nodiscard]] bool slot_exists(
        type_handle type) const noexcept;

    [[nodiscard]] bool slot_exists(
        object_handle object) const noexcept;

    [[nodiscard]] bool slot_exists(
        link_handle link) const noexcept;

    [[nodiscard]] bool contains(
        type_handle type) const noexcept;

    [[nodiscard]] bool contains(
        object_handle object) const noexcept;

    [[nodiscard]] bool contains(
        link_handle link) const noexcept;

    [[nodiscard]] bool contains(
        type_ref type) const noexcept;

    [[nodiscard]] bool type(
        type_handle type,
        type_entry& output) const noexcept;

    [[nodiscard]] bool object(
        object_handle object,
        object_entry& output) const noexcept;

    [[nodiscard]] bool link(
        link_handle link,
        link_record& output) const noexcept;

    // Pointer access is retained for dense/local construction code. Logical
    // baseline-capable code must use the by-value accessors above.
    [[nodiscard]] const type_entry* find(
        type_handle type) const noexcept;

    [[nodiscard]] const object_entry* find(
        object_handle object) const noexcept;

    [[nodiscard]] const link_record* find(
        link_handle link) const noexcept;

    [[nodiscard]] type_handle find_type(
        identity_ref identity) const noexcept;

    [[nodiscard]] object_handle find_object(
        identity_ref identity) const noexcept;

    [[nodiscard]] identity_ref identity(
        type_handle type) const noexcept;

    [[nodiscard]] identity_ref identity(
        object_handle object) const noexcept;

    [[nodiscard]] std::span<const member_record> members(
        type_handle type) const noexcept;

    [[nodiscard]] member_index find_member(
        type_handle type,
        string_id name) const noexcept;

    [[nodiscard]] const member_record* member(
        type_handle type,
        member_index member) const noexcept;

    [[nodiscard]] bool member(
        type_handle type,
        member_index member,
        member_record& output) const noexcept;

    [[nodiscard]] const construction_value* construction(
        type_handle type,
        member_index member) const noexcept;

    [[nodiscard]] bool construction(
        type_handle type,
        member_index member,
        construction_value& output) const noexcept;

    [[nodiscard]] bool construction(
        object_handle object,
        construction_value& output) const noexcept;

    [[nodiscard]] object_handle object_at(
        std::size_t index) const noexcept {

        if (index >= object_count()) {
            return {};
        }

        const object_handle output{
            static_cast<std::uint32_t>(
                index + 1)};

        return contains(output)
            ? output
            : object_handle{};
    }

    [[nodiscard]] bool intrinsic(
        type_ref type,
        intrinsic_type& output) const noexcept;

    [[nodiscard]] bool named(
        type_ref type,
        type_handle& output) const noexcept;

    [[nodiscard]] bool derived(
        type_ref type,
        derived_type_record& output) const noexcept;

    // Count means lineage slot count. Slots are never compacted by BUILD.
    [[nodiscard]] std::size_t type_count() const noexcept {
        return baseline_type_count +
            types.size();
    }

    [[nodiscard]] std::size_t live_type_count() const noexcept {
        return live_type_count_value;
    }

    [[nodiscard]] std::size_t member_count() const noexcept {
        return baseline_member_count +
            member_records.size();
    }

    [[nodiscard]] std::size_t object_count() const noexcept {
        return baseline_object_count +
            objects.size();
    }

    [[nodiscard]] std::size_t live_object_count() const noexcept {
        return live_object_count_value;
    }

    [[nodiscard]] std::size_t link_count() const noexcept {
        return baseline_link_count +
            links.size();
    }

    [[nodiscard]] std::size_t live_link_count() const noexcept {
        return live_link_count_value;
    }

    [[nodiscard]] std::size_t derived_type_count() const noexcept {
        return baseline_derived_count +
            derived_types.size();
    }

    // Dense construction spans. In BUILD baseline mode these expose only the
    // append arena; final sparse persistence uses logical access instead.
    [[nodiscard]] std::span<const type_entry>
    type_entries() const noexcept {
        return types;
    }

    [[nodiscard]] std::span<const identity_ref>
    type_identity_entries() const noexcept {
        return type_identities;
    }

    [[nodiscard]] std::span<const member_record>
    member_entries() const noexcept {
        return member_records;
    }

    [[nodiscard]] std::span<const construction_value>
    member_construction_entries() const noexcept {
        return member_construction;
    }

    [[nodiscard]] std::span<const object_entry>
    object_entries() const noexcept {
        return objects;
    }

    [[nodiscard]] std::span<const identity_ref>
    object_identity_entries() const noexcept {
        return object_identities;
    }

    [[nodiscard]] std::span<const construction_value>
    object_construction_entries() const noexcept {
        return object_construction;
    }

    [[nodiscard]] std::span<const link_record>
    link_entries() const noexcept {
        return links;
    }

    [[nodiscard]] std::span<const derived_type_record>
    derived_type_entries() const noexcept {
        return derived_types;
    }

private:
    enum class location_kind : std::uint8_t {
        none = 0,
        type = 1,
        object = 2,
    };

    struct derived_index_slot final {
        std::uint32_t fingerprint = 0;
        type_ref type{};
    };

    struct sparse_index_slot final {
        std::uint32_t key = 0;
        std::uint32_t value = 0;
    };

    class sparse_index final {
    public:
        [[nodiscard]] bool empty() const noexcept {
            return count == 0;
        }

        [[nodiscard]] std::uint32_t find(
            std::uint32_t key) const noexcept;

        [[nodiscard]] server_status insert(
            std::uint32_t key,
            std::uint32_t value) noexcept;

    private:
        [[nodiscard]] server_status ensure_capacity(
            std::size_t additional) noexcept;

        std::vector<sparse_index_slot> slots;
        std::size_t count = 0;
    };

    struct type_patch final {
        std::uint32_t slot = 0;
        type_entry value;
        bool live = true;
    };

    struct object_patch final {
        std::uint32_t slot = 0;
        object_entry value;
        construction_value construction;
        bool live = true;
    };

    struct link_patch final {
        std::uint32_t slot = 0;
        link_record value;
        bool live = true;
    };

    struct link_target_index_slot final {
        std::uint64_t key = 0;
        link_handle link{};
    };

    static_assert(sizeof(derived_index_slot) == 8);

    [[nodiscard]] static std::uint32_t encode_location(
        location_kind kind,
        std::uint32_t slot) noexcept;

    [[nodiscard]] static location_kind decode_location_kind(
        std::uint32_t value) noexcept;

    [[nodiscard]] static std::uint32_t decode_location_slot(
        std::uint32_t value) noexcept;

    [[nodiscard]] server_status ensure_identity_slot(
        identity_ref identity) noexcept;

    [[nodiscard]] server_status publish_identity_location(
        identity_ref identity,
        location_kind kind,
        std::uint32_t slot) noexcept;

    [[nodiscard]] std::uint32_t lineage_location(
        identity_ref identity) const noexcept;

    [[nodiscard]] type_handle lineage_type(
        identity_ref identity) const noexcept;

    [[nodiscard]] object_handle lineage_object(
        identity_ref identity) const noexcept;

    [[nodiscard]] const type_patch* find_type_patch(
        std::uint32_t slot) const noexcept;

    [[nodiscard]] type_patch* find_type_patch(
        std::uint32_t slot) noexcept;

    [[nodiscard]] server_status ensure_type_patch(
        type_handle type,
        type_patch*& output) noexcept;

    [[nodiscard]] const object_patch* find_object_patch(
        std::uint32_t slot) const noexcept;

    [[nodiscard]] object_patch* find_object_patch(
        std::uint32_t slot) noexcept;

    [[nodiscard]] server_status ensure_object_patch(
        object_handle object,
        object_patch*& output) noexcept;

    [[nodiscard]] const link_patch* find_link_patch(
        std::uint32_t slot) const noexcept;

    [[nodiscard]] link_patch* find_link_patch(
        std::uint32_t slot) noexcept;

    [[nodiscard]] server_status ensure_link_patch(
        link_handle link,
        link_patch*& output) noexcept;

    [[nodiscard]] static std::uint64_t hash_derived(
        type_ref child,
        derived_type_kind kind,
        std::uint64_t payload) noexcept;

    [[nodiscard]] static std::uint32_t fingerprint(
        std::uint64_t hash) noexcept;

    [[nodiscard]] type_ref find_derived(
        type_ref child,
        derived_type_kind kind,
        std::uint64_t payload,
        std::uint64_t hash,
        std::uint32_t fingerprint) const noexcept;

    [[nodiscard]] server_status ensure_derived_index_capacity(
        std::size_t additional) noexcept;

    void insert_derived_index(
        std::vector<derived_index_slot>& target,
        type_ref type,
        std::uint64_t hash,
        std::uint32_t fingerprint) const noexcept;

    [[nodiscard]] static std::uint64_t link_target_key(
        object_endpoint target) noexcept;

    [[nodiscard]] server_status ensure_link_target_index_capacity(
        std::size_t additional) noexcept;

    void insert_link_target_index(
        std::vector<link_target_index_slot>& target,
        std::uint64_t key,
        link_handle link) const noexcept;

    [[nodiscard]] link_handle find_local_link_target(
        object_endpoint target) const noexcept;

    [[nodiscard]] link_handle lineage_link_target(
        object_endpoint target) const noexcept;

    [[nodiscard]] bool endpoint_valid(
        object_endpoint endpoint) const noexcept;

    const compiled_project_view* baseline = nullptr;

    std::size_t baseline_type_count = 0;
    std::size_t baseline_member_count = 0;
    std::size_t baseline_object_count = 0;
    std::size_t baseline_object_construction_count = 0;
    std::size_t baseline_link_count = 0;
    std::size_t baseline_derived_count = 0;

    std::size_t live_type_count_value = 0;
    std::size_t live_object_count_value = 0;
    std::size_t live_link_count_value = 0;

    // Fresh REBUILD uses this dense identity locator. BUILD keeps baseline
    // identity lookup mmap-native and stores only appended locations here.
    std::vector<std::uint32_t> identity_locations;
    sparse_index identity_overlay;

    std::vector<type_patch> type_patches;
    sparse_index type_patch_index;

    std::vector<object_patch> object_patches;
    sparse_index object_patch_index;

    std::vector<link_patch> link_patches;
    sparse_index link_patch_index;

    std::vector<type_entry> types;
    std::vector<identity_ref> type_identities;
    std::vector<std::uint8_t> type_live;

    std::vector<member_record> member_records;
    std::vector<construction_value> member_construction;

    std::vector<object_entry> objects;
    std::vector<identity_ref> object_identities;
    std::vector<std::uint8_t> object_live;
    std::vector<construction_value> object_construction;

    std::vector<link_record> links;
    std::vector<std::uint8_t> link_live;
    std::vector<link_target_index_slot> link_target_index;
    std::size_t link_target_index_count = 0;

    std::vector<derived_type_record> derived_types;
    std::vector<derived_index_slot> derived_index;
};

}
