/*
 * SHM Runtime V2 - selective flattening profile.
 *
 * Measurement-only analysis over the prepared local Type Batch image.
 * No G/project access and no changes to materialization.
 */
#pragma once

#include "shm_type_batch.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace cw::server {

enum class shm_hybrid_profile_result : std::uint8_t {
    success = 0,
    invalid_input,
    overflow,
    failed,
};

struct shm_hybrid_budget_profile final {
    std::uint64_t budget_bytes = 0;
    std::uint64_t selected_type_apis = 0;
    std::uint64_t flat_bytes = 0;
    std::uint64_t object_roots = 0;

    // Benefit if a selected root uses one record/base-flattened API instead
    // of traversing Child edges for every root object.
    std::uint64_t weighted_child_visits = 0;

    // Same saved Child-edge work under the current Type Batch execution
    // policy (one structural traversal per root batch).
    std::uint64_t batch_child_visits = 0;
};

struct shm_hybrid_profile final {
    static constexpr std::size_t budget_count = 6;

    std::uint64_t object_groups = 0;
    std::uint64_t object_roots = 0;
    std::uint64_t candidate_type_apis = 0;

    std::uint64_t singleton_groups = 0;
    std::uint64_t groups_le_2 = 0;
    std::uint64_t groups_le_4 = 0;
    std::uint64_t groups_le_8 = 0;
    std::uint64_t groups_le_16 = 0;
    std::uint64_t groups_le_32 = 0;
    std::uint64_t groups_le_64 = 0;
    std::uint64_t groups_gt_64 = 0;
    std::uint64_t max_group_roots = 0;

    // Sum if every object-root TypeApi received an independent flat variant.
    // Constants/ObjectWhere remain shared and are not charged again.
    std::uint64_t all_candidate_flat_bytes = 0;

    // Total record/base Child work available to eliminate.
    std::uint64_t all_weighted_child_visits = 0;
    std::uint64_t all_batch_child_visits = 0;

    std::array<shm_hybrid_budget_profile, budget_count> budgets{};
};

[[nodiscard]] shm_hybrid_profile_result profile_shm_type_hybrid(
    const shm_type_batch& area,
    shm_hybrid_profile& output) noexcept;

}
