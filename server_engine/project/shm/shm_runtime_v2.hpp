/*
 * SHM Runtime V2 - production physical Runtime construction.
 *
 * Fixed policy:
 *   - object-driven local Type API preparation
 *   - bottom-up leaf inlining up to 64 physical operations
 *   - object-major hot materialization
 *   - static Graph links compiled to physical endpoint programs
 *
 * identity_ref / Graph endpoint metadata are consumed during prepare only.
 * Runtime execution receives only offsets, native reference words and compact
 * dereference programs. No semantic name/hash lookup is present in the
 * materialization path.
 */
#pragma once

#include "shm_type_batch.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace cw::server {

class compiled_project_view;
class runtime_project_view;
class shm_runtime_v2_link_builder;
class shm_runtime_v2_link_executor;

using shm_runtime_v2_result =
    shm_type_batch_result;

using shm_runtime_v2_prepare_telemetry =
    shm_type_batch_prepare_telemetry;

using shm_runtime_v2_execute_telemetry =
    shm_type_batch_execute_telemetry;

struct shm_runtime_v2_link_telemetry final {
    std::uint64_t links_prepared = 0;
    std::uint64_t endpoint_programs = 0;
    std::uint64_t dereference_steps = 0;

    std::uint64_t targets_marked = 0;
    std::uint64_t links_resolved = 0;
    std::uint64_t recursive_resolutions = 0;
    std::uint64_t dereference_reads = 0;
};

struct shm_runtime_v2_initialization_telemetry final {
    std::uint64_t initializations_prepared = 0;
    std::uint64_t endpoint_programs = 0;
    std::uint64_t dereference_steps = 0;

    std::uint64_t writes = 0;
    std::uint64_t dereference_reads = 0;
};

inline constexpr std::uint32_t
    shm_runtime_v2_inline_leaf_limit = 64;


struct shm_runtime_v2_physical_endpoint_program final {
    shm_offset root = 0;
    shm_offset tail = 0;
    std::uint32_t dereference_begin = 0;
    std::uint32_t dereference_count = 0;
};

static_assert(
    sizeof(shm_runtime_v2_physical_endpoint_program) == 24);

inline constexpr std::uint32_t
    shm_runtime_v2_physical_link_live = 0x01u;

inline constexpr std::uint32_t
    shm_runtime_v2_physical_link_source_reference = 0x02u;

struct shm_runtime_v2_physical_link final {
    // Direct position when the corresponding program slot is zero.
    shm_offset source = 0;
    shm_offset target = 0;

    // One-based slots in runtime_endpoint_programs; zero means direct.
    std::uint32_t source_program = 0;
    std::uint32_t target_program = 0;

    std::uint32_t flags = 0;
    std::uint32_t reserved = 0;
};

static_assert(
    sizeof(shm_runtime_v2_physical_link) == 32);

// Compact physical endpoints use 30-bit SHM offsets or program IDs. Larger
// layouts/program spaces retain the original lossless 32-byte record.
struct shm_runtime_v2_compact_link final { std::uint32_t source = 0, target = 0; };
static_assert(sizeof(shm_runtime_v2_compact_link) == 8);
inline constexpr std::uint32_t shm_runtime_v2_compact_limit = 1u << 30;
inline bool pack_shm_runtime_v2_link(const shm_runtime_v2_physical_link& link,
    shm_runtime_v2_compact_link& packed) noexcept {
    packed = {};
    if (link.reserved || (link.flags & ~3u)) return false;
    if (!(link.flags & shm_runtime_v2_physical_link_live))
        return link.flags == 0 && !link.source && !link.target && !link.source_program && !link.target_program;
    const auto source = link.source_program ? link.source_program : link.source;
    const auto target = link.target_program ? link.target_program : link.target;
    if (source >= shm_runtime_v2_compact_limit || target >= shm_runtime_v2_compact_limit ||
        (link.source_program && link.source) || (link.target_program && link.target)) return false;
    packed.source = static_cast<std::uint32_t>(source) | (link.source_program ? 1u << 30 : 0) |
        ((link.flags & shm_runtime_v2_physical_link_source_reference) ? 1u << 31 : 0);
    packed.target = static_cast<std::uint32_t>(target) | (link.target_program ? 1u << 30 : 0) | (1u << 31);
    return true;
}
inline shm_runtime_v2_physical_link unpack_shm_runtime_v2_link(shm_runtime_v2_compact_link packed) noexcept {
    shm_runtime_v2_physical_link link{};
    constexpr auto mask = shm_runtime_v2_compact_limit - 1;
    if (packed.source & (1u << 30)) link.source_program = packed.source & mask;
    else link.source = packed.source & mask;
    if (packed.target & (1u << 30)) link.target_program = packed.target & mask;
    else link.target = packed.target & mask;
    link.flags = (packed.target >> 31) | ((packed.source >> 31) << 1);
    return link;
}

struct shm_runtime_v2_physical_initialization final {
    // Direct position when target_program is zero.
    shm_offset target = 0;
    std::array<std::byte, 16> value{};

    // One-based slot in runtime_endpoint_programs; zero means direct.
    std::uint32_t target_program = 0;

    std::uint8_t size = 0;
    // For size>0, reserved[0] is an optional eight-bit byte-write mask.
    // size==0/reserved[0]==255 encodes char_array_store in value, referring
    // to runtime_initialization_bytes. For a bit store, reserved[1] holds
    // the bit position in the addressed byte plus one.
    std::uint8_t reserved[3]{};
};

static_assert(
    sizeof(shm_runtime_v2_physical_initialization) == 32);

struct shm_runtime_v2_physical_counts final {
    std::size_t link_slots = 0;
    std::size_t initializations = 0;
    std::uint64_t endpoint_programs = 0;
    std::uint64_t dereferences = 0;
    std::uint64_t initialization_bytes = 0;
    bool compact_links = false;
};

class shm_runtime_v2 final {
public:
    shm_runtime_v2() = default;

    shm_runtime_v2(const shm_runtime_v2&) = delete;
    shm_runtime_v2& operator=(const shm_runtime_v2&) = delete;

    shm_runtime_v2(shm_runtime_v2&&) noexcept = default;
    shm_runtime_v2& operator=(shm_runtime_v2&&) noexcept = default;

    [[nodiscard]] bool prepared() const noexcept {
        return area.prepared();
    }

    [[nodiscard]] std::size_t type_api_count() const noexcept {
        return area.type_api_count();
    }

    [[nodiscard]] std::size_t object_count() const noexcept {
        return area.object_count();
    }

    [[nodiscard]] std::size_t link_count() const noexcept {
        return live_link_count_value;
    }

    [[nodiscard]] std::size_t link_endpoint_program_count() const noexcept {
        return live_link_count_value * 2;
    }

    [[nodiscard]] std::size_t link_dereference_count() const noexcept {
        return link_dereference_count_value;
    }

    [[nodiscard]] std::size_t initialization_count() const noexcept {
        return persisted_physical_value
            ? persisted_initializations.size()
            : initializations.size();
    }

    [[nodiscard]] std::size_t initialization_dereference_count() const noexcept {
        const auto total =
            persisted_physical_value
            ? persisted_endpoint_dereferences.size()
            : endpoint_dereferences.size();

        return total >=
                link_dereference_count_value
            ? total -
                link_dereference_count_value
            : 0;
    }

    [[nodiscard]] bool persisted_physical() const noexcept {
        return persisted_physical_value;
    }

    // Counts the already validated construction plan without traversing G again.
    // Only an owned physical plan can be used to size a new runtime image.
    [[nodiscard]] bool physical_counts(shm_runtime_v2_physical_counts& output) const noexcept;

    [[nodiscard]] std::size_t construction_bytes() const noexcept {
        if (persisted_physical_value) {
            return
                area.resident_bytes() +
                persisted_link_states.size() +
                persisted_link_target_slots.size() *
                    sizeof(shm_offset);
        }

        return
            area.resident_bytes() +
            links.size() * sizeof(link_plan) +
            initializations.size() *
                sizeof(initialization_plan) +
            endpoint_dereferences.size() *
                sizeof(shm_offset) + initialization_bytes.size();
    }

private:
    enum class link_state : std::uint8_t {
        empty = 0,
        prepared,
        marked,
        resolving,
        resolved,
    };

    struct endpoint_program final {
        shm_offset root = 0;
        shm_offset tail = 0;

        std::uint32_t dereference_begin = 0;
        std::uint32_t dereference_count = 0;

        bool final_reference = false;
        std::uint8_t reserved[7]{};
    };

    static_assert(sizeof(endpoint_program) == 32);

    struct link_plan final {
        endpoint_program source{};
        endpoint_program target{};

        shm_offset target_slot = 0;
        shm_offset resolved_source = 0;

        link_state state = link_state::empty;
        bool live = false;
        std::uint8_t reserved[6]{};
    };

    static_assert(sizeof(link_plan) == 88);

    struct initialization_plan final {
        endpoint_program target{};
        std::array<std::byte, 16> value{};

        std::uint8_t size = 0;
        // Same byte mask / whole-array descriptor tag as the persisted record.
        std::uint8_t reserved[7]{};
    };

    static_assert(sizeof(initialization_plan) == 56);

    shm_type_batch area;

    // Stable Graph link WHERE -> physical plan. Dead Graph slots remain empty,
    // so ~link_handle remains a direct one-based index into this vector.
    std::vector<link_plan> links;

    // Source statements produce a dense final initialization list.
    std::vector<initialization_plan> initializations;
    std::vector<std::byte> initialization_bytes;
    std::span<const std::byte> persisted_initialization_bytes;

    // Shared physical endpoint bytecode for links and initializations.
    std::vector<shm_offset> endpoint_dereferences;

    std::size_t live_link_count_value = 0;
    std::size_t link_dereference_count_value = 0;


    // Variant-C zero-copy execution columns. These spans point directly into
    // compiled.bin and exist only during Runtime construction.
    class link_column_view {
    public:
        void assign(std::span<const std::byte> input, std::uint32_t stride) noexcept {
            bytes = input; compact = stride == 8; count = stride ? input.size() / stride : 0;
        }
        [[nodiscard]] std::size_t size() const noexcept { return count; }
        [[nodiscard]] shm_runtime_v2_physical_link operator[](std::size_t index) const noexcept {
            if (compact) {
                shm_runtime_v2_compact_link packed;
                std::memcpy(&packed, bytes.data() + index * 8, 8);
                return unpack_shm_runtime_v2_link(packed);
            }
            shm_runtime_v2_physical_link link;
            std::memcpy(&link, bytes.data() + index * 32, 32);
            return link;
        }
    private:
        std::span<const std::byte> bytes;
        std::size_t count = 0;
        bool compact = false;
    } persisted_links;
    bool compact_link_offsets = false;
    std::span<const shm_runtime_v2_physical_initialization>
        persisted_initializations;
    std::span<const shm_runtime_v2_physical_endpoint_program>
        persisted_endpoint_programs;
    std::span<const shm_offset>
        persisted_endpoint_dereferences;

    // Physical plans are immutable. Only dependency/cycle state is mutable.
    std::vector<link_state> persisted_link_states;
    std::vector<shm_offset> persisted_link_target_slots;

    bool persisted_physical_value = false;

    [[nodiscard]] std::size_t
    link_slot_count() const noexcept;

    [[nodiscard]] bool
    link_program_at(
        std::size_t index,
        endpoint_program& source,
        endpoint_program& target,
        bool& live) const noexcept;

    [[nodiscard]] link_state*
    link_state_at(
        std::size_t index) noexcept;

    [[nodiscard]] bool
    set_link_target_slot(
        std::size_t index,
        shm_offset value) noexcept;

    [[nodiscard]] bool
    link_target_slot_at(
        std::size_t index,
        shm_offset& value) const noexcept;

    [[nodiscard]] bool
    initialization_program_at(
        std::size_t index,
        initialization_plan& value) const noexcept;

    [[nodiscard]] std::size_t
    endpoint_dereference_count_total() const noexcept;

    [[nodiscard]] bool
    endpoint_dereference_at(
        std::size_t index,
        shm_offset& value) const noexcept;

    friend class shm_runtime_v2_link_builder;
    friend class shm_runtime_v2_link_executor;
    friend shm_runtime_v2_result
    prepare_shm_runtime_v2_persisted(
        const runtime_project_view&,
        const server_abi_configuration&,
        const shm_layout&,
        shm_runtime_v2&,
        shm_runtime_v2_prepare_telemetry*) noexcept;



    friend shm_runtime_v2_result
    encode_shm_runtime_v2_physical_columns(
        const compiled_project_view&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>) noexcept;

    friend shm_runtime_v2_result encode_shm_runtime_v2_physical_columns(
        const shm_runtime_v2&, const shm_layout&, std::span<std::byte>) noexcept;

    friend shm_runtime_v2_result
    attach_shm_runtime_v2_physical_columns(
        const runtime_project_view&,
        shm_runtime_v2&) noexcept;

    friend shm_runtime_v2_result
    prepare_shm_runtime_v2(
        const compiled_project_view&,
        const server_abi_configuration&,
        const shm_layout&,
        shm_runtime_v2&,
        shm_runtime_v2_prepare_telemetry*) noexcept;

    friend shm_runtime_v2_result
    materialize_shm_runtime_v2_canonical(
        const shm_runtime_v2&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_runtime_v2_execute_telemetry*) noexcept;

    friend shm_runtime_v2_result
    mark_shm_runtime_v2_links(
        shm_runtime_v2&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_runtime_v2_link_telemetry*) noexcept;

    friend shm_runtime_v2_result
    mark_shm_runtime_v2_persisted_direct_target_range(
        shm_runtime_v2&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_offset,
        shm_offset,
        shm_runtime_v2_link_telemetry*) noexcept;

    friend shm_runtime_v2_result
    mark_shm_runtime_v2_persisted_complex_targets(
        shm_runtime_v2&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_runtime_v2_link_telemetry*) noexcept;

    friend shm_runtime_v2_result
    materialize_shm_runtime_v2_objects(
        const shm_runtime_v2&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_runtime_v2_execute_telemetry*) noexcept;

    friend shm_runtime_v2_result
    materialize_shm_runtime_v2_objects_range(
        const shm_runtime_v2&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        std::size_t,
        std::size_t,
        shm_runtime_v2_execute_telemetry*) noexcept;

    friend shm_runtime_v2_result
    materialize_shm_runtime_v2_links(
        shm_runtime_v2&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_runtime_v2_link_telemetry*) noexcept;

    friend shm_runtime_v2_result
    materialize_shm_runtime_v2_initializations(
        shm_runtime_v2&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_runtime_v2_initialization_telemetry*) noexcept;
};


struct shm_runtime_v2_physical_plan_telemetry final {
    std::uint64_t links_ns = 0;
    std::uint64_t initializations_ns = 0;

    std::uint64_t link_slots = 0;
    std::uint64_t live_links = 0;
    std::uint64_t initializations = 0;

    std::uint64_t link_dereferences = 0;
    std::uint64_t initialization_dereferences = 0;
    std::uint64_t total_dereferences = 0;

    std::uint64_t parallel_lanes = 1;
};

// PUBLISH compiles links and source initializations once and encodes immutable
// same-WHERE Runtime columns directly into runtime.bin.
// Preparation owns its output and reads only the stable Project/ABI views.
// Optional telemetry is coarse: no clocks are read inside record loops.
[[nodiscard]] shm_runtime_v2_result prepare_shm_runtime_v2_physical_plan(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_runtime_v2& output,
    shm_runtime_v2_physical_plan_telemetry* telemetry = nullptr) noexcept;

[[nodiscard]] shm_runtime_v2_result encode_shm_runtime_v2_physical_columns(
    const shm_runtime_v2& runtime,
    const shm_layout& layout,
    std::span<std::byte> compiled_image) noexcept;

[[nodiscard]] shm_runtime_v2_result
encode_shm_runtime_v2_physical_columns(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> compiled_image) noexcept;

// LOAD trusts the PUBLISH image. This attaches mmap spans and allocates only
// the mutable link-resolution state. No record validation or semantic fallback.
[[nodiscard]] shm_runtime_v2_result
attach_shm_runtime_v2_physical_columns(
    const runtime_project_view& project,
    shm_runtime_v2& output) noexcept;

// Prepare resolves WHO / semantic endpoint paths once into physical Runtime
// object offsets and compact dereference programs.
// Production LOAD path: prepare only the remaining Type/INLINE-64 area and
// attach already-compiled physical link/initialization columns directly.
[[nodiscard]] shm_runtime_v2_result
prepare_shm_runtime_v2_persisted(
    const runtime_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_runtime_v2& output,
    shm_runtime_v2_prepare_telemetry* telemetry = nullptr) noexcept;

[[nodiscard]] shm_runtime_v2_result
prepare_shm_runtime_v2(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_runtime_v2& output,
    shm_runtime_v2_prepare_telemetry* telemetry = nullptr) noexcept;

// Canonical unconnected storage first.
[[nodiscard]] shm_runtime_v2_result
materialize_shm_runtime_v2_canonical(
    const shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_execute_telemetry* telemetry = nullptr) noexcept;

// Static-link pass 1. Direct target reference slots are marked before object
// construction. Object reference writes recognize/preserve these markers.
// Targets containing dereferences are deferred to link materialization, after
// constructor reference defaults exist.
[[nodiscard]] shm_runtime_v2_result
mark_shm_runtime_v2_links(
    shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_link_telemetry* telemetry = nullptr) noexcept;

// Parallel-mark primitive for persisted direct targets. The caller partitions
// the physical SHM target-address space into disjoint ranges; each physical
// target slot therefore has exactly one owning lane even if semantic endpoints
// could alias to the same storage.
[[nodiscard]] shm_runtime_v2_result
mark_shm_runtime_v2_persisted_direct_target_range(
    shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_offset target_begin,
    shm_offset target_end,
    shm_runtime_v2_link_telemetry* telemetry = nullptr) noexcept;

// Dereference-bearing persisted targets are marked serially after object
// construction because their final physical target depends on reference defaults.
[[nodiscard]] shm_runtime_v2_result
mark_shm_runtime_v2_persisted_complex_targets(
    shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_link_telemetry* telemetry = nullptr) noexcept;

// Hot object path is deliberately object-major.
[[nodiscard]] shm_runtime_v2_result
materialize_shm_runtime_v2_objects(
    const shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_execute_telemetry* telemetry = nullptr) noexcept;

[[nodiscard]] shm_runtime_v2_result
materialize_shm_runtime_v2_objects_range(
    const shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    std::size_t object_begin,
    std::size_t object_count,
    shm_runtime_v2_execute_telemetry* telemetry = nullptr) noexcept;

// Static-link pass 2. Sources are resolved from physical endpoint programs.
// Pending-link dependencies recurse by stable Graph link WHERE and cycles fail.
[[nodiscard]] shm_runtime_v2_result
materialize_shm_runtime_v2_links(
    shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_link_telemetry* telemetry = nullptr) noexcept;

// Final construction phase. Source scalar initializations run after all static
// links so endpoint programs may safely dereference linked native references.
[[nodiscard]] shm_runtime_v2_result
materialize_shm_runtime_v2_initializations(
    shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_initialization_telemetry* telemetry = nullptr) noexcept;

}
