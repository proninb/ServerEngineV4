/*
 * SHM Runtime V2 - dense Type Area.
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
class shm_type_area_builder;
class shm_type_area_executor;

enum class shm_type_area_result : std::uint8_t {
    success = 0,
    invalid_input,
    unsupported_type,
    incompatible_abi,
    overflow,
    failed,
};

struct shm_type_area_prepare_telemetry final {
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
    std::uint64_t repeats = 0;

    std::uint64_t semantic_identity_resolutions = 0;
    std::uint64_t object_binding_resolutions = 0;
    std::uint64_t reference_chain_steps_resolved = 0;
    std::uint64_t flattened_api_copies = 0;
    std::uint64_t zero_operations_elided = 0;

    // SHM-TYPE-AREA-FINALIZE-01: builder image before root compaction.
    std::uint64_t prefinal_type_apis = 0;
    std::uint64_t prefinal_relative_references = 0;
    std::uint64_t prefinal_absolute_references = 0;
    std::uint64_t prefinal_object_references = 0;
    std::uint64_t prefinal_stores = 0;
    std::uint64_t prefinal_repeats = 0;
    std::uint64_t prefinal_constants = 0;
    std::uint64_t prefinal_resident_bytes = 0;

    std::uint64_t reachable_type_apis = 0;
    std::uint64_t discarded_type_apis = 0;
    std::uint64_t discarded_relative_references = 0;
    std::uint64_t discarded_absolute_references = 0;
    std::uint64_t discarded_object_references = 0;
    std::uint64_t discarded_stores = 0;
    std::uint64_t discarded_repeats = 0;
    std::uint64_t discarded_constants = 0;
    std::uint64_t reclaimed_bytes = 0;

    std::uint64_t type_api_bytes = 0;
    std::uint64_t relative_reference_bytes = 0;
    std::uint64_t absolute_reference_bytes = 0;
    std::uint64_t object_reference_bytes = 0;
    std::uint64_t store_bytes = 0;
    std::uint64_t repeat_bytes = 0;
    std::uint64_t constant_bytes = 0;
    std::uint64_t object_where_bytes = 0;
    std::uint64_t object_runtime_bytes = 0;
    std::uint64_t canonical_root_bytes = 0;
    std::uint64_t object_patch_bytes = 0;
    std::uint64_t resident_bytes = 0;
};

struct shm_type_area_execute_telemetry final {
    std::uint64_t api_applications = 0;
    std::uint64_t canonical_roots = 0;
    std::uint64_t objects = 0;

    std::uint64_t reference_writes = 0;
    std::uint64_t relative_reference_writes = 0;
    std::uint64_t absolute_reference_writes = 0;
    std::uint64_t object_reference_writes = 0;
    std::uint64_t store_writes = 0;

    std::uint64_t repeat_visits = 0;
    std::uint64_t repeat_iterations = 0;
    std::uint64_t object_patch_writes = 0;
};

class shm_type_area final {
public:
    shm_type_area() = default;

    shm_type_area(const shm_type_area&) = delete;
    shm_type_area& operator=(const shm_type_area&) = delete;

    shm_type_area(shm_type_area&&) noexcept = default;
    shm_type_area& operator=(shm_type_area&&) noexcept = default;

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
        range repeats{};
    };

    static_assert(sizeof(type_api) == 40);

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

    void reset() noexcept;

    std::vector<type_api> type_apis;
    std::vector<relative_reference> relative_references;
    std::vector<absolute_reference> absolute_references;
    std::vector<object_reference> object_references;
    std::vector<store_operation> stores;
    std::vector<repeat_operation> repeats;
    std::vector<std::array<std::byte, 16>> constants;

    // Dense object slot -> SHM WHERE. No semantic identity survives here.
    std::vector<shm_offset> object_where;

    std::vector<object_runtime> objects;
    std::vector<canonical_root> canonical_roots;
    std::vector<store_operation> object_patches;

    abi_target target_value = abi_target::windows_x64;
    shm_offset layout_size = 0;
    bool prepared_value = false;

    friend class shm_type_area_builder;
    friend class shm_type_area_executor;

    friend shm_type_area_result prepare_shm_type_area(
        const compiled_project_view&,
        const server_abi_configuration&,
        const shm_layout&,
        shm_type_area&,
        shm_type_area_prepare_telemetry*) noexcept;

    friend shm_type_area_result materialize_shm_type_area_canonical(
        const shm_type_area&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_type_area_execute_telemetry*) noexcept;

    friend shm_type_area_result materialize_shm_type_area_objects(
        const shm_type_area&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_type_area_execute_telemetry*) noexcept;
};

[[nodiscard]] shm_type_area_result prepare_shm_type_area(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_type_area& output,
    shm_type_area_prepare_telemetry* telemetry = nullptr) noexcept;

// Hot path: no G/project access, no identity_ref, no map/hash/name lookup.
[[nodiscard]] shm_type_area_result materialize_shm_type_area_canonical(
    const shm_type_area& area,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_type_area_execute_telemetry* telemetry = nullptr) noexcept;

[[nodiscard]] shm_type_area_result materialize_shm_type_area_objects(
    const shm_type_area& area,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_type_area_execute_telemetry* telemetry = nullptr) noexcept;

}
