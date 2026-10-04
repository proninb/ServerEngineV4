/*
 * SHM Runtime V2 - sparse physical construction.
 *
 * G is semantic truth. shm_layout is physical WHERE. This object retains only
 * physical writes required to construct a zeroed FIXED_DIRECT SHM.
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
class shm_sparse_builder;
class shm_sparse_executor;

enum class shm_sparse_result : std::uint8_t {
    success = 0,
    invalid_input,
    unsupported_type,
    incompatible_abi,
    overflow,
    failed,
};

struct shm_sparse_prepare_telemetry final {
    std::uint64_t named_programs = 0;
    std::uint64_t derived_programs = 0;
    std::uint64_t empty_programs = 0;

    std::uint64_t canonical_roots = 0;
    std::uint64_t object_roots = 0;

    std::uint64_t actions = 0;
    std::uint64_t reference_relative_actions = 0;
    std::uint64_t reference_absolute_actions = 0;
    std::uint64_t store_actions = 0;
    std::uint64_t call_actions = 0;
    std::uint64_t repeat_actions = 0;

    std::uint64_t reference_chain_steps_resolved = 0;
    std::uint64_t zero_actions_elided = 0;

    std::uint64_t action_size = 0;
    std::uint64_t hot_action_bytes = 0;
    std::uint64_t absolute_offsets = 0;
    std::uint64_t absolute_offset_bytes = 0;
    std::uint64_t constants = 0;
    std::uint64_t constant_bytes = 0;
    std::uint64_t repeat_descriptors = 0;
    std::uint64_t repeat_descriptor_bytes = 0;
    std::uint64_t root_bytes = 0;
    std::uint64_t resident_bytes = 0;
};

struct shm_sparse_execute_telemetry final {
    std::uint64_t root_executions = 0;
    std::uint64_t action_visits = 0;

    std::uint64_t reference_writes = 0;
    std::uint64_t store_writes = 0;

    std::uint64_t call_visits = 0;
    std::uint64_t repeat_visits = 0;
    std::uint64_t repeat_iterations = 0;
};

class shm_sparse_types final {
public:
    shm_sparse_types() = default;

    shm_sparse_types(const shm_sparse_types&) = delete;
    shm_sparse_types& operator=(const shm_sparse_types&) = delete;

    shm_sparse_types(shm_sparse_types&&) noexcept = default;
    shm_sparse_types& operator=(shm_sparse_types&&) noexcept = default;

    [[nodiscard]] bool prepared() const noexcept {
        return prepared_value;
    }

    [[nodiscard]] std::size_t action_count() const noexcept {
        return actions.size();
    }

    [[nodiscard]] std::size_t canonical_root_count() const noexcept {
        return canonical_roots.size();
    }

    [[nodiscard]] std::size_t object_root_count() const noexcept {
        return object_roots.size();
    }

    [[nodiscard]] std::size_t resident_bytes() const noexcept;

private:
    struct program_range final {
        std::uint32_t begin = 0;
        std::uint32_t count = 0;

        [[nodiscard]] constexpr bool empty() const noexcept {
            return count == 0;
        }
    };

    enum class action_kind : std::uint8_t {
        reference_relative = 1,
        reference_absolute,
        store,
        call,
        repeat,
    };

    struct action final {
        shm_record_offset target = 0;
        std::uint32_t arg0 = 0;
        std::uint32_t arg1 = 0;
        action_kind kind = action_kind::store;
        std::uint8_t aux = 0;
        std::uint16_t reserved = 0;
    };

    static_assert(sizeof(action) == 16);

    struct repeat_descriptor final {
        program_range child{};
        shm_offset stride = 0;
        std::uint64_t count = 0;
    };

    static_assert(sizeof(repeat_descriptor) == 24);

    struct root final {
        shm_offset offset = 0;
        program_range program{};
    };

    void reset() noexcept;

    std::vector<action> actions;
    std::vector<shm_offset> absolute_offsets;
    std::vector<std::array<std::byte, 16>> constants;
    std::vector<repeat_descriptor> repeat_descriptors;
    std::vector<root> canonical_roots;
    std::vector<root> object_roots;

    abi_target target_value = abi_target::windows_x64;
    shm_offset layout_size = 0;
    bool prepared_value = false;

    friend class shm_sparse_builder;
    friend class shm_sparse_executor;

    friend shm_sparse_result prepare_shm_sparse_types(
        const compiled_project_view&,
        const server_abi_configuration&,
        const shm_layout&,
        shm_sparse_types&,
        shm_sparse_prepare_telemetry*) noexcept;

    friend shm_sparse_result materialize_shm_sparse_canonical(
        const shm_sparse_types&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_sparse_execute_telemetry*) noexcept;

    friend shm_sparse_result materialize_shm_sparse_objects(
        const shm_sparse_types&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_sparse_execute_telemetry*) noexcept;
};

[[nodiscard]] shm_sparse_result prepare_shm_sparse_types(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_sparse_types& output,
    shm_sparse_prepare_telemetry* telemetry = nullptr) noexcept;

// These executors do not access G/project data.
[[nodiscard]] shm_sparse_result materialize_shm_sparse_canonical(
    const shm_sparse_types& sparse,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_sparse_execute_telemetry* telemetry = nullptr) noexcept;

[[nodiscard]] shm_sparse_result materialize_shm_sparse_objects(
    const shm_sparse_types& sparse,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_sparse_execute_telemetry* telemetry = nullptr) noexcept;

}
