/*
 * SHM Runtime V2 - selective 4 MiB hot flatten over the compact Type Batch.
 *
 * Type Batch remains the compact physical source. Hybrid owns only a hot
 * object-major flat cache selected from physical usage/cost data.
 * No G/project/identity_ref dependency exists in prepare or execution here.
 */
#pragma once

#include "shm_type_batch.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cw::server {

class shm_hybrid_04m_builder;
class shm_hybrid_04m_executor;

enum class shm_hybrid_04m_result : std::uint8_t {
    success = 0,
    invalid_input,
    incompatible_abi,
    overflow,
    failed,
};

struct shm_hybrid_04m_prepare_telemetry final {
    std::uint64_t budget_bytes = 0;
    std::uint64_t candidate_type_apis = 0;
    std::uint64_t selected_type_apis = 0;
    std::uint64_t hot_object_roots = 0;
    std::uint64_t cold_object_roots = 0;

    std::uint64_t selected_weighted_child_visits = 0;
    std::uint64_t all_weighted_child_visits = 0;

    // SHM-HYBRID-04M-WORK-01:
    // Physical operations whose execution order changes from compact/local
    // traversal to object-major flat traversal.
    std::uint64_t selected_weighted_physical_writes = 0;
    std::uint64_t all_weighted_physical_writes = 0;

    // Selection work = flattenable structural visits + flat physical writes.
    std::uint64_t selected_weighted_work = 0;
    std::uint64_t all_weighted_work = 0;

    std::uint64_t flat_apis = 0;
    std::uint64_t flat_relative_references = 0;
    std::uint64_t flat_absolute_references = 0;
    std::uint64_t flat_object_references = 0;
    std::uint64_t flat_stores = 0;
    std::uint64_t flat_repeats = 0;

    std::uint64_t flat_api_bytes = 0;
    std::uint64_t flat_relative_reference_bytes = 0;
    std::uint64_t flat_absolute_reference_bytes = 0;
    std::uint64_t flat_object_reference_bytes = 0;
    std::uint64_t flat_store_bytes = 0;
    std::uint64_t flat_repeat_bytes = 0;
    std::uint64_t selection_map_bytes = 0;

    // flat_payload_bytes is budgeted. resident_bytes also includes the dense
    // local-api -> flat-api selection map. Type Batch bytes are not duplicated.
    std::uint64_t flat_payload_bytes = 0;
    std::uint64_t resident_bytes = 0;
    std::uint64_t base_type_batch_bytes = 0;
    std::uint64_t total_runtime_metadata_bytes = 0;
};

struct shm_hybrid_04m_execute_telemetry final {
    std::uint64_t objects = 0;
    std::uint64_t hot_object_roots = 0;
    std::uint64_t cold_object_roots = 0;

    std::uint64_t flat_api_applications = 0;
    std::uint64_t local_batch_api_applications = 0;
    std::uint64_t local_child_visits = 0;
    std::uint64_t cold_batches = 0;

    std::uint64_t reference_writes = 0;
    std::uint64_t relative_reference_writes = 0;
    std::uint64_t absolute_reference_writes = 0;
    std::uint64_t object_reference_writes = 0;
    std::uint64_t store_writes = 0;

    std::uint64_t repeat_visits = 0;
    std::uint64_t repeat_iterations = 0;
    std::uint64_t object_patch_writes = 0;
};

class shm_hybrid_04m final {
public:
    shm_hybrid_04m() = default;

    shm_hybrid_04m(const shm_hybrid_04m&) = delete;
    shm_hybrid_04m& operator=(const shm_hybrid_04m&) = delete;

    shm_hybrid_04m(shm_hybrid_04m&&) noexcept = default;
    shm_hybrid_04m& operator=(shm_hybrid_04m&&) noexcept = default;

    [[nodiscard]] bool prepared() const noexcept {
        return prepared_value;
    }

    [[nodiscard]] std::size_t resident_bytes() const noexcept;
    [[nodiscard]] std::size_t flat_payload_bytes() const noexcept;

    static constexpr std::uint64_t flat_budget_bytes =
        4ull * 1024ull * 1024ull;

private:
    struct range final {
        std::uint32_t begin = 0;
        std::uint32_t count = 0;
    };

    struct flat_api final {
        range relative_references{};
        range absolute_references{};
        range object_references{};
        range stores{};
        range repeats{};
    };

    static_assert(sizeof(flat_api) == 40);

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

    // Repeat deliberately points back to the compact LOCAL Type API. Arrays
    // were excluded from the flatten benefit model and remain structural.
    struct repeat_operation final {
        shm_record_offset target = 0;
        std::uint32_t local_type_api = 0;
        shm_offset stride = 0;
        std::uint64_t count = 0;
    };

    static_assert(sizeof(repeat_operation) == 24);

    void reset() noexcept;

    // Dense local Type API id (one based) -> hot flat API id (one based).
    std::vector<std::uint32_t> flat_api_by_local;

    std::vector<flat_api> flat_apis;
    std::vector<relative_reference> relative_references;
    std::vector<absolute_reference> absolute_references;
    std::vector<object_reference> object_references;
    std::vector<store_operation> stores;
    std::vector<repeat_operation> repeats;

    abi_target target_value = abi_target::windows_x64;
    shm_offset layout_size = 0;
    bool prepared_value = false;

    friend class shm_hybrid_04m_builder;
    friend class shm_hybrid_04m_executor;
};

// Parameterized prepare used by measurement/tuning. The Runtime
// representation and executor are identical for every budget.
[[nodiscard]] shm_hybrid_04m_result prepare_shm_hybrid_budget(
    const shm_type_batch& base,
    std::uint64_t flat_budget_bytes,
    shm_hybrid_04m& output,
    shm_hybrid_04m_prepare_telemetry* telemetry = nullptr) noexcept;

// Compatibility/production-candidate wrapper for the current 4 MiB policy.
[[nodiscard]] shm_hybrid_04m_result prepare_shm_hybrid_04m(
    const shm_type_batch& base,
    shm_hybrid_04m& output,
    shm_hybrid_04m_prepare_telemetry* telemetry = nullptr) noexcept;

// Hot path: no G/project access, no identity_ref, no hash/map/name lookup.
[[nodiscard]] shm_hybrid_04m_result materialize_shm_hybrid_04m_objects(
    const shm_type_batch& base,
    const shm_hybrid_04m& hybrid,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_hybrid_04m_execute_telemetry* telemetry = nullptr) noexcept;

}
