#include "shm_hybrid_04m.hpp"

#include "../abi/abi_layout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
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
    std::uint64_t flattenable_child_visits = 0;
};

struct candidate final {
    std::uint32_t local_type_api = 0;
    std::uint32_t roots = 0;
    std::uint64_t flat_bytes = 0;
    std::uint64_t weighted_child_visits = 0;
    std::uint64_t weighted_physical_writes = 0;
    std::uint64_t weighted_work = 0;
};

[[nodiscard]] bool host_compatible(
    abi_target target) noexcept {

#if defined(_WIN32)
    if constexpr (sizeof(void*) == 8) {
        return target == abi_target::windows_x64;
    }
    else {
        return target == abi_target::windows_x86;
    }
#else
    return sizeof(void*) == 8 &&
        target == abi_target::posix_x64;
#endif
}

[[nodiscard]] constexpr shm_offset invalid_where() noexcept {
    return (std::numeric_limits<shm_offset>::max)();
}

[[nodiscard]] bool add_u64(
    std::uint64_t& target,
    std::uint64_t value) noexcept {

    if (value >
        (std::numeric_limits<std::uint64_t>::max)() -
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

    if (left == 0 || right == 0) {
        return true;
    }

    if (right >
        (std::numeric_limits<std::uint64_t>::max)() /
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

    return mul_u64(count, size, bytes) &&
        add_u64(target, bytes);
}

}

class shm_hybrid_04m_builder final {
public:
    shm_hybrid_04m_builder(
        const shm_type_batch& base,
        std::uint64_t flat_budget_bytes,
        shm_hybrid_04m& output,
        shm_hybrid_04m_prepare_telemetry* telemetry) noexcept
        : base(base),
          output(output),
          telemetry(telemetry),
          budget_bytes(flat_budget_bytes) {
    }

    [[nodiscard]] shm_hybrid_04m_result build() {

        output.reset();

        if (!base.prepared_value ||
            base.type_apis.empty()) {
            return shm_hybrid_04m_result::invalid_input;
        }

        states.resize(
            base.type_apis.size(),
            profile_state::empty);
        profiles.resize(base.type_apis.size());

        output.flat_api_by_local.assign(
            base.type_apis.size(),
            0);

        std::vector<candidate> candidates;
        candidates.reserve(base.object_groups.size());

        std::uint64_t all_weighted = 0;
        std::uint64_t all_weighted_physical_writes = 0;
        std::uint64_t all_weighted_work = 0;

        for (const auto& group : base.object_groups) {
            if (!valid_group(group)) {
                output.reset();
                return shm_hybrid_04m_result::invalid_input;
            }

            api_profile profile;
            const auto profiled =
                profile_api(group.type_api, profile);

            if (profiled != shm_hybrid_04m_result::success) {
                output.reset();
                return profiled;
            }

            if (profile.flattenable_child_visits == 0) {
                continue;
            }

            std::uint64_t flat_bytes =
                sizeof(shm_hybrid_04m::flat_api);

            if (!add_mul_u64(
                    flat_bytes,
                    profile.flat_relative_references,
                    sizeof(shm_hybrid_04m::relative_reference)) ||
                !add_mul_u64(
                    flat_bytes,
                    profile.flat_absolute_references,
                    sizeof(shm_hybrid_04m::absolute_reference)) ||
                !add_mul_u64(
                    flat_bytes,
                    profile.flat_object_references,
                    sizeof(shm_hybrid_04m::object_reference)) ||
                !add_mul_u64(
                    flat_bytes,
                    profile.flat_stores,
                    sizeof(shm_hybrid_04m::store_operation)) ||
                !add_mul_u64(
                    flat_bytes,
                    profile.flat_repeats,
                    sizeof(shm_hybrid_04m::repeat_operation))) {

                output.reset();
                return shm_hybrid_04m_result::overflow;
            }

            std::uint64_t weighted = 0;
            if (!mul_u64(
                    group.roots.count,
                    profile.flattenable_child_visits,
                    weighted) ||
                !add_u64(all_weighted, weighted)) {

                output.reset();
                return shm_hybrid_04m_result::overflow;
            }

            std::uint64_t physical_per_root = 0;

            if (!add_u64(
                    physical_per_root,
                    profile.flat_relative_references) ||
                !add_u64(
                    physical_per_root,
                    profile.flat_absolute_references) ||
                !add_u64(
                    physical_per_root,
                    profile.flat_object_references) ||
                !add_u64(
                    physical_per_root,
                    profile.flat_stores)) {

                output.reset();
                return shm_hybrid_04m_result::overflow;
            }

            std::uint64_t weighted_physical_writes = 0;

            if (!mul_u64(
                    group.roots.count,
                    physical_per_root,
                    weighted_physical_writes) ||
                !add_u64(
                    all_weighted_physical_writes,
                    weighted_physical_writes)) {

                output.reset();
                return shm_hybrid_04m_result::overflow;
            }

            std::uint64_t weighted_work =
                weighted;

            if (!add_u64(
                    weighted_work,
                    weighted_physical_writes) ||
                !add_u64(
                    all_weighted_work,
                    weighted_work)) {

                output.reset();
                return shm_hybrid_04m_result::overflow;
            }

            candidates.push_back({
                group.type_api,
                group.roots.count,
                flat_bytes,
                weighted,
                weighted_physical_writes,
                weighted_work,
            });
        }

        std::sort(
            candidates.begin(),
            candidates.end(),
            [](
                const candidate& left,
                const candidate& right) noexcept {

                // SHM-HYBRID-04M-WORK-01:
                // The first selector considered only structural child
                // traversal. The measured 04M execution gain also comes from
                // moving many physical writes into the object-major flat
                // path, so rank by total hot work per retained flat byte.
                const auto left_score =
                    static_cast<long double>(
                        left.weighted_work) /
                    static_cast<long double>(
                        left.flat_bytes);

                const auto right_score =
                    static_cast<long double>(
                        right.weighted_work) /
                    static_cast<long double>(
                        right.flat_bytes);

                if (left_score != right_score) {
                    return left_score > right_score;
                }

                if (left.weighted_work !=
                    right.weighted_work) {
                    return
                        left.weighted_work >
                        right.weighted_work;
                }

                if (left.weighted_physical_writes !=
                    right.weighted_physical_writes) {
                    return
                        left.weighted_physical_writes >
                        right.weighted_physical_writes;
                }

                if (left.weighted_child_visits !=
                    right.weighted_child_visits) {
                    return
                        left.weighted_child_visits >
                        right.weighted_child_visits;
                }

                if (left.flat_bytes != right.flat_bytes) {
                    return left.flat_bytes < right.flat_bytes;
                }

                return
                    left.local_type_api <
                    right.local_type_api;
            });

        std::uint64_t selected_bytes = 0;
        std::uint64_t selected_weighted = 0;
        std::uint64_t selected_weighted_physical_writes = 0;
        std::uint64_t selected_weighted_work = 0;
        std::uint64_t hot_roots = 0;
        std::uint64_t selected_count = 0;

        for (const auto& current : candidates) {
            if (current.flat_bytes >
                budget_bytes -
                    selected_bytes) {
                continue;
            }

            const auto built =
                build_flat_api(current.local_type_api);

            if (built != shm_hybrid_04m_result::success) {
                output.reset();
                return built;
            }

            if (!add_u64(
                    selected_bytes,
                    current.flat_bytes) ||
                !add_u64(
                    selected_weighted,
                    current.weighted_child_visits) ||
                !add_u64(
                    selected_weighted_physical_writes,
                    current.weighted_physical_writes) ||
                !add_u64(
                    selected_weighted_work,
                    current.weighted_work) ||
                !add_u64(
                    hot_roots,
                    current.roots)) {

                output.reset();
                return shm_hybrid_04m_result::overflow;
            }

            ++selected_count;
        }

        const auto actual_payload =
            output.flat_payload_bytes();

        if (actual_payload >
                budget_bytes ||
            actual_payload != selected_bytes) {

            output.reset();
            return shm_hybrid_04m_result::invalid_input;
        }

        output.target_value = base.target_value;
        output.layout_size = base.layout_size;
        output.prepared_value = true;

        if (telemetry != nullptr) {
            telemetry->budget_bytes =
                budget_bytes;
            telemetry->candidate_type_apis =
                candidates.size();
            telemetry->selected_type_apis =
                selected_count;
            telemetry->hot_object_roots =
                hot_roots;
            telemetry->cold_object_roots =
                base.object_group_offsets.size() -
                static_cast<std::size_t>(hot_roots);
            telemetry->selected_weighted_child_visits =
                selected_weighted;
            telemetry->all_weighted_child_visits =
                all_weighted;
            telemetry->selected_weighted_physical_writes =
                selected_weighted_physical_writes;
            telemetry->all_weighted_physical_writes =
                all_weighted_physical_writes;
            telemetry->selected_weighted_work =
                selected_weighted_work;
            telemetry->all_weighted_work =
                all_weighted_work;

            telemetry->flat_apis =
                output.flat_apis.size();
            telemetry->flat_relative_references =
                output.relative_references.size();
            telemetry->flat_absolute_references =
                output.absolute_references.size();
            telemetry->flat_object_references =
                output.object_references.size();
            telemetry->flat_stores =
                output.stores.size();
            telemetry->flat_repeats =
                output.repeats.size();

            telemetry->flat_api_bytes =
                output.flat_apis.size() *
                sizeof(shm_hybrid_04m::flat_api);
            telemetry->flat_relative_reference_bytes =
                output.relative_references.size() *
                sizeof(shm_hybrid_04m::relative_reference);
            telemetry->flat_absolute_reference_bytes =
                output.absolute_references.size() *
                sizeof(shm_hybrid_04m::absolute_reference);
            telemetry->flat_object_reference_bytes =
                output.object_references.size() *
                sizeof(shm_hybrid_04m::object_reference);
            telemetry->flat_store_bytes =
                output.stores.size() *
                sizeof(shm_hybrid_04m::store_operation);
            telemetry->flat_repeat_bytes =
                output.repeats.size() *
                sizeof(shm_hybrid_04m::repeat_operation);
            telemetry->selection_map_bytes =
                output.flat_api_by_local.size() *
                sizeof(std::uint32_t);
            telemetry->flat_payload_bytes =
                actual_payload;
            telemetry->resident_bytes =
                output.resident_bytes();
            telemetry->base_type_batch_bytes =
                base.resident_bytes();
            telemetry->total_runtime_metadata_bytes =
                base.resident_bytes() +
                output.resident_bytes();
        }

        return shm_hybrid_04m_result::success;
    }

private:
    [[nodiscard]] bool valid_group(
        const shm_type_batch::root_group& group) const noexcept {

        return group.type_api != 0 &&
            group.type_api <= base.type_apis.size() &&
            group.roots.begin <=
                base.object_group_offsets.size() &&
            group.roots.count <=
                base.object_group_offsets.size() -
                    group.roots.begin;
    }

    [[nodiscard]] bool valid_range(
        shm_type_batch::range range,
        std::size_t size) const noexcept {

        return range.begin <= size &&
            range.count <= size - range.begin;
    }

    [[nodiscard]] shm_hybrid_04m_result profile_api(
        std::uint32_t type_api,
        api_profile& output_profile) {

        output_profile = {};

        if (type_api == 0 ||
            type_api > base.type_apis.size()) {
            return shm_hybrid_04m_result::invalid_input;
        }

        const auto index =
            static_cast<std::size_t>(type_api - 1);

        if (states[index] == profile_state::ready) {
            output_profile = profiles[index];
            return shm_hybrid_04m_result::success;
        }

        if (states[index] == profile_state::visiting) {
            return shm_hybrid_04m_result::invalid_input;
        }

        states[index] = profile_state::visiting;

        const auto& api = base.type_apis[index];

        if (!valid_range(
                api.relative_references,
                base.relative_references.size()) ||
            !valid_range(
                api.absolute_references,
                base.absolute_references.size()) ||
            !valid_range(
                api.object_references,
                base.object_references.size()) ||
            !valid_range(
                api.stores,
                base.stores.size()) ||
            !valid_range(
                api.children,
                base.children.size()) ||
            !valid_range(
                api.repeats,
                base.repeats.size())) {

            states[index] = profile_state::empty;
            return shm_hybrid_04m_result::invalid_input;
        }

        api_profile result;
        result.flat_relative_references =
            api.relative_references.count;
        result.flat_absolute_references =
            api.absolute_references.count;
        result.flat_object_references =
            api.object_references.count;
        result.flat_stores = api.stores.count;
        result.flat_repeats = api.repeats.count;

        for (std::uint32_t child_index = 0;
             child_index < api.children.count;
             ++child_index) {

            const auto& child =
                base.children[
                    static_cast<std::size_t>(
                        api.children.begin) +
                    child_index];

            api_profile child_profile;
            const auto profiled =
                profile_api(
                    child.type_api,
                    child_profile);

            if (profiled != shm_hybrid_04m_result::success) {
                states[index] = profile_state::empty;
                return profiled;
            }

            if (!add_u64(
                    result.flat_relative_references,
                    child_profile.flat_relative_references) ||
                !add_u64(
                    result.flat_absolute_references,
                    child_profile.flat_absolute_references) ||
                !add_u64(
                    result.flat_object_references,
                    child_profile.flat_object_references) ||
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
                    child_profile.flattenable_child_visits)) {

                states[index] = profile_state::empty;
                return shm_hybrid_04m_result::overflow;
            }
        }

        profiles[index] = result;
        states[index] = profile_state::ready;
        output_profile = result;
        return shm_hybrid_04m_result::success;
    }

    [[nodiscard]] bool add_record_offset(
        shm_record_offset base_offset,
        shm_record_offset local_offset,
        shm_record_offset& output_offset) const noexcept {

        const auto value =
            static_cast<std::uint64_t>(base_offset) +
            static_cast<std::uint64_t>(local_offset);

        if (value >
            (std::numeric_limits<
                shm_record_offset>::max)()) {
            return false;
        }

        output_offset =
            static_cast<shm_record_offset>(value);
        return true;
    }

    [[nodiscard]] bool make_range(
        std::size_t begin,
        std::size_t end,
        shm_hybrid_04m::range& output_range) const noexcept {

        if (end < begin ||
            begin >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)()) ||
            end - begin >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)())) {
            return false;
        }

        output_range.begin =
            static_cast<std::uint32_t>(begin);
        output_range.count =
            static_cast<std::uint32_t>(end - begin);
        return true;
    }

    [[nodiscard]] shm_hybrid_04m_result flatten_api(
        std::uint32_t local_type_api,
        shm_record_offset base_offset) {

        if (local_type_api == 0 ||
            local_type_api > base.type_apis.size()) {
            return shm_hybrid_04m_result::invalid_input;
        }

        const auto& api =
            base.type_apis[local_type_api - 1];

        if (!valid_range(
                api.relative_references,
                base.relative_references.size()) ||
            !valid_range(
                api.absolute_references,
                base.absolute_references.size()) ||
            !valid_range(
                api.object_references,
                base.object_references.size()) ||
            !valid_range(api.stores, base.stores.size()) ||
            !valid_range(api.children, base.children.size()) ||
            !valid_range(api.repeats, base.repeats.size())) {
            return shm_hybrid_04m_result::invalid_input;
        }

        for (std::uint32_t index = 0;
             index < api.relative_references.count;
             ++index) {

            const auto& source =
                base.relative_references[
                    static_cast<std::size_t>(
                        api.relative_references.begin) +
                    index];

            shm_hybrid_04m::relative_reference target;

            if (!add_record_offset(
                    base_offset,
                    source.target,
                    target.target) ||
                !add_record_offset(
                    base_offset,
                    source.source,
                    target.source)) {
                return shm_hybrid_04m_result::overflow;
            }

            output.relative_references.push_back(target);
        }

        for (std::uint32_t index = 0;
             index < api.absolute_references.count;
             ++index) {

            const auto& source =
                base.absolute_references[
                    static_cast<std::size_t>(
                        api.absolute_references.begin) +
                    index];

            shm_hybrid_04m::absolute_reference target;
            target.source = source.source;

            if (!add_record_offset(
                    base_offset,
                    source.target,
                    target.target)) {
                return shm_hybrid_04m_result::overflow;
            }

            output.absolute_references.push_back(target);
        }

        for (std::uint32_t index = 0;
             index < api.object_references.count;
             ++index) {

            const auto& source =
                base.object_references[
                    static_cast<std::size_t>(
                        api.object_references.begin) +
                    index];

            shm_hybrid_04m::object_reference target;
            target.object_slot = source.object_slot;

            if (!add_record_offset(
                    base_offset,
                    source.target,
                    target.target)) {
                return shm_hybrid_04m_result::overflow;
            }

            output.object_references.push_back(target);
        }

        for (std::uint32_t index = 0;
             index < api.stores.count;
             ++index) {

            const auto& source =
                base.stores[
                    static_cast<std::size_t>(
                        api.stores.begin) +
                    index];

            shm_hybrid_04m::store_operation target;
            target.constant = source.constant;
            target.size = source.size;

            if (!add_record_offset(
                    base_offset,
                    source.target,
                    target.target)) {
                return shm_hybrid_04m_result::overflow;
            }

            output.stores.push_back(target);
        }

        for (std::uint32_t index = 0;
             index < api.repeats.count;
             ++index) {

            const auto& source =
                base.repeats[
                    static_cast<std::size_t>(
                        api.repeats.begin) +
                    index];

            shm_hybrid_04m::repeat_operation target;
            target.local_type_api = source.type_api;
            target.stride = source.stride;
            target.count = source.count;

            if (!add_record_offset(
                    base_offset,
                    source.target,
                    target.target)) {
                return shm_hybrid_04m_result::overflow;
            }

            output.repeats.push_back(target);
        }

        for (std::uint32_t index = 0;
             index < api.children.count;
             ++index) {

            const auto& child =
                base.children[
                    static_cast<std::size_t>(
                        api.children.begin) +
                    index];

            shm_record_offset child_base = 0;
            if (!add_record_offset(
                    base_offset,
                    child.target,
                    child_base)) {
                return shm_hybrid_04m_result::overflow;
            }

            const auto result =
                flatten_api(
                    child.type_api,
                    child_base);

            if (result != shm_hybrid_04m_result::success) {
                return result;
            }
        }

        return shm_hybrid_04m_result::success;
    }

    [[nodiscard]] shm_hybrid_04m_result build_flat_api(
        std::uint32_t local_type_api) {

        if (local_type_api == 0 ||
            local_type_api >
                output.flat_api_by_local.size() ||
            output.flat_api_by_local[
                local_type_api - 1] != 0) {
            return shm_hybrid_04m_result::invalid_input;
        }

        const auto relative_begin =
            output.relative_references.size();
        const auto absolute_begin =
            output.absolute_references.size();
        const auto object_begin =
            output.object_references.size();
        const auto store_begin =
            output.stores.size();
        const auto repeat_begin =
            output.repeats.size();

        const auto flattened =
            flatten_api(local_type_api, 0);

        if (flattened != shm_hybrid_04m_result::success) {
            return flattened;
        }

        shm_hybrid_04m::flat_api api;

        if (!make_range(
                relative_begin,
                output.relative_references.size(),
                api.relative_references) ||
            !make_range(
                absolute_begin,
                output.absolute_references.size(),
                api.absolute_references) ||
            !make_range(
                object_begin,
                output.object_references.size(),
                api.object_references) ||
            !make_range(
                store_begin,
                output.stores.size(),
                api.stores) ||
            !make_range(
                repeat_begin,
                output.repeats.size(),
                api.repeats)) {
            return shm_hybrid_04m_result::overflow;
        }

        if (output.flat_apis.size() >=
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {
            return shm_hybrid_04m_result::overflow;
        }

        output.flat_apis.push_back(api);
        output.flat_api_by_local[
            local_type_api - 1] =
            static_cast<std::uint32_t>(
                output.flat_apis.size());

        return shm_hybrid_04m_result::success;
    }

    const shm_type_batch& base;
    shm_hybrid_04m& output;
    shm_hybrid_04m_prepare_telemetry* telemetry = nullptr;
    std::uint64_t budget_bytes = 0;
    std::vector<profile_state> states;
    std::vector<api_profile> profiles;
};

class shm_hybrid_04m_executor final {
public:
    shm_hybrid_04m_executor(
        const shm_type_batch& base,
        const shm_hybrid_04m& hybrid,
        const server_abi_configuration& abi,
        const shm_layout& layout,
        std::span<std::byte> shm,
        shm_hybrid_04m_execute_telemetry* telemetry) noexcept
        : base(base),
          hybrid(hybrid),
          abi(abi),
          layout(layout),
          shm(shm),
          telemetry(telemetry) {
    }

    [[nodiscard]] shm_hybrid_04m_result objects() noexcept {

        const auto valid = validate();
        if (valid != shm_hybrid_04m_result::success) {
            return valid;
        }

        // Hot selected roots run object-major in original Runtime object order.
        for (const auto& object : base.objects) {
            if (object.type_api == 0) {
                continue;
            }

            if (object.type_api >
                hybrid.flat_api_by_local.size()) {
                return shm_hybrid_04m_result::invalid_input;
            }

            const auto flat_api =
                hybrid.flat_api_by_local[
                    object.type_api - 1];

            if (flat_api == 0) {
                continue;
            }

            const auto result =
                apply_flat_api(
                    flat_api,
                    object.offset);

            if (result != shm_hybrid_04m_result::success) {
                return result;
            }

            if (telemetry != nullptr) {
                ++telemetry->hot_object_roots;
            }
        }

        // Cold roots retain the compact local Type Batch execution model.
        for (const auto& group : base.object_groups) {
            if (!valid_group(group)) {
                return shm_hybrid_04m_result::invalid_input;
            }

            if (hybrid.flat_api_by_local[
                    group.type_api - 1] != 0) {
                continue;
            }

            const auto begin =
                static_cast<std::size_t>(
                    group.roots.begin);
            const auto end =
                begin + group.roots.count;

            for (std::size_t position = begin;
                 position < end;
                 position +=
                     shm_type_batch::execution_batch_size) {

                const auto count =
                    (std::min)(
                        static_cast<std::size_t>(
                            shm_type_batch::execution_batch_size),
                        end - position);

                const auto result =
                    apply_local_batch(
                        group.type_api,
                        std::span<const shm_offset>{
                            base.object_group_offsets.data() +
                                position,
                            count},
                        0);

                if (result != shm_hybrid_04m_result::success) {
                    return result;
                }

                if (telemetry != nullptr) {
                    ++telemetry->cold_batches;
                    telemetry->cold_object_roots += count;
                }
            }
        }

        // Object-specific construction stays shared and is applied exactly once.
        for (const auto& object : base.objects) {
            if (object.patch == 0) {
                continue;
            }

            if (object.patch > base.object_patches.size()) {
                return shm_hybrid_04m_result::invalid_input;
            }

            const auto result =
                execute_local_store(
                    base.object_patches[
                        object.patch - 1],
                    object.offset);

            if (result != shm_hybrid_04m_result::success) {
                return result;
            }

            if (telemetry != nullptr) {
                ++telemetry->object_patch_writes;
            }
        }

        if (telemetry != nullptr) {
            telemetry->objects = base.objects.size();
        }

        return shm_hybrid_04m_result::success;
    }

private:
    [[nodiscard]] shm_hybrid_04m_result validate() noexcept {

        if (!base.prepared_value ||
            !hybrid.prepared_value ||
            base.target_value != abi.target ||
            hybrid.target_value != abi.target ||
            layout.target() != abi.target ||
            base.layout_size != layout.size() ||
            hybrid.layout_size != layout.size() ||
            hybrid.flat_api_by_local.size() !=
                base.type_apis.size() ||
            layout.size() > shm.size() ||
            (layout.size() != 0 && shm.data() == nullptr) ||
            !host_compatible(abi.target)) {
            return shm_hybrid_04m_result::incompatible_abi;
        }

        if (!abi_layout_properties(
                abi.target,
                properties) ||
            (properties.reference_size != 4 &&
             properties.reference_size != 8)) {
            return shm_hybrid_04m_result::incompatible_abi;
        }

        const auto address =
            reinterpret_cast<std::uintptr_t>(
                shm.data());

        const auto mask =
            properties.reference_size == 4
            ? static_cast<std::uint64_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())
            : (std::numeric_limits<
                std::uint64_t>::max)();

        if (address == 0 ||
            address > mask ||
            (layout.size() != 0 &&
             layout.size() - 1 >
                mask -
                    static_cast<std::uint64_t>(
                        address))) {
            return shm_hybrid_04m_result::overflow;
        }

        return shm_hybrid_04m_result::success;
    }

    [[nodiscard]] bool valid_group(
        const shm_type_batch::root_group& group) const noexcept {

        return group.type_api != 0 &&
            group.type_api <= base.type_apis.size() &&
            group.roots.begin <=
                base.object_group_offsets.size() &&
            group.roots.count <=
                base.object_group_offsets.size() -
                    group.roots.begin;
    }

    template <typename Range>
    [[nodiscard]] static bool valid_range(
        Range range,
        std::size_t size) noexcept {

        return range.begin <= size &&
            range.count <= size - range.begin;
    }

    [[nodiscard]] shm_hybrid_04m_result write_reference(
        std::byte* target,
        const std::byte* source) noexcept {

        if (target == nullptr || source == nullptr) {
            return shm_hybrid_04m_result::invalid_input;
        }

        const auto value =
            reinterpret_cast<std::uintptr_t>(source);

        if (properties.reference_size == 4) {
            if (value >
                (std::numeric_limits<
                    std::uint32_t>::max)()) {
                return shm_hybrid_04m_result::overflow;
            }

            const auto native =
                static_cast<std::uint32_t>(value);
            std::memcpy(target, &native, sizeof(native));
            return shm_hybrid_04m_result::success;
        }

        if (properties.reference_size == 8) {
            const auto native =
                static_cast<std::uint64_t>(value);
            std::memcpy(target, &native, sizeof(native));
            return shm_hybrid_04m_result::success;
        }

        return shm_hybrid_04m_result::incompatible_abi;
    }

    [[nodiscard]] shm_hybrid_04m_result execute_flat_store(
        const shm_hybrid_04m::store_operation& operation,
        shm_offset base_offset) noexcept {

        if (operation.size == 0 ||
            operation.size > 16 ||
            operation.constant >= base.constants.size() ||
            base_offset >= base.layout_size ||
            operation.target >
                base.layout_size - base_offset ||
            static_cast<shm_offset>(operation.size) >
                base.layout_size - base_offset -
                    operation.target) {
            return shm_hybrid_04m_result::invalid_input;
        }

        std::memcpy(
            shm.data() +
                static_cast<std::size_t>(
                    base_offset + operation.target),
            base.constants[operation.constant].data(),
            operation.size);

        if (telemetry != nullptr) {
            ++telemetry->store_writes;
        }

        return shm_hybrid_04m_result::success;
    }

    [[nodiscard]] shm_hybrid_04m_result execute_local_store(
        const shm_type_batch::store_operation& operation,
        shm_offset base_offset) noexcept {

        if (operation.size == 0 ||
            operation.size > 16 ||
            operation.constant >= base.constants.size() ||
            base_offset >= base.layout_size ||
            operation.target >
                base.layout_size - base_offset ||
            static_cast<shm_offset>(operation.size) >
                base.layout_size - base_offset -
                    operation.target) {
            return shm_hybrid_04m_result::invalid_input;
        }

        std::memcpy(
            shm.data() +
                static_cast<std::size_t>(
                    base_offset + operation.target),
            base.constants[operation.constant].data(),
            operation.size);

        if (telemetry != nullptr) {
            ++telemetry->store_writes;
        }

        return shm_hybrid_04m_result::success;
    }

    [[nodiscard]] shm_hybrid_04m_result apply_flat_api(
        std::uint32_t flat_api,
        shm_offset base_offset) noexcept {

        if (flat_api == 0 ||
            flat_api > hybrid.flat_apis.size() ||
            base_offset >= base.layout_size) {
            return shm_hybrid_04m_result::invalid_input;
        }

        const auto api =
            hybrid.flat_apis[flat_api - 1];

        if (!valid_range(
                api.relative_references,
                hybrid.relative_references.size()) ||
            !valid_range(
                api.absolute_references,
                hybrid.absolute_references.size()) ||
            !valid_range(
                api.object_references,
                hybrid.object_references.size()) ||
            !valid_range(api.stores, hybrid.stores.size()) ||
            !valid_range(api.repeats, hybrid.repeats.size())) {
            return shm_hybrid_04m_result::invalid_input;
        }

        if (telemetry != nullptr) {
            ++telemetry->flat_api_applications;
        }

        auto* const object_base =
            shm.data() +
            static_cast<std::size_t>(base_offset);

        for (std::uint32_t index = 0;
             index < api.relative_references.count;
             ++index) {

            const auto& reference =
                hybrid.relative_references[
                    static_cast<std::size_t>(
                        api.relative_references.begin) +
                    index];

            if (reference.target >=
                    base.layout_size - base_offset ||
                reference.source >=
                    base.layout_size - base_offset) {
                return shm_hybrid_04m_result::invalid_input;
            }

            const auto result =
                write_reference(
                    object_base + reference.target,
                    object_base + reference.source);

            if (result != shm_hybrid_04m_result::success) {
                return result;
            }

            if (telemetry != nullptr) {
                ++telemetry->reference_writes;
                ++telemetry->relative_reference_writes;
            }
        }

        for (std::uint32_t index = 0;
             index < api.absolute_references.count;
             ++index) {

            const auto& reference =
                hybrid.absolute_references[
                    static_cast<std::size_t>(
                        api.absolute_references.begin) +
                    index];

            if (reference.target >=
                    base.layout_size - base_offset ||
                reference.source >= base.layout_size) {
                return shm_hybrid_04m_result::invalid_input;
            }

            const auto result =
                write_reference(
                    object_base + reference.target,
                    shm.data() +
                        static_cast<std::size_t>(
                            reference.source));

            if (result != shm_hybrid_04m_result::success) {
                return result;
            }

            if (telemetry != nullptr) {
                ++telemetry->reference_writes;
                ++telemetry->absolute_reference_writes;
            }
        }

        for (std::uint32_t index = 0;
             index < api.object_references.count;
             ++index) {

            const auto& reference =
                hybrid.object_references[
                    static_cast<std::size_t>(
                        api.object_references.begin) +
                    index];

            if (reference.target >=
                    base.layout_size - base_offset ||
                reference.object_slot == 0 ||
                reference.object_slot >
                    base.object_where.size()) {
                return shm_hybrid_04m_result::invalid_input;
            }

            const auto source_offset =
                base.object_where[
                    reference.object_slot - 1];

            if (source_offset == invalid_where() ||
                source_offset >= base.layout_size) {
                return shm_hybrid_04m_result::invalid_input;
            }

            const auto result =
                write_reference(
                    object_base + reference.target,
                    shm.data() +
                        static_cast<std::size_t>(
                            source_offset));

            if (result != shm_hybrid_04m_result::success) {
                return result;
            }

            if (telemetry != nullptr) {
                ++telemetry->reference_writes;
                ++telemetry->object_reference_writes;
            }
        }

        for (std::uint32_t index = 0;
             index < api.stores.count;
             ++index) {

            const auto result =
                execute_flat_store(
                    hybrid.stores[
                        static_cast<std::size_t>(
                            api.stores.begin) +
                        index],
                    base_offset);

            if (result != shm_hybrid_04m_result::success) {
                return result;
            }
        }

        for (std::uint32_t index = 0;
             index < api.repeats.count;
             ++index) {

            const auto& repeat =
                hybrid.repeats[
                    static_cast<std::size_t>(
                        api.repeats.begin) +
                    index];

            if (repeat.local_type_api == 0 ||
                repeat.local_type_api >
                    base.type_apis.size() ||
                repeat.stride == 0 ||
                repeat.count == 0 ||
                repeat.target >=
                    base.layout_size - base_offset) {
                return shm_hybrid_04m_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->repeat_visits;
                telemetry->repeat_iterations +=
                    repeat.count;
            }

            shm_offset current =
                base_offset + repeat.target;

            for (std::uint64_t iteration = 0;
                 iteration < repeat.count;
                 ++iteration) {

                const auto result =
                    apply_local_single(
                        repeat.local_type_api,
                        current);

                if (result != shm_hybrid_04m_result::success) {
                    return result;
                }

                if (iteration + 1 != repeat.count) {
                    if (repeat.stride >
                        base.layout_size - current) {
                        return shm_hybrid_04m_result::overflow;
                    }
                    current += repeat.stride;
                }
            }
        }

        return shm_hybrid_04m_result::success;
    }

    [[nodiscard]] shm_hybrid_04m_result apply_local_single(
        std::uint32_t type_api,
        shm_offset root) noexcept {

        return apply_local_batch(
            type_api,
            std::span<const shm_offset>{&root, 1},
            0);
    }

    [[nodiscard]] shm_hybrid_04m_result apply_local_batch(
        std::uint32_t type_api,
        std::span<const shm_offset> roots,
        shm_offset relative_base) noexcept {

        if (type_api == 0 ||
            type_api > base.type_apis.size() ||
            roots.empty()) {
            return shm_hybrid_04m_result::invalid_input;
        }

        const auto api =
            base.type_apis[type_api - 1];

        if (!valid_range(
                api.relative_references,
                base.relative_references.size()) ||
            !valid_range(
                api.absolute_references,
                base.absolute_references.size()) ||
            !valid_range(
                api.object_references,
                base.object_references.size()) ||
            !valid_range(api.stores, base.stores.size()) ||
            !valid_range(api.children, base.children.size()) ||
            !valid_range(api.repeats, base.repeats.size())) {
            return shm_hybrid_04m_result::invalid_input;
        }

        if (telemetry != nullptr) {
            ++telemetry->local_batch_api_applications;
        }

        for (const auto root : roots) {
            if (root >= base.layout_size ||
                relative_base >
                    base.layout_size - root) {
                return shm_hybrid_04m_result::invalid_input;
            }

            const auto base_offset =
                root + relative_base;

            if (base_offset >= base.layout_size) {
                return shm_hybrid_04m_result::invalid_input;
            }

            auto* const object_base =
                shm.data() +
                static_cast<std::size_t>(base_offset);

            for (std::uint32_t index = 0;
                 index < api.relative_references.count;
                 ++index) {

                const auto& reference =
                    base.relative_references[
                        static_cast<std::size_t>(
                            api.relative_references.begin) +
                        index];

                if (reference.target >=
                        base.layout_size - base_offset ||
                    reference.source >=
                        base.layout_size - base_offset) {
                    return shm_hybrid_04m_result::invalid_input;
                }

                const auto result =
                    write_reference(
                        object_base + reference.target,
                        object_base + reference.source);

                if (result != shm_hybrid_04m_result::success) {
                    return result;
                }

                if (telemetry != nullptr) {
                    ++telemetry->reference_writes;
                    ++telemetry->relative_reference_writes;
                }
            }

            for (std::uint32_t index = 0;
                 index < api.absolute_references.count;
                 ++index) {

                const auto& reference =
                    base.absolute_references[
                        static_cast<std::size_t>(
                            api.absolute_references.begin) +
                        index];

                if (reference.target >=
                        base.layout_size - base_offset ||
                    reference.source >= base.layout_size) {
                    return shm_hybrid_04m_result::invalid_input;
                }

                const auto result =
                    write_reference(
                        object_base + reference.target,
                        shm.data() +
                            static_cast<std::size_t>(
                                reference.source));

                if (result != shm_hybrid_04m_result::success) {
                    return result;
                }

                if (telemetry != nullptr) {
                    ++telemetry->reference_writes;
                    ++telemetry->absolute_reference_writes;
                }
            }

            for (std::uint32_t index = 0;
                 index < api.object_references.count;
                 ++index) {

                const auto& reference =
                    base.object_references[
                        static_cast<std::size_t>(
                            api.object_references.begin) +
                        index];

                if (reference.target >=
                        base.layout_size - base_offset ||
                    reference.object_slot == 0 ||
                    reference.object_slot >
                        base.object_where.size()) {
                    return shm_hybrid_04m_result::invalid_input;
                }

                const auto source_offset =
                    base.object_where[
                        reference.object_slot - 1];

                if (source_offset == invalid_where() ||
                    source_offset >= base.layout_size) {
                    return shm_hybrid_04m_result::invalid_input;
                }

                const auto result =
                    write_reference(
                        object_base + reference.target,
                        shm.data() +
                            static_cast<std::size_t>(
                                source_offset));

                if (result != shm_hybrid_04m_result::success) {
                    return result;
                }

                if (telemetry != nullptr) {
                    ++telemetry->reference_writes;
                    ++telemetry->object_reference_writes;
                }
            }

            for (std::uint32_t index = 0;
                 index < api.stores.count;
                 ++index) {

                const auto result =
                    execute_local_store(
                        base.stores[
                            static_cast<std::size_t>(
                                api.stores.begin) +
                            index],
                        base_offset);

                if (result != shm_hybrid_04m_result::success) {
                    return result;
                }
            }
        }

        for (std::uint32_t index = 0;
             index < api.children.count;
             ++index) {

            const auto& child =
                base.children[
                    static_cast<std::size_t>(
                        api.children.begin) +
                    index];

            if (child.type_api == 0 ||
                child.type_api > base.type_apis.size() ||
                child.target >
                    (std::numeric_limits<shm_offset>::max)() -
                        relative_base) {
                return shm_hybrid_04m_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->local_child_visits;
            }

            const auto result =
                apply_local_batch(
                    child.type_api,
                    roots,
                    relative_base + child.target);

            if (result != shm_hybrid_04m_result::success) {
                return result;
            }
        }

        for (std::uint32_t index = 0;
             index < api.repeats.count;
             ++index) {

            const auto& repeat =
                base.repeats[
                    static_cast<std::size_t>(
                        api.repeats.begin) +
                    index];

            if (repeat.type_api == 0 ||
                repeat.type_api > base.type_apis.size() ||
                repeat.stride == 0 ||
                repeat.count == 0 ||
                repeat.target >
                    (std::numeric_limits<shm_offset>::max)() -
                        relative_base) {
                return shm_hybrid_04m_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->repeat_visits;
                telemetry->repeat_iterations +=
                    repeat.count;
            }

            shm_offset current =
                relative_base + repeat.target;

            for (std::uint64_t iteration = 0;
                 iteration < repeat.count;
                 ++iteration) {

                const auto result =
                    apply_local_batch(
                        repeat.type_api,
                        roots,
                        current);

                if (result != shm_hybrid_04m_result::success) {
                    return result;
                }

                if (iteration + 1 != repeat.count) {
                    if (repeat.stride >
                        (std::numeric_limits<shm_offset>::max)() -
                            current) {
                        return shm_hybrid_04m_result::overflow;
                    }
                    current += repeat.stride;
                }
            }
        }

        return shm_hybrid_04m_result::success;
    }

    const shm_type_batch& base;
    const shm_hybrid_04m& hybrid;
    const server_abi_configuration& abi;
    const shm_layout& layout;
    std::span<std::byte> shm;
    shm_hybrid_04m_execute_telemetry* telemetry = nullptr;
    abi_properties properties{};
};

std::size_t shm_hybrid_04m::flat_payload_bytes() const noexcept {

    return
        flat_apis.size() * sizeof(flat_api) +
        relative_references.size() * sizeof(relative_reference) +
        absolute_references.size() * sizeof(absolute_reference) +
        object_references.size() * sizeof(object_reference) +
        stores.size() * sizeof(store_operation) +
        repeats.size() * sizeof(repeat_operation);
}

std::size_t shm_hybrid_04m::resident_bytes() const noexcept {

    return
        flat_api_by_local.size() * sizeof(std::uint32_t) +
        flat_payload_bytes();
}

void shm_hybrid_04m::reset() noexcept {

    flat_api_by_local.clear();
    flat_apis.clear();
    relative_references.clear();
    absolute_references.clear();
    object_references.clear();
    stores.clear();
    repeats.clear();

    target_value = abi_target::windows_x64;
    layout_size = 0;
    prepared_value = false;
}

shm_hybrid_04m_result prepare_shm_hybrid_budget(
    const shm_type_batch& base,
    std::uint64_t flat_budget_bytes,
    shm_hybrid_04m& output,
    shm_hybrid_04m_prepare_telemetry* telemetry) noexcept {

    if (telemetry != nullptr) {
        *telemetry = {};
    }

    try {
        shm_hybrid_04m_builder builder{
            base,
            flat_budget_bytes,
            output,
            telemetry,
        };

        return builder.build();
    }
    catch (...) {
        // Free prepare is not a friend of the physical cache. Move-assign a
        // fresh value so failed prepare never publishes a partial image.
        output = shm_hybrid_04m{};
        return shm_hybrid_04m_result::failed;
    }
}

shm_hybrid_04m_result prepare_shm_hybrid_04m(
    const shm_type_batch& base,
    shm_hybrid_04m& output,
    shm_hybrid_04m_prepare_telemetry* telemetry) noexcept {

    return prepare_shm_hybrid_budget(
        base,
        shm_hybrid_04m::flat_budget_bytes,
        output,
        telemetry);
}

shm_hybrid_04m_result materialize_shm_hybrid_04m_objects(
    const shm_type_batch& base,
    const shm_hybrid_04m& hybrid,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_hybrid_04m_execute_telemetry* telemetry) noexcept {

    if (telemetry != nullptr) {
        *telemetry = {};
    }

    shm_hybrid_04m_executor executor{
        base,
        hybrid,
        abi,
        layout,
        shm,
        telemetry,
    };

    return executor.objects();
}

}
