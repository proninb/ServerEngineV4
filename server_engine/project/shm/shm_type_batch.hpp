/*
 * SHM Runtime V2 - local Type API batched construction.
 *
 * identity_ref is consumed at semantic -> physical prepare boundaries.
 * The hot executor owns only dense type/object slots and numeric offsets.
 *
 * G is semantic truth. This area is disposable physical Runtime state.
 */
#pragma once

#include "shm_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cw::server {

class compiled_project_view;
class shm_type_batch_builder;
class shm_type_batch_executor;
class shm_hybrid_profiler;
class shm_hybrid_04m_builder;
class shm_hybrid_04m_executor;

enum class shm_type_batch_result : std::uint8_t {
    success = 0,
    invalid_input,
    unsupported_type,
    incompatible_abi,
    overflow,
    failed,
};

struct shm_type_batch_prepare_telemetry final {
    std::uint64_t type_apis = 0;
    std::uint64_t named_types_prepared = 0;
    std::uint64_t derived_types_prepared = 0;

    std::uint64_t canonical_roots = 0;
    std::uint64_t objects = 0;
    std::uint64_t object_patches = 0;

    std::uint64_t relative_references = 0;
    std::uint64_t absolute_references = 0;
    std::uint64_t object_references = 0;
    std::uint64_t stores = 0;
    std::uint64_t constructor_defaults = 0;
    std::uint64_t children = 0;
    std::uint64_t repeats = 0;
    std::uint64_t object_groups = 0;
    std::uint64_t canonical_groups = 0;
    std::uint64_t grouped_object_roots = 0;
    std::uint64_t grouped_canonical_roots = 0;
    std::uint64_t batch_size = 0;

    std::uint64_t semantic_identity_resolutions = 0;
    std::uint64_t object_binding_resolutions = 0;
    std::uint64_t reference_chain_steps_resolved = 0;
    std::uint64_t child_edges = 0;
    std::uint64_t zero_operations_elided = 0;

    // SHM-TYPE-INLINE-08-01.
    std::uint64_t inline_leaf_limit = 0;
    std::uint64_t inline_leaf_children = 0;
    std::uint64_t inline_leaf_operations = 0;
    std::uint64_t inline_leaf_relative_references = 0;
    std::uint64_t inline_leaf_absolute_references = 0;
    std::uint64_t inline_leaf_object_references = 0;
    std::uint64_t inline_leaf_stores = 0;

    // SHM-TYPE-SUBTREE-INLINE-08-01.
    std::uint64_t inline_subtree_limit = 0;
    std::uint64_t inline_subtree_children = 0;
    std::uint64_t inline_subtree_operations = 0;

    std::uint64_t type_api_bytes = 0;
    std::uint64_t relative_reference_bytes = 0;
    std::uint64_t absolute_reference_bytes = 0;
    std::uint64_t object_reference_bytes = 0;
    std::uint64_t store_bytes = 0;
    std::uint64_t post_store_bytes = 0;
    std::uint64_t child_bytes = 0;
    std::uint64_t repeat_bytes = 0;
    std::uint64_t constant_bytes = 0;
    std::uint64_t object_where_bytes = 0;
    std::uint64_t object_runtime_bytes = 0;
    std::uint64_t canonical_root_bytes = 0;
    std::uint64_t object_group_bytes = 0;
    std::uint64_t object_group_offset_bytes = 0;
    std::uint64_t canonical_group_bytes = 0;
    std::uint64_t canonical_group_offset_bytes = 0;
    std::uint64_t object_patch_bytes = 0;
    std::uint64_t resident_bytes = 0;
};

struct shm_type_batch_execute_telemetry final {
    std::uint64_t api_applications = 0;
    std::uint64_t canonical_roots = 0;
    std::uint64_t objects = 0;

    std::uint64_t reference_writes = 0;
    std::uint64_t relative_reference_writes = 0;
    std::uint64_t absolute_reference_writes = 0;
    std::uint64_t object_reference_writes = 0;
    std::uint64_t store_writes = 0;
    std::uint64_t constructor_default_writes = 0;

    // A static-link target is marked before object construction.
    std::uint64_t pending_link_preserves = 0;

    std::uint64_t batch_api_applications = 0;
    std::uint64_t child_visits = 0;
    std::uint64_t repeat_visits = 0;
    std::uint64_t repeat_iterations = 0;
    std::uint64_t object_batches = 0;
    std::uint64_t canonical_batches = 0;
    std::uint64_t object_patch_writes = 0;
};

class shm_type_batch final {
public:
    shm_type_batch() = default;

    shm_type_batch(const shm_type_batch&) = delete;
    shm_type_batch& operator=(const shm_type_batch&) = delete;

    shm_type_batch(shm_type_batch&&) noexcept = default;
    shm_type_batch& operator=(shm_type_batch&&) noexcept = default;

    [[nodiscard]] bool prepared() const noexcept {
        return prepared_value;
    }

    [[nodiscard]] std::size_t type_api_count() const noexcept {
        return type_apis.size();
    }

    [[nodiscard]] std::size_t object_count() const noexcept {
        return objects.size();
    }

    [[nodiscard]] std::size_t resident_bytes() const noexcept;

private:
    struct range final {
        std::uint32_t begin = 0;
        std::uint32_t count = 0;
    };

    struct type_api final {
        range relative_references{};
        range absolute_references{};
        range object_references{};
        range stores{};
        range children{};
        range repeats{};

        // Constructor-default scalar writes execute only after all child/base
        // APIs and repeats for this record have completed.
        range post_stores{};
    };

    static_assert(sizeof(type_api) == 56);

    struct relative_reference final {
        shm_record_offset target = 0;
        shm_record_offset source = 0;
    };

    static_assert(sizeof(relative_reference) == 8);

    struct absolute_reference final {
        shm_record_offset target = 0;
        std::uint32_t reserved = 0;
        shm_offset source = 0;
    };

    static_assert(sizeof(absolute_reference) == 16);

    struct object_reference final {
        shm_record_offset target = 0;
        std::uint32_t object_slot = 0;
    };

    static_assert(sizeof(object_reference) == 8);

    struct store_operation final {
        shm_record_offset target = 0;
        std::uint32_t constant = 0;
        std::uint8_t size = 0;
        std::uint8_t reserved[3]{};
    };

    static_assert(sizeof(store_operation) == 12);

    struct child_operation final {
        shm_record_offset target = 0;
        std::uint32_t type_api = 0;
    };

    static_assert(sizeof(child_operation) == 8);

    struct repeat_operation final {
        shm_record_offset target = 0;
        std::uint32_t type_api = 0;
        shm_offset stride = 0;
        std::uint64_t count = 0;
    };

    static_assert(sizeof(repeat_operation) == 24);

    struct canonical_root final {
        shm_offset offset = 0;
        std::uint32_t type_api = 0;
        std::uint32_t reserved = 0;
    };

    static_assert(sizeof(canonical_root) == 16);

    struct object_runtime final {
        shm_offset offset = 0;
        std::uint32_t type_api = 0;
        std::uint32_t patch = 0;
    };

    static_assert(sizeof(object_runtime) == 16);

    struct root_group final {
        std::uint32_t type_api = 0;
        range roots{};
    };

    static_assert(sizeof(root_group) == 12);

    static constexpr std::uint32_t execution_batch_size = 64;

    void reset() noexcept;

    std::vector<type_api> type_apis;
    std::vector<relative_reference> relative_references;
    std::vector<absolute_reference> absolute_references;
    std::vector<object_reference> object_references;
    std::vector<store_operation> stores;
    std::vector<store_operation> post_stores;
    std::vector<child_operation> children;
    std::vector<repeat_operation> repeats;
    std::vector<std::array<std::byte, 16>> constants;

    // Dense object slot -> SHM WHERE. No semantic identity survives here.
    std::vector<shm_offset> object_where;

    std::vector<object_runtime> objects;
    std::vector<canonical_root> canonical_roots;
    std::vector<root_group> object_groups;
    std::vector<shm_offset> object_group_offsets;
    std::vector<root_group> canonical_groups;
    std::vector<shm_offset> canonical_group_offsets;
    std::vector<store_operation> object_patches;

    abi_target target_value = abi_target::windows_x64;
    shm_offset layout_size = 0;
    bool prepared_value = false;

    friend class shm_type_batch_builder;
    friend class shm_type_batch_executor;
    friend class shm_hybrid_profiler;
    friend class shm_hybrid_04m_builder;
    friend class shm_hybrid_04m_executor;

    friend shm_type_batch_result prepare_shm_type_batch(
        const compiled_project_view&,
        const server_abi_configuration&,
        const shm_layout&,
        shm_type_batch&,
        shm_type_batch_prepare_telemetry*) noexcept;

    friend shm_type_batch_result prepare_shm_type_batch_inline08(
        const compiled_project_view&,
        const server_abi_configuration&,
        const shm_layout&,
        shm_type_batch&,
        shm_type_batch_prepare_telemetry*) noexcept;

    friend shm_type_batch_result prepare_shm_type_batch_subtree08(
        const compiled_project_view&,
        const server_abi_configuration&,
        const shm_layout&,
        shm_type_batch&,
        shm_type_batch_prepare_telemetry*) noexcept;

    friend shm_type_batch_result prepare_shm_type_batch_inline16(
        const compiled_project_view&,
        const server_abi_configuration&,
        const shm_layout&,
        shm_type_batch&,
        shm_type_batch_prepare_telemetry*) noexcept;

    friend shm_type_batch_result prepare_shm_type_batch_inline32(
        const compiled_project_view&,
        const server_abi_configuration&,
        const shm_layout&,
        shm_type_batch&,
        shm_type_batch_prepare_telemetry*) noexcept;

    friend shm_type_batch_result prepare_shm_type_batch_inline64(
        const compiled_project_view&,
        const server_abi_configuration&,
        const shm_layout&,
        shm_type_batch&,
        shm_type_batch_prepare_telemetry*) noexcept;

    friend shm_type_batch_result materialize_shm_type_batch_canonical(
        const shm_type_batch&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_type_batch_execute_telemetry*) noexcept;

    friend shm_type_batch_result materialize_shm_type_batch_objects(
        const shm_type_batch&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_type_batch_execute_telemetry*) noexcept;
};

[[nodiscard]] shm_type_batch_result prepare_shm_type_batch(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_type_batch& output,
    shm_type_batch_prepare_telemetry* telemetry = nullptr) noexcept;

// Same physical representation and executor as Type Batch, but small leaf
// child APIs (<= 8 physical operations, no Child/Repeat) are fused into their
// parent during prepare.
[[nodiscard]] shm_type_batch_result prepare_shm_type_batch_inline08(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_type_batch& output,
    shm_type_batch_prepare_telemetry* telemetry = nullptr) noexcept;

[[nodiscard]] shm_type_batch_result prepare_shm_type_batch_subtree08(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_type_batch& output,
    shm_type_batch_prepare_telemetry* telemetry = nullptr) noexcept;

// SHM-TYPE-INLINE-SWEEP-01:
// Same representation and object-major executor as INLINE-08. Only the
// bottom-up leaf-inline physical-operation threshold changes.
[[nodiscard]] shm_type_batch_result prepare_shm_type_batch_inline16(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_type_batch& output,
    shm_type_batch_prepare_telemetry* telemetry = nullptr) noexcept;

[[nodiscard]] shm_type_batch_result prepare_shm_type_batch_inline32(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_type_batch& output,
    shm_type_batch_prepare_telemetry* telemetry = nullptr) noexcept;

[[nodiscard]] shm_type_batch_result prepare_shm_type_batch_inline64(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_type_batch& output,
    shm_type_batch_prepare_telemetry* telemetry = nullptr) noexcept;

// Hot path: no G/project access, no identity_ref, no map/hash/name lookup.
[[nodiscard]] shm_type_batch_result materialize_shm_type_batch_canonical(
    const shm_type_batch& area,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_type_batch_execute_telemetry* telemetry = nullptr) noexcept;

[[nodiscard]] shm_type_batch_result materialize_shm_type_batch_objects(
    const shm_type_batch& area,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_type_batch_execute_telemetry* telemetry = nullptr) noexcept;

// SHM-TYPE-INLINE-08-OBJECT-MAJOR-01:
// Measurement path over the same shm_type_batch representation. This changes
// only execution order: one Object -> complete Type API tree -> next Object.
[[nodiscard]] shm_type_batch_result
materialize_shm_type_batch_objects_object_major(
    const shm_type_batch& area,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_type_batch_execute_telemetry* telemetry = nullptr) noexcept;

}
