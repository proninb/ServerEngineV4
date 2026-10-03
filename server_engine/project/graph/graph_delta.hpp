/*
 * BUILD-local sparse semantic Graph delta.
 *
 * Unchanged state stays in the immutable compiled_project_view mmap baseline.
 * Existing top-level type/object/link WHERE is stable between REBUILDs:
 * updates patch that slot, deletions mark it inactive, and only new semantic
 * owners allocate new top-level slots. BUILD-local replacement buffers do not
 * prescribe persisted placement; writers reuse assigned physical storage when possible.
 * graph_delta is BUILD operation state, never resident G.
 */
#pragma once

#include "graph.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cw::server {

class compiled_project_view;

enum class graph_delta_change_kind : std::uint8_t {
    patch = 1,
    append = 2,
};

struct graph_delta_type_change final {
    type_handle handle{};
    identity_ref identity{};
    type_entry value{};
    graph_delta_change_kind kind =
        graph_delta_change_kind::patch;
    bool live = false;
};

struct graph_delta_object_change final {
    object_handle handle{};
    identity_ref identity{};
    object_entry value{};
    construction_value construction{};
    graph_delta_change_kind kind =
        graph_delta_change_kind::patch;
    bool live = false;
};

struct graph_delta_link_change final {
    link_handle handle{};
    link_record value{};
    graph_delta_change_kind kind =
        graph_delta_change_kind::patch;
    bool live = false;
};

struct graph_delta_initialization_change final {
    object_initialization_record value{};
    graph_delta_change_kind kind =
        graph_delta_change_kind::patch;
    bool live = false;
};

class graph_delta final {
public:
    constructor_default_table constructor_defaults;
    graph_delta() = default;

    graph_delta(const graph_delta&) = delete;
    graph_delta& operator=(const graph_delta&) = delete;

    graph_delta(graph_delta&&) noexcept = default;
    graph_delta& operator=(graph_delta&&) noexcept = default;

    [[nodiscard]] server_status bind_baseline(
        const compiled_project_view& baseline) noexcept;

    using object_change_sink =
        server_status (*)(
            void* context,
            const graph_delta& graph,
            const graph_delta_object_change& change) noexcept;

    void set_object_change_sink(
        void* context,
        object_change_sink sink) noexcept {
        object_sink_context = context;
        object_sink = sink;
    }

    [[nodiscard]] bool object_change(
        object_handle object,
        graph_delta_object_change& output) const noexcept;

    [[nodiscard]] bool baseline_bound() const noexcept {
        return baseline != nullptr;
    }

    [[nodiscard]] server_status define_intrinsic_alias(identity_ref identity, intrinsic_type intrinsic, type_handle& output) noexcept;

    [[nodiscard]] server_status declare_record(
        identity_ref identity,
        graph_record_kind kind,
        type_handle& output) noexcept;

    [[nodiscard]] server_status define_record(
        type_handle type,
        graph_record_kind kind,
        std::span<const member_record> definition,
        std::span<const construction_value> construction = {},
        std::span<const base_record> bases = {},
        bool declares_virtual = false) noexcept;

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

    // Parser-compatible canonical object/subobject initialization. BUILD keeps
    // unchanged targets in compiled.bin and records only sparse target changes.
    [[nodiscard]] server_status add_initialization(
        object_endpoint target,
        construction_value value,
        bool& replaced) noexcept;

    [[nodiscard]] bool initialization(
        object_endpoint target,
        object_initialization_record& output) const noexcept;

    // BUILD invalidation is intentionally idempotent because more than one
    // semantic root may have produced the same canonical target.
    [[nodiscard]] server_status invalidate_initialization(
        object_endpoint target) noexcept;

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

    [[nodiscard]] server_status intern_endpoint_path(
        type_ref root_type,
        std::span<const endpoint_path_step> steps,
        endpoint_path_handle& output,
        type_ref* value_type = nullptr) noexcept;

    [[nodiscard]] bool endpoint_path(
        endpoint_path_handle path,
        endpoint_path_record& output) const noexcept;

    [[nodiscard]] bool endpoint_path_step_at(
        std::size_t index,
        endpoint_path_step& output) const noexcept;

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

    [[nodiscard]] bool base(
        type_handle type,
        std::uint32_t local_base,
        base_record& output) const noexcept;

    [[nodiscard]] bool polymorphic(
        type_handle type) const noexcept;

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

    [[nodiscard]] bool member(
        type_handle type,
        std::uint32_t local_member,
        member_record& output) const noexcept;

    [[nodiscard]] bool construction(
        type_handle type,
        std::uint32_t local_member,
        construction_value& output) const noexcept;

    [[nodiscard]] type_handle type_at(
        std::size_t index) const noexcept {

        if (index >= type_count()) {
            return {};
        }

        const type_handle output{
            static_cast<std::uint32_t>(
                index + 1)};

        return contains(output)
            ? output
            : type_handle{};
    }

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

    [[nodiscard]] link_handle link_at(
        std::size_t index) const noexcept {

        if (index >= link_count()) {
            return {};
        }

        const link_handle output{
            static_cast<std::uint32_t>(
                index + 1)};

        return contains(output)
            ? output
            : link_handle{};
    }

    [[nodiscard]] type_ref derived_at(
        std::size_t index) const noexcept {

        return index < derived_type_count()
            ? type_ref::make(
                type_ref_kind::derived,
                static_cast<std::uint32_t>(
                    index + 1))
            : type_ref{};
    }

    [[nodiscard]] endpoint_path_handle endpoint_path_at(
        std::size_t index) const noexcept {

        return index < endpoint_path_count()
            ? endpoint_path_handle{
                static_cast<std::uint32_t>(
                    index + 1)}
            : endpoint_path_handle{};
    }

    using initialization_visitor =
        server_status (*)(
            void* context,
            const object_initialization_record& value) noexcept;

    [[nodiscard]] server_status visit_initializations(
        void* context,
        initialization_visitor visitor) const noexcept;

    // O(changed) persistence boundary. These visitors enumerate only BUILD
    // patches and appends; unchanged mmap baseline payload is never visited.
    using type_change_visitor =
        server_status (*)(
            void* context,
            const graph_delta_type_change& change) noexcept;

    using object_change_visitor =
        server_status (*)(
            void* context,
            const graph_delta_object_change& change) noexcept;

    using link_change_visitor =
        server_status (*)(
            void* context,
            const graph_delta_link_change& change) noexcept;

    using initialization_change_visitor =
        server_status (*)(
            void* context,
            const graph_delta_initialization_change& change) noexcept;

    [[nodiscard]] server_status visit_type_changes(
        void* context,
        type_change_visitor visitor) const noexcept;

    [[nodiscard]] server_status visit_object_changes(
        void* context,
        object_change_visitor visitor) const noexcept;

    [[nodiscard]] server_status visit_link_changes(
        void* context,
        link_change_visitor visitor) const noexcept;

    [[nodiscard]] server_status visit_initialization_changes(
        void* context,
        initialization_change_visitor visitor) const noexcept;

    [[nodiscard]] bool intrinsic(
        type_ref type,
        intrinsic_type& output) const noexcept;

    [[nodiscard]] bool named(
        type_ref type,
        type_handle& output) const noexcept;

    [[nodiscard]] bool derived(
        type_ref type,
        derived_type_record& output) const noexcept;

    // Count is physical top-level storage. UPDATE preserves an existing
    // WHERE; DELETE may leave an inactive slot; REBUILD is the compaction
    // boundary.
    [[nodiscard]] std::size_t type_count() const noexcept {
        return baseline_type_count +
            types.size();
    }

    [[nodiscard]] std::size_t live_type_count() const noexcept {
        return live_type_count_value;
    }

    [[nodiscard]] std::size_t stale_type_count() const noexcept {
        return type_count() -
            live_type_count_value;
    }

    [[nodiscard]] std::size_t member_count() const noexcept {
        return baseline_member_count +
            member_records.size();
    }

    [[nodiscard]] std::size_t base_count() const noexcept {
        return baseline_base_count +
            base_records.size();
    }

    [[nodiscard]] std::size_t object_count() const noexcept {
        return baseline_object_count +
            objects.size();
    }

    [[nodiscard]] std::size_t object_construction_count() const noexcept {
        return baseline_object_construction_count +
            object_construction.size();
    }

    [[nodiscard]] std::size_t live_object_count() const noexcept {
        return live_object_count_value;
    }

    [[nodiscard]] std::size_t stale_object_count() const noexcept {
        return object_count() -
            live_object_count_value;
    }

    [[nodiscard]] std::size_t link_count() const noexcept {
        return baseline_link_count +
            links.size();
    }

    [[nodiscard]] std::size_t live_link_count() const noexcept {
        return live_link_count_value;
    }

    [[nodiscard]] std::size_t stale_link_count() const noexcept {
        return link_count() -
            live_link_count_value;
    }

    [[nodiscard]] std::size_t initialization_count() const noexcept {
        return live_initialization_count_value;
    }

    [[nodiscard]] std::size_t derived_type_count() const noexcept {
        return baseline_derived_count +
            derived_types.size();
    }

    [[nodiscard]] std::size_t endpoint_path_count() const noexcept {
        return baseline_endpoint_path_count +
            endpoint_paths.size();
    }

    [[nodiscard]] std::size_t endpoint_path_step_count() const noexcept {
        return baseline_endpoint_path_step_count +
            endpoint_path_steps.size();
    }

    [[nodiscard]] std::size_t type_patch_count() const noexcept {
        return type_patches.size();
    }

    [[nodiscard]] std::size_t appended_type_count() const noexcept {
        return types.size();
    }

    [[nodiscard]] std::size_t object_patch_count() const noexcept {
        return object_patches.size();
    }

    [[nodiscard]] std::size_t appended_object_count() const noexcept {
        return objects.size();
    }

    [[nodiscard]] std::size_t link_patch_count() const noexcept {
        return link_patches.size();
    }

    [[nodiscard]] std::size_t appended_link_count() const noexcept {
        return links.size();
    }

    [[nodiscard]] std::size_t initialization_change_count() const noexcept {
        return initialization_patches.size();
    }

    // BUILD-local replacement/new-storage buffers. Persistence decides whether
    // each semantic result patches assigned storage or needs new owner storage.
    [[nodiscard]] std::span<const type_entry>
    type_entries() const noexcept {
        return types;
    }

    [[nodiscard]] std::span<const identity_ref>
    type_identity_entries() const noexcept {
        return type_identities;
    }

    [[nodiscard]] std::span<const base_record>
    base_entries() const noexcept {
        return base_records;
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

    [[nodiscard]] std::span<const endpoint_path_record>
    endpoint_path_entries() const noexcept {
        return endpoint_paths;
    }

    [[nodiscard]] std::span<const endpoint_path_step>
    endpoint_path_step_entries() const noexcept {
        return endpoint_path_steps;
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

    struct endpoint_path_index_slot final {
        std::uint32_t fingerprint = 0;
        endpoint_path_handle path{};
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

    struct initialization_patch final {
        object_initialization_record value;
        bool live = true;
    };

    struct initialization_target_index_slot final {
        std::uint64_t key = 0;
        std::uint32_t patch = 0;
        std::uint32_t reserved = 0;
    };

    static_assert(sizeof(derived_index_slot) == 8);
    static_assert(sizeof(endpoint_path_index_slot) == 8);
    static_assert(sizeof(initialization_target_index_slot) == 16);

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

    [[nodiscard]] static std::uint64_t hash_endpoint_path(
        type_ref root_type,
        std::span<const endpoint_path_step> steps) noexcept;

    [[nodiscard]] bool resolve_endpoint_path(
        type_ref root_type,
        std::span<const endpoint_path_step> steps,
        type_ref& output) const noexcept;

    [[nodiscard]] endpoint_path_handle find_local_endpoint_path(
        type_ref root_type,
        std::span<const endpoint_path_step> steps,
        std::uint64_t hash,
        std::uint32_t fingerprint) const noexcept;

    [[nodiscard]] server_status ensure_endpoint_path_index_capacity(
        std::size_t additional) noexcept;

    void insert_endpoint_path_index(
        std::vector<endpoint_path_index_slot>& target,
        endpoint_path_handle path,
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

    [[nodiscard]] const initialization_patch*
    find_initialization_patch(
        object_endpoint target) const noexcept;

    [[nodiscard]] initialization_patch*
    find_initialization_patch(
        object_endpoint target) noexcept;

    [[nodiscard]] server_status
    ensure_initialization_target_index_capacity(
        std::size_t additional) noexcept;

    void insert_initialization_target_index(
        std::vector<initialization_target_index_slot>& target,
        std::uint64_t key,
        std::uint32_t patch) const noexcept;

    [[nodiscard]] bool scalar_initialization_target(
        type_ref type) const noexcept;

    [[nodiscard]] bool reference_binding_compatible(
        type_ref target,
        type_ref source) const noexcept;

    [[nodiscard]] bool endpoint_type(
        object_endpoint endpoint,
        type_ref& output) const noexcept;

    [[nodiscard]] server_status notify_object_change(
        object_handle object) noexcept;

    const compiled_project_view* baseline = nullptr;

    void* object_sink_context = nullptr;
    object_change_sink object_sink = nullptr;

    std::size_t baseline_type_count = 0;
    std::size_t baseline_base_count = 0;
    std::size_t baseline_member_count = 0;
    std::size_t baseline_object_count = 0;
    std::size_t baseline_object_construction_count = 0;
    std::size_t baseline_link_count = 0;
    std::size_t baseline_derived_count = 0;
    std::size_t baseline_endpoint_path_count = 0;
    std::size_t baseline_endpoint_path_step_count = 0;

    std::size_t live_type_count_value = 0;
    std::size_t live_object_count_value = 0;
    std::size_t live_link_count_value = 0;
    std::size_t live_initialization_count_value = 0;

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

    std::vector<base_record> base_records;
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

    // One entry per touched canonical target. Baseline records remain mmap-only.
    std::vector<initialization_patch> initialization_patches;
    std::vector<initialization_target_index_slot>
        initialization_target_index;

    std::vector<derived_type_record> derived_types;
    std::vector<derived_index_slot> derived_index;

    std::vector<endpoint_path_record> endpoint_paths;
    std::vector<endpoint_path_step> endpoint_path_steps;
    std::vector<endpoint_path_index_slot> endpoint_path_index;

};

// BUILD-only projection from graph_delta lineage slots to one dense final G.
// It owns only remap tables; semantic payload stays in graph_delta/baseline.
// Runtime never sees these maps.
class graph_dense_projection final {
public:
    graph_dense_projection() = default;

    graph_dense_projection(
        const graph_dense_projection&) = delete;

    graph_dense_projection& operator=(
        const graph_dense_projection&) = delete;

    [[nodiscard]] server_status prepare(
        const graph_delta& graph) noexcept;

    void reset() noexcept;

    [[nodiscard]] bool valid() const noexcept {
        return source != nullptr;
    }

    [[nodiscard]] std::size_t type_count() const noexcept {
        return final_type_count;
    }

    [[nodiscard]] std::size_t object_count() const noexcept {
        return final_object_count;
    }

    [[nodiscard]] std::size_t link_count() const noexcept {
        return final_link_count;
    }

    [[nodiscard]] std::size_t derived_type_count() const noexcept {
        return final_derived_count;
    }

    [[nodiscard]] std::size_t endpoint_path_count() const noexcept {
        return final_endpoint_path_count;
    }

    [[nodiscard]] std::size_t member_count() const noexcept {
        return final_member_count;
    }

    [[nodiscard]] std::size_t base_count() const noexcept {
        return final_base_count;
    }

    [[nodiscard]] std::size_t object_construction_count() const noexcept {
        return final_object_construction_count;
    }

    [[nodiscard]] std::size_t endpoint_path_step_count() const noexcept {
        return final_endpoint_path_step_count;
    }

    [[nodiscard]] bool prepared_for(
        const graph_delta& graph) const noexcept {
        return source == &graph;
    }

    [[nodiscard]] type_handle remap(
        type_handle value) const noexcept;

    [[nodiscard]] object_handle remap(
        object_handle value) const noexcept;

    [[nodiscard]] link_handle remap(
        link_handle value) const noexcept;

    [[nodiscard]] type_ref remap(
        type_ref value) const noexcept;

    [[nodiscard]] endpoint_path_handle remap(
        endpoint_path_handle value) const noexcept;

    [[nodiscard]] bool remap(
        object_endpoint value,
        object_endpoint& output) const noexcept;

    [[nodiscard]] bool remap(
        construction_value value,
        construction_value& output) const noexcept;

private:
    const graph_delta* source = nullptr;

    std::vector<std::uint32_t> type_slots;
    std::vector<std::uint32_t> object_slots;
    std::vector<std::uint32_t> link_slots;
    std::vector<std::uint32_t> derived_slots;
    std::vector<std::uint32_t> endpoint_path_slots;

    std::size_t final_type_count = 0;
    std::size_t final_object_count = 0;
    std::size_t final_link_count = 0;
    std::size_t final_derived_count = 0;
    std::size_t final_endpoint_path_count = 0;
    std::size_t final_member_count = 0;
    std::size_t final_base_count = 0;
    std::size_t final_object_construction_count = 0;
    std::size_t final_endpoint_path_step_count = 0;
};

}
