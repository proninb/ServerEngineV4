#include "shm_hybrid_profile.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace cw::server {
namespace {

enum class profile_state : std::uint8_t {
    empty = 0,
    visiting,
    ready,
};

struct api_profile final {
    std::uint64_t flat_relative_references = 0;
    std::uint64_t flat_absolute_references = 0;
    std::uint64_t flat_object_references = 0;
    std::uint64_t flat_stores = 0;
    std::uint64_t flat_repeats = 0;

    // Child edges reachable only through record/base composition.
    // Repeat targets intentionally remain separate APIs.
    std::uint64_t flattenable_child_visits = 0;
};

struct candidate final {
    std::uint32_t type_api = 0;
    std::uint32_t roots = 0;
    std::uint64_t flat_bytes = 0;
    std::uint64_t weighted_child_visits = 0;
    std::uint64_t batch_child_visits = 0;
};

[[nodiscard]] bool add_u64(
    std::uint64_t& target,
    std::uint64_t value) noexcept {

    if (value >
        (std::numeric_limits<
            std::uint64_t>::max)() -
            target) {

        return false;
    }

    target += value;
    return true;
}

[[nodiscard]] bool mul_u64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& output) noexcept {

    output = 0;

    if (left == 0 ||
        right == 0) {

        return true;
    }

    if (right >
        (std::numeric_limits<
            std::uint64_t>::max)() /
            left) {

        return false;
    }

    output = left * right;
    return true;
}

[[nodiscard]] bool add_mul_u64(
    std::uint64_t& target,
    std::uint64_t count,
    std::uint64_t size) noexcept {

    std::uint64_t bytes = 0;

    return mul_u64(
               count,
               size,
               bytes) &&
        add_u64(
            target,
            bytes);
}

}

class shm_hybrid_profiler final {
public:
    explicit shm_hybrid_profiler(
        const shm_type_batch& area) noexcept
        : area(area) {
    }

    [[nodiscard]] shm_hybrid_profile_result run(
        shm_hybrid_profile& output) {

        output = {};

        if (!area.prepared_value) {
            return shm_hybrid_profile_result::
                invalid_input;
        }

        states.resize(
            area.type_apis.size(),
            profile_state::empty);

        profiles.resize(
            area.type_apis.size());

        std::vector<candidate> candidates;
        candidates.reserve(
            area.object_groups.size());

        output.object_groups =
            area.object_groups.size();
        output.object_roots =
            area.object_group_offsets.size();

        for (const auto& group :
             area.object_groups) {

            if (group.type_api == 0 ||
                group.type_api >
                    area.type_apis.size() ||
                group.roots.begin >
                    area.object_group_offsets.size() ||
                group.roots.count >
                    area.object_group_offsets.size() -
                        group.roots.begin) {

                return shm_hybrid_profile_result::
                    invalid_input;
            }

            const auto roots =
                static_cast<std::uint64_t>(
                    group.roots.count);

            if (roots == 0) {
                return shm_hybrid_profile_result::
                    invalid_input;
            }

            output.max_group_roots =
                (std::max)(
                    output.max_group_roots,
                    roots);

            if (roots == 1) {
                ++output.singleton_groups;
            }
            if (roots <= 2) {
                ++output.groups_le_2;
            }
            if (roots <= 4) {
                ++output.groups_le_4;
            }
            if (roots <= 8) {
                ++output.groups_le_8;
            }
            if (roots <= 16) {
                ++output.groups_le_16;
            }
            if (roots <= 32) {
                ++output.groups_le_32;
            }
            if (roots <= 64) {
                ++output.groups_le_64;
            }
            else {
                ++output.groups_gt_64;
            }

            api_profile profile;

            const auto profiled =
                profile_api(
                    group.type_api,
                    profile);

            if (profiled !=
                shm_hybrid_profile_result::
                    success) {

                return profiled;
            }

            if (profile.flattenable_child_visits == 0) {
                continue;
            }

            std::uint64_t flat_bytes =
                sizeof(shm_type_batch::type_api);

            if (!add_mul_u64(
                    flat_bytes,
                    profile.flat_relative_references,
                    sizeof(
                        shm_type_batch::
                            relative_reference)) ||
                !add_mul_u64(
                    flat_bytes,
                    profile.flat_absolute_references,
                    sizeof(
                        shm_type_batch::
                            absolute_reference)) ||
                !add_mul_u64(
                    flat_bytes,
                    profile.flat_object_references,
                    sizeof(
                        shm_type_batch::
                            object_reference)) ||
                !add_mul_u64(
                    flat_bytes,
                    profile.flat_stores,
                    sizeof(
                        shm_type_batch::
                            store_operation)) ||
                !add_mul_u64(
                    flat_bytes,
                    profile.flat_repeats,
                    sizeof(
                        shm_type_batch::
                            repeat_operation))) {

                return shm_hybrid_profile_result::
                    overflow;
            }

            std::uint64_t weighted = 0;

            if (!mul_u64(
                    roots,
                    profile.flattenable_child_visits,
                    weighted)) {

                return shm_hybrid_profile_result::
                    overflow;
            }

            const auto batches =
                (roots +
                    shm_type_batch::
                        execution_batch_size -
                    1) /
                shm_type_batch::
                    execution_batch_size;

            std::uint64_t batch_visits = 0;

            if (!mul_u64(
                    batches,
                    profile.flattenable_child_visits,
                    batch_visits)) {

                return shm_hybrid_profile_result::
                    overflow;
            }

            candidate current{
                group.type_api,
                group.roots.count,
                flat_bytes,
                weighted,
                batch_visits,
            };

            candidates.push_back(
                current);

            if (!add_u64(
                    output.all_candidate_flat_bytes,
                    flat_bytes) ||
                !add_u64(
                    output.all_weighted_child_visits,
                    weighted) ||
                !add_u64(
                    output.all_batch_child_visits,
                    batch_visits)) {

                return shm_hybrid_profile_result::
                    overflow;
            }
        }

        output.candidate_type_apis =
            candidates.size();

        std::sort(
            candidates.begin(),
            candidates.end(),
            [](
                const candidate& left,
                const candidate& right) noexcept {

                // Descending weighted-benefit / byte ratio. long double is
                // profiling-only and avoids overflow in cross multiplication.
                const auto left_score =
                    static_cast<long double>(
                        left.weighted_child_visits) /
                    static_cast<long double>(
                        left.flat_bytes);

                const auto right_score =
                    static_cast<long double>(
                        right.weighted_child_visits) /
                    static_cast<long double>(
                        right.flat_bytes);

                if (left_score != right_score) {
                    return left_score >
                        right_score;
                }

                if (left.weighted_child_visits !=
                    right.weighted_child_visits) {

                    return
                        left.weighted_child_visits >
                        right.weighted_child_visits;
                }

                if (left.flat_bytes !=
                    right.flat_bytes) {

                    return left.flat_bytes <
                        right.flat_bytes;
                }

                return left.type_api <
                    right.type_api;
            });

        static constexpr std::uint64_t mib =
            1024ull * 1024ull;

        static constexpr std::uint64_t
            budget_values[
                shm_hybrid_profile::budget_count]{
                1ull * mib,
                4ull * mib,
                8ull * mib,
                16ull * mib,
                32ull * mib,
                64ull * mib,
            };

        for (std::size_t budget_index = 0;
             budget_index <
                 shm_hybrid_profile::budget_count;
             ++budget_index) {

            auto& budget =
                output.budgets[
                    budget_index];

            budget.budget_bytes =
                budget_values[
                    budget_index];

            for (const auto& current :
                 candidates) {

                if (current.flat_bytes >
                    budget.budget_bytes -
                        budget.flat_bytes) {

                    continue;
                }

                if (!add_u64(
                        budget.flat_bytes,
                        current.flat_bytes) ||
                    !add_u64(
                        budget.object_roots,
                        current.roots) ||
                    !add_u64(
                        budget.weighted_child_visits,
                        current.weighted_child_visits) ||
                    !add_u64(
                        budget.batch_child_visits,
                        current.batch_child_visits)) {

                    return shm_hybrid_profile_result::
                        overflow;
                }

                ++budget.selected_type_apis;
            }
        }

        return shm_hybrid_profile_result::
            success;
    }

private:
    [[nodiscard]] shm_hybrid_profile_result
    profile_api(
        std::uint32_t type_api,
        api_profile& output) {

        output = {};

        if (type_api == 0 ||
            type_api >
                area.type_apis.size()) {

            return shm_hybrid_profile_result::
                invalid_input;
        }

        const auto index =
            static_cast<std::size_t>(
                type_api - 1);

        if (states[index] ==
            profile_state::ready) {

            output = profiles[index];
            return shm_hybrid_profile_result::
                success;
        }

        if (states[index] ==
            profile_state::visiting) {

            return shm_hybrid_profile_result::
                invalid_input;
        }

        states[index] =
            profile_state::visiting;

        const auto& api =
            area.type_apis[index];

        if (!valid_range(
                api.relative_references,
                area.relative_references.size()) ||
            !valid_range(
                api.absolute_references,
                area.absolute_references.size()) ||
            !valid_range(
                api.object_references,
                area.object_references.size()) ||
            !valid_range(
                api.stores,
                area.stores.size()) ||
            !valid_range(
                api.children,
                area.children.size()) ||
            !valid_range(
                api.repeats,
                area.repeats.size())) {

            states[index] =
                profile_state::empty;

            return shm_hybrid_profile_result::
                invalid_input;
        }

        api_profile result;
        result.flat_relative_references =
            api.relative_references.count;
        result.flat_absolute_references =
            api.absolute_references.count;
        result.flat_object_references =
            api.object_references.count;
        result.flat_stores =
            api.stores.count;
        result.flat_repeats =
            api.repeats.count;

        for (std::uint32_t child_index = 0;
             child_index <
                 api.children.count;
             ++child_index) {

            const auto& child =
                area.children[
                    static_cast<std::size_t>(
                        api.children.begin) +
                    child_index];

            api_profile child_profile;

            const auto child_result =
                profile_api(
                    child.type_api,
                    child_profile);

            if (child_result !=
                shm_hybrid_profile_result::
                    success) {

                states[index] =
                    profile_state::empty;

                return child_result;
            }

            if (!add_u64(
                    result.flat_relative_references,
                    child_profile.
                        flat_relative_references) ||
                !add_u64(
                    result.flat_absolute_references,
                    child_profile.
                        flat_absolute_references) ||
                !add_u64(
                    result.flat_object_references,
                    child_profile.
                        flat_object_references) ||
                !add_u64(
                    result.flat_stores,
                    child_profile.flat_stores) ||
                !add_u64(
                    result.flat_repeats,
                    child_profile.flat_repeats) ||
                !add_u64(
                    result.flattenable_child_visits,
                    1) ||
                !add_u64(
                    result.flattenable_child_visits,
                    child_profile.
                        flattenable_child_visits)) {

                states[index] =
                    profile_state::empty;

                return shm_hybrid_profile_result::
                    overflow;
            }
        }

        // Repeat is intentionally not recursively flattened here. A selective
        // flat root retains Repeat as an explicit edge, exactly as the proven
        // flat Type Area did for arrays. This profile therefore measures only
        // record/base flattening and does not over-credit array structure.

        profiles[index] = result;
        states[index] = profile_state::ready;
        output = result;

        return shm_hybrid_profile_result::
            success;
    }

    [[nodiscard]] static bool valid_range(
        shm_type_batch::range range,
        std::size_t size) noexcept {

        return range.begin <= size &&
            range.count <=
                size - range.begin;
    }

    const shm_type_batch& area;
    std::vector<profile_state> states;
    std::vector<api_profile> profiles;
};

shm_hybrid_profile_result profile_shm_type_hybrid(
    const shm_type_batch& area,
    shm_hybrid_profile& output) noexcept {

    try {
        shm_hybrid_profiler profiler{
            area};

        return profiler.run(output);
    }
    catch (...) {
        output = {};
        return shm_hybrid_profile_result::
            failed;
    }
}

}
