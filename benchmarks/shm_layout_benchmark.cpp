/*
 * SHM-LAYOUT-COMPARE-01
 *
 * Cold comparison tool. It mmap-binds one compiled.bin, builds the accepted
 * Runtime layout and SHM Runtime V2 layout independently, then compares every
 * externally meaningful physical result.
 *
 * This executable does not create SHM and does not run materialization.
 */
#include "configuration/server_configuration.hpp"
#include "project/persistence/compiled_project.hpp"
#include "project/persistence/project_artifact.hpp"
#include "project/runtime/runtime_layout.hpp"
#include "project/shm/shm_layout.hpp"
#include "project/shm/shm_materializer.hpp"
#include "project/shm/shm_sparse.hpp"
#include "project/shm/shm_type_area.hpp"
#include "project/shm/shm_type_batch.hpp"
#include "project/shm/shm_hybrid_profile.hpp"
#include "project/shm/shm_hybrid_04m.hpp"
#include "fixed_shared_memory.hpp"
#include "read_only_file_mapping.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>

namespace {

[[nodiscard]] bool parse_count(
    const char* value,
    std::size_t& output) noexcept {

    if (value == nullptr ||
        *value == '\0') {

        return false;
    }

    char* end = nullptr;

    const auto parsed =
        std::strtoull(
            value,
            &end,
            10);

    if (end == value ||
        *end != '\0' ||
        parsed >
            static_cast<unsigned long long>(
                (std::numeric_limits<
                    std::size_t>::max)())) {

        return false;
    }

    output =
        static_cast<std::size_t>(
            parsed);

    return true;
}

[[nodiscard]] cw::server::server_settings_configuration
make_settings() {

    cw::server::server_settings_configuration settings;

#if defined(_WIN32)
    settings.abi.target =
        cw::server::abi_target::windows_x64;
#else
    settings.abi.target =
        cw::server::abi_target::posix_x64;
#endif

    settings.abi.pack = 8;

    settings.shm.mode =
        cw::server::shm_runtime_mode::fixed_direct;

    settings.shm.name =
        "CW.ServerEngineV4.ShmObjectsBenchmark";

    settings.shm.fixed_base_address =
        0x0000010000000000ull;

    settings.files.manifest =
        "project.manifest";

    settings.files.source_save =
        "source.bin";

    settings.files.database =
        "database.bin";

    settings.files.compiled =
        "compiled.bin";

    return settings;
}

[[nodiscard]] double ms(
    std::chrono::steady_clock::duration value) noexcept {

    return std::chrono::duration<
        double,
        std::milli>{
            value}.count();
}


[[nodiscard]] bool mapping_size(
    std::uint64_t logical_size,
    std::size_t page_size,
    std::size_t& output) noexcept {

    output = 0;

    if (page_size == 0) {
        return false;
    }

    const auto page =
        static_cast<std::uint64_t>(
            page_size);

    auto value =
        logical_size == 0
        ? page
        : logical_size;

    const auto remainder =
        value % page;

    if (remainder != 0) {
        const auto padding =
            page - remainder;

        if (value >
            (std::numeric_limits<
                std::uint64_t>::max)() -
                padding) {

            return false;
        }

        value += padding;
    }

    if (value >
        static_cast<std::uint64_t>(
            (std::numeric_limits<
                std::size_t>::max)())) {

        return false;
    }

    output =
        static_cast<std::size_t>(
            value);

    return output != 0;
}

struct comparison_counts final {
    std::uint64_t type_slots = 0;
    std::uint64_t member_slots = 0;
    std::uint64_t base_slots = 0;
    std::uint64_t object_slots = 0;
    std::uint64_t unconnected = 0;
};

[[nodiscard]] bool compare_layouts(
    const cw::server::compiled_project_view& project,
    const cw::server::runtime_layout& old_layout,
    const cw::server::shm_layout& new_layout,
    comparison_counts& counts) {

    counts = {};

    if (old_layout.size() !=
            new_layout.size() ||
        old_layout.alignment() !=
            new_layout.alignment() ||
        old_layout.target() !=
            new_layout.target()) {

        std::cerr
            << "Final layout mismatch:"
            << " old_size=" << old_layout.size()
            << " new_size=" << new_layout.size()
            << " old_alignment=" << old_layout.alignment()
            << " new_alignment=" << new_layout.alignment()
            << " old_target=" << static_cast<int>(old_layout.target())
            << " new_target=" << static_cast<int>(new_layout.target())
            << '\n';

        return false;
    }

    for (std::size_t index = 0;
         index < project.type_slot_count();
         ++index) {

        const auto handle =
            project.type_at(index);

        if (!handle) {
            continue;
        }

        cw::server::runtime_value_layout old_value;
        cw::server::shm_value_layout new_value;

        const auto old_valid =
            old_layout.type(
                handle,
                old_value);

        const auto new_valid =
            new_layout.type(
                handle,
                new_value);

        if (old_valid != new_valid ||
            (old_valid &&
             (old_value.size !=
                  new_value.size ||
              old_value.alignment !=
                  new_value.alignment))) {

            std::cerr
                << "Type layout mismatch:"
                << " slot=" << handle.value()
                << " old_valid=" << old_valid
                << " new_valid=" << new_valid
                << " old_size=" << old_value.size
                << " new_size=" << new_value.size
                << " old_alignment=" << old_value.alignment
                << " new_alignment=" << new_value.alignment
                << '\n';

            return false;
        }

        ++counts.type_slots;
    }

    for (std::size_t index = 0;
         index < project.member_count();
         ++index) {

        cw::server::record_offset old_offset = 0;
        cw::server::shm_record_offset new_offset = 0;

        const auto old_valid =
            old_layout.member_offset(
                index,
                old_offset);

        const auto new_valid =
            new_layout.member_offset(
                index,
                new_offset);

        if (old_valid != new_valid ||
            (old_valid &&
             old_offset != new_offset)) {

            std::cerr
                << "Member offset mismatch:"
                << " global=" << index
                << " old_valid=" << old_valid
                << " new_valid=" << new_valid
                << " old=" << old_offset
                << " new=" << new_offset
                << '\n';

            return false;
        }

        ++counts.member_slots;
    }

    for (std::size_t index = 0;
         index < project.base_count();
         ++index) {

        cw::server::record_offset old_offset = 0;
        cw::server::shm_record_offset new_offset = 0;

        const auto old_valid =
            old_layout.base_offset(
                index,
                old_offset);

        const auto new_valid =
            new_layout.base_offset(
                index,
                new_offset);

        if (old_valid != new_valid ||
            (old_valid &&
             old_offset != new_offset)) {

            std::cerr
                << "Base offset mismatch:"
                << " global=" << index
                << " old_valid=" << old_valid
                << " new_valid=" << new_valid
                << " old=" << old_offset
                << " new=" << new_offset
                << '\n';

            return false;
        }

        ++counts.base_slots;
    }

    for (std::size_t index = 0;
         index < project.object_slot_count();
         ++index) {

        const auto handle =
            project.object_at(index);

        if (!handle) {
            continue;
        }

        cw::server::runtime_offset old_offset = 0;
        cw::server::shm_offset new_offset = 0;

        const auto old_valid =
            old_layout.object_offset(
                handle,
                old_offset);

        const auto new_valid =
            new_layout.object_offset(
                handle,
                new_offset);

        if (old_valid != new_valid ||
            (old_valid &&
             old_offset != new_offset)) {

            std::cerr
                << "Object offset mismatch:"
                << " slot=" << handle.value()
                << " old_valid=" << old_valid
                << " new_valid=" << new_valid
                << " old=" << old_offset
                << " new=" << new_offset
                << '\n';

            return false;
        }

        ++counts.object_slots;
    }

    if (old_layout.unconnected_count() !=
        new_layout.unconnected_count()) {

        std::cerr
            << "Unconnected count mismatch:"
            << " old=" << old_layout.unconnected_count()
            << " new=" << new_layout.unconnected_count()
            << '\n';

        return false;
    }

    for (std::size_t index = 0;
         index < old_layout.unconnected_count();
         ++index) {

        const auto old_type =
            old_layout.unconnected_type(
                index);

        const auto new_type =
            new_layout.unconnected_type(
                index);

        if (old_type != new_type) {
            std::cerr
                << "Unconnected type order mismatch:"
                << " index=" << index
                << " old=" << old_type.value()
                << " new=" << new_type.value()
                << '\n';

            return false;
        }

        cw::server::runtime_offset old_offset = 0;
        cw::server::shm_offset new_offset = 0;

        if (!old_layout.unconnected_offset(
                old_type,
                old_offset) ||
            !new_layout.unconnected_offset(
                new_type,
                new_offset) ||
            old_offset != new_offset) {

            std::cerr
                << "Unconnected offset mismatch:"
                << " index=" << index
                << " type=" << old_type.value()
                << " old=" << old_offset
                << " new=" << new_offset
                << '\n';

            return false;
        }

        ++counts.unconnected;
    }

    return true;
}


void diagnose_object_construction_failure(
    const cw::server::compiled_project_view& project) {

    std::uint64_t object_counts[7]{};
    std::uint64_t member_counts[7]{};

    bool first_named_nonzero_printed = false;

    for (std::size_t index = 0;
         index < project.object_count();
         ++index) {

        const auto handle =
            project.object_at(index);

        cw::server::object_entry object;
        cw::server::construction_value construction;

        if (!handle ||
            !project.object(handle, object) ||
            !project.construction(handle, construction)) {

            std::cerr
                << "diagnostic_object_read_failed"
                << ",index=" << index
                << '\n';
            return;
        }

        const auto kind =
            static_cast<std::uint32_t>(
                construction.kind);

        if (kind < 7) {
            ++object_counts[kind];
        }

        if (!first_named_nonzero_printed &&
            object.type.kind() ==
                cw::server::type_ref_kind::named &&
            construction.kind !=
                cw::server::construction_kind::zero) {

            std::cerr
                << "first_named_object_nonzero"
                << ",object_slot=" << handle.value()
                << ",type_ref=" << object.type.value()
                << ",construction_kind=" << kind
                << ",operand=" << construction.operand
                << ",bits=" << construction.bits()
                << '\n';

            first_named_nonzero_printed = true;
        }
    }

    for (std::size_t index = 0;
         index < project.member_count();
         ++index) {

        cw::server::construction_value construction;

        if (!project.construction_at(
                index,
                construction)) {

            std::cerr
                << "diagnostic_member_construction_read_failed"
                << ",global_member=" << index
                << '\n';
            return;
        }

        const auto kind =
            static_cast<std::uint32_t>(
                construction.kind);

        if (kind < 7) {
            ++member_counts[kind];
        }
    }

    std::cerr
        << "object_construction_kinds"
        << ",zero=" << object_counts[0]
        << ",signed=" << object_counts[1]
        << ",unsigned=" << object_counts[2]
        << ",real=" << object_counts[3]
        << ",member_binding=" << object_counts[4]
        << ",object_binding=" << object_counts[5]
        << ",unsupported=" << object_counts[6]
        << '\n';

    std::cerr
        << "member_construction_kinds"
        << ",zero=" << member_counts[0]
        << ",signed=" << member_counts[1]
        << ",unsigned=" << member_counts[2]
        << ",real=" << member_counts[3]
        << ",member_binding=" << member_counts[4]
        << ",object_binding=" << member_counts[5]
        << ",unsupported=" << member_counts[6]
        << '\n';
}

void usage() {
    std::cerr
        << "Usage:\n"
        << "  ServerEngineV4ShmLayoutBenchmark"
           " <project.json> <expected-types>\n";
}

}

int main(
    int argc,
    char* argv[]) {

    if (argc != 3) {
        usage();
        return 2;
    }

    std::size_t expected_types = 0;

    if (!parse_count(
            argv[2],
            expected_types)) {

        std::cerr
            << "Invalid expected type count\n";

        return 2;
    }

    std::error_code path_error;

    const auto project_path =
        std::filesystem::absolute(
            std::filesystem::path{
                argv[1]},
            path_error)
            .lexically_normal();

    if (path_error) {
        std::cerr
            << "Cannot resolve Project path\n";

        return 2;
    }

    const auto settings =
        make_settings();

    cw::server::project_artifact_layout artifacts;

    if (cw::server::make_project_artifact_layout(
            project_path,
            settings.files,
            artifacts) !=
        cw::server::project_artifact_layout_result::
            success) {

        std::cerr
            << "Cannot construct artifact layout\n";

        return 2;
    }

    cw::server::read_only_file_mapping mapping;

    if (mapping.open(
            artifacts.compiled) !=
        cw::server::read_only_file_mapping_result::
            success) {

        std::cerr
            << "Cannot mmap compiled.bin\n";

        return 1;
    }

    cw::server::compiled_project_view compiled;

    if (compiled.bind(
            mapping.bytes()) !=
        cw::server::compiled_project_image_result::
            success) {

        std::cerr
            << "Cannot bind compiled.bin\n";

        return 1;
    }

    if (compiled.type_count() !=
        expected_types) {

        std::cerr
            << "Unexpected type count: "
            << compiled.type_count()
            << " != "
            << expected_types
            << '\n';

        return 3;
    }

    cw::server::runtime_layout old_layout;
    cw::server::shm_layout new_layout;

    const auto old_started =
        std::chrono::steady_clock::now();

    const auto old_result =
        cw::server::prepare_runtime_layout(
            compiled,
            settings.abi,
            old_layout);

    const auto old_finished =
        std::chrono::steady_clock::now();

    if (old_result !=
        cw::server::runtime_layout_result::
            success) {

        std::cerr
            << "Old Runtime layout failed: "
            << static_cast<int>(
                old_result)
            << '\n';

        return 1;
    }

    const auto new_started =
        std::chrono::steady_clock::now();

    const auto new_result =
        cw::server::prepare_shm_layout(
            compiled,
            settings.abi,
            new_layout);

    const auto new_finished =
        std::chrono::steady_clock::now();

    if (new_result !=
        cw::server::shm_layout_result::
            success) {

        std::cerr
            << "New SHM layout failed: "
            << static_cast<int>(
                new_result)
            << '\n';

        return 1;
    }

    comparison_counts counts;

    if (!compare_layouts(
            compiled,
            old_layout,
            new_layout,
            counts)) {

        return 4;
    }


    cw::server::shm_sparse_types sparse;
    cw::server::shm_sparse_prepare_telemetry sparse_prepare{};

    const auto sparse_prepare_started = std::chrono::steady_clock::now();
    const auto sparse_prepare_result = cw::server::prepare_shm_sparse_types(
        compiled, settings.abi, new_layout, sparse, &sparse_prepare);
    const auto sparse_prepare_finished = std::chrono::steady_clock::now();

    if (sparse_prepare_result != cw::server::shm_sparse_result::success) {
        std::cerr << "Sparse SHM prepare failed: "
                  << static_cast<int>(sparse_prepare_result) << '\n';
        return 6;
    }


    cw::server::shm_sparse_call_profile
        sparse_call_profile{};

    const auto sparse_call_profile_result =
        cw::server::profile_shm_sparse_calls(
            sparse,
            sparse_call_profile);

    if (sparse_call_profile_result !=
        cw::server::shm_sparse_result::success) {

        std::cerr
            << "Sparse SHM CALL profile failed: "
            << static_cast<int>(
                sparse_call_profile_result)
            << '\n';

        return 6;
    }

    if (sparse_call_profile.call_actions !=
        sparse_prepare.call_actions) {

        std::cerr
            << "Sparse SHM CALL action profile mismatch"
            << ",prepare="
            << sparse_prepare.call_actions
            << ",profile="
            << sparse_call_profile.call_actions
            << '\n';

        return 6;
    }


    cw::server::shm_type_area type_area;
    cw::server::shm_type_area_prepare_telemetry
        type_area_prepare{};

    const auto type_area_prepare_started =
        std::chrono::steady_clock::now();

    const auto type_area_prepare_result =
        cw::server::prepare_shm_type_area(
            compiled,
            settings.abi,
            new_layout,
            type_area,
            &type_area_prepare);

    const auto type_area_prepare_finished =
        std::chrono::steady_clock::now();

    if (type_area_prepare_result !=
        cw::server::shm_type_area_result::success) {

        std::cerr
            << "SHM Type Area prepare failed: "
            << static_cast<int>(
                type_area_prepare_result)
            << '\n';

        return 8;
    }

    if (type_area_prepare.type_apis !=
            type_area_prepare.reachable_type_apis ||
        type_area_prepare.prefinal_resident_bytes <
            type_area_prepare.resident_bytes ||
        type_area_prepare.reclaimed_bytes !=
            type_area_prepare.prefinal_resident_bytes -
            type_area_prepare.resident_bytes) {

        std::cerr
            << "SHM Type Area finalization invariant failed"
            << ",prefinal_resident="
            << type_area_prepare.prefinal_resident_bytes
            << ",final_resident="
            << type_area_prepare.resident_bytes
            << ",reclaimed="
            << type_area_prepare.reclaimed_bytes
            << ",reachable_apis="
            << type_area_prepare.reachable_type_apis
            << ",final_apis="
            << type_area_prepare.type_apis
            << '\n';

        return 8;
    }


    cw::server::shm_type_batch type_batch;
    cw::server::shm_type_batch_prepare_telemetry
        type_batch_prepare{};

    const auto type_batch_prepare_started =
        std::chrono::steady_clock::now();

    const auto type_batch_prepare_result =
        cw::server::prepare_shm_type_batch(
            compiled,
            settings.abi,
            new_layout,
            type_batch,
            &type_batch_prepare);

    const auto type_batch_prepare_finished =
        std::chrono::steady_clock::now();

    if (type_batch_prepare_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "SHM Type Batch prepare failed: "
            << static_cast<int>(
                type_batch_prepare_result)
            << '\n';

        return 9;
    }


    cw::server::shm_hybrid_profile hybrid_profile;

    const auto hybrid_profile_started =
        std::chrono::steady_clock::now();

    const auto hybrid_profile_result =
        cw::server::profile_shm_type_hybrid(
            type_batch,
            hybrid_profile);

    const auto hybrid_profile_finished =
        std::chrono::steady_clock::now();

    if (hybrid_profile_result !=
        cw::server::shm_hybrid_profile_result::success) {

        std::cerr
            << "SHM Hybrid profile failed: "
            << static_cast<int>(
                hybrid_profile_result)
            << '\n';

        return 10;
    }


    cw::server::shm_hybrid_04m hybrid_04m;
    cw::server::shm_hybrid_04m_prepare_telemetry
        hybrid_04m_prepare{};

    const auto hybrid_04m_prepare_started =
        std::chrono::steady_clock::now();

    const auto hybrid_04m_prepare_result =
        cw::server::prepare_shm_hybrid_04m(
            type_batch,
            hybrid_04m,
            &hybrid_04m_prepare);

    const auto hybrid_04m_prepare_finished =
        std::chrono::steady_clock::now();

    if (hybrid_04m_prepare_result !=
        cw::server::shm_hybrid_04m_result::success) {

        std::cerr
            << "SHM Hybrid 4M prepare failed: "
            << static_cast<int>(
                hybrid_04m_prepare_result)
            << '\n';

        return 10;
    }

    if (hybrid_04m_prepare.flat_payload_bytes >
            hybrid_04m_prepare.budget_bytes ||
        hybrid_04m_prepare.total_runtime_metadata_bytes !=
            type_batch_prepare.resident_bytes +
                hybrid_04m_prepare.resident_bytes) {

        std::cerr
            << "SHM Hybrid 4M prepare invariant failed"
            << ",budget="
            << hybrid_04m_prepare.budget_bytes
            << ",flat_payload="
            << hybrid_04m_prepare.flat_payload_bytes
            << ",base="
            << type_batch_prepare.resident_bytes
            << ",hybrid="
            << hybrid_04m_prepare.resident_bytes
            << ",total="
            << hybrid_04m_prepare.total_runtime_metadata_bytes
            << '\n';

        return 10;
    }


    cw::server::shm_type_batch type_inline08;
    cw::server::shm_type_batch_prepare_telemetry
        type_inline08_prepare{};

    const auto type_inline08_prepare_started =
        std::chrono::steady_clock::now();

    const auto type_inline08_prepare_result =
        cw::server::prepare_shm_type_batch_inline08(
            compiled,
            settings.abi,
            new_layout,
            type_inline08,
            &type_inline08_prepare);

    const auto type_inline08_prepare_finished =
        std::chrono::steady_clock::now();

    if (type_inline08_prepare_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "SHM Type Inline08 prepare failed: "
            << static_cast<int>(
                type_inline08_prepare_result)
            << '\n';

        return 10;
    }


    cw::server::shm_type_batch type_subtree08;
    cw::server::shm_type_batch_prepare_telemetry
        type_subtree08_prepare{};

    const auto type_subtree08_prepare_started =
        std::chrono::steady_clock::now();

    const auto type_subtree08_prepare_result =
        cw::server::prepare_shm_type_batch_subtree08(
            compiled,
            settings.abi,
            new_layout,
            type_subtree08,
            &type_subtree08_prepare);

    const auto type_subtree08_prepare_finished =
        std::chrono::steady_clock::now();

    if (type_subtree08_prepare_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "SUBTREE-INLINE-08 prepare failed: "
            << static_cast<int>(
                type_subtree08_prepare_result)
            << '\n';

        return 13;
    }


    cw::server::shm_type_batch type_inline16;
    cw::server::shm_type_batch type_inline32;
    cw::server::shm_type_batch type_inline64;

    cw::server::shm_type_batch_prepare_telemetry
        type_inline16_prepare{};
    cw::server::shm_type_batch_prepare_telemetry
        type_inline32_prepare{};
    cw::server::shm_type_batch_prepare_telemetry
        type_inline64_prepare{};

    const auto type_inline16_prepare_started =
        std::chrono::steady_clock::now();
    const auto type_inline16_prepare_result =
        cw::server::prepare_shm_type_batch_inline16(
            compiled,
            settings.abi,
            new_layout,
            type_inline16,
            &type_inline16_prepare);
    const auto type_inline16_prepare_finished =
        std::chrono::steady_clock::now();

    if (type_inline16_prepare_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "SHM Type Inline16 prepare failed: "
            << static_cast<int>(
                type_inline16_prepare_result)
            << '\n';

        return 14;
    }

    const auto type_inline32_prepare_started =
        std::chrono::steady_clock::now();
    const auto type_inline32_prepare_result =
        cw::server::prepare_shm_type_batch_inline32(
            compiled,
            settings.abi,
            new_layout,
            type_inline32,
            &type_inline32_prepare);
    const auto type_inline32_prepare_finished =
        std::chrono::steady_clock::now();

    if (type_inline32_prepare_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "SHM Type Inline32 prepare failed: "
            << static_cast<int>(
                type_inline32_prepare_result)
            << '\n';

        return 14;
    }

    const auto type_inline64_prepare_started =
        std::chrono::steady_clock::now();
    const auto type_inline64_prepare_result =
        cw::server::prepare_shm_type_batch_inline64(
            compiled,
            settings.abi,
            new_layout,
            type_inline64,
            &type_inline64_prepare);
    const auto type_inline64_prepare_finished =
        std::chrono::steady_clock::now();

    if (type_inline64_prepare_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "SHM Type Inline64 prepare failed: "
            << static_cast<int>(
                type_inline64_prepare_result)
            << '\n';

        return 14;
    }

    const auto page_size =
        cw::server::fixed_shared_memory::
            size_alignment();

    std::size_t mapped_size = 0;

    if (!mapping_size(
            new_layout.size(),
            page_size,
            mapped_size)) {

        std::cerr
            << "Cannot derive fixed SHM mapping size\n";

        return 5;
    }

    cw::server::fixed_shared_memory shm;

    const auto shm_created =
        shm.create(
            settings.shm.name,
            mapped_size,
            static_cast<std::uintptr_t>(
                settings.shm.fixed_base_address));

    if (shm_created !=
        cw::server::fixed_shared_memory_result::
            success) {

        std::cerr
            << "Cannot create fixed SHM: "
            << static_cast<int>(
                shm_created)
            << '\n';

        return 5;
    }

    cw::server::shm_materialization_telemetry
        materialization{};

    const auto logical_size =
        static_cast<std::size_t>(
            new_layout.size());

    auto logical_shm =
        shm.bytes().first(
            logical_size);

    const auto canonical_started =
        std::chrono::steady_clock::now();

    const auto canonical_result =
        cw::server::materialize_shm_canonical(
            compiled,
            settings.abi,
            new_layout,
            logical_shm,
            &materialization);

    const auto canonical_finished =
        std::chrono::steady_clock::now();

    if (canonical_result !=
        cw::server::shm_materialization_result::
            success) {

        std::cerr
            << "New SHM canonical materialization failed: "
            << static_cast<int>(
                canonical_result)
            << '\n';

        return 5;
    }

    const auto objects_started =
        std::chrono::steady_clock::now();

    const auto objects_result =
        cw::server::materialize_shm_objects(
            compiled,
            settings.abi,
            new_layout,
            logical_shm,
            &materialization);

    const auto objects_finished =
        std::chrono::steady_clock::now();

    if (objects_result !=
        cw::server::shm_materialization_result::
            success) {

        std::cerr
            << "New SHM object materialization failed: "
            << static_cast<int>(
                objects_result)
            << '\n';

        if (materialization.failure_object_slot != 0) {
            const auto failed_object =
                compiled.object_at(
                    materialization.failure_object_slot - 1);
            cw::server::object_entry failed_entry;
            if (failed_object &&
                compiled.object(failed_object, failed_entry)) {
                auto current = failed_entry.type;
                std::cerr
                    << "shm_failed_type_chain"
                    << ",object_slot=" << failed_object.value()
                    << ",root_type_ref=" << current.value();
                for (std::uint32_t depth = 0; depth < 16; ++depth) {
                    cw::server::derived_type_record derived;
                    if (!compiled.derived(current, derived)) {
                        break;
                    }
                    std::cerr
                        << ",d" << depth
                        << "_kind="
                        << static_cast<std::uint32_t>(derived.kind)
                        << ",d" << depth
                        << "_payload=" << derived.payload
                        << ",d" << depth
                        << "_child=" << derived.child.value();
                    current = derived.child;
                }
                std::cerr << '\n';
            }
        }

        std::cerr
            << "shm_failure_stage="
            << materialization.failure_stage
            << ",object_slot="
            << materialization.failure_object_slot
            << ",type_slot="
            << materialization.failure_type_slot
            << ",member_local="
            << materialization.failure_member_local
            << ",member_global="
            << materialization.failure_member_global
            << ",member_type="
            << materialization.failure_member_type
            << ",construction_kind="
            << materialization.failure_construction_kind
            << ",construction_operand="
            << materialization.failure_construction_operand
            << '\n';

        diagnose_object_construction_failure(
            compiled);

        return 5;
    }

    shm.reset();

    const auto sparse_shm_created = shm.create(
        settings.shm.name, mapped_size,
        static_cast<std::uintptr_t>(settings.shm.fixed_base_address));

    if (sparse_shm_created != cw::server::fixed_shared_memory_result::success) {
        std::cerr << "Cannot recreate fixed SHM for sparse path: "
                  << static_cast<int>(sparse_shm_created) << '\n';
        return 6;
    }

    logical_shm = shm.bytes().first(logical_size);

    cw::server::shm_sparse_execute_telemetry sparse_execute{};

    const auto sparse_canonical_started = std::chrono::steady_clock::now();
    const auto sparse_canonical_result = cw::server::materialize_shm_sparse_canonical(
        sparse, settings.abi, new_layout, logical_shm, &sparse_execute);
    const auto sparse_canonical_finished = std::chrono::steady_clock::now();

    if (sparse_canonical_result != cw::server::shm_sparse_result::success) {
        std::cerr << "Sparse SHM canonical failed: "
                  << static_cast<int>(sparse_canonical_result) << '\n';
        return 6;
    }

    const auto sparse_objects_started = std::chrono::steady_clock::now();
    const auto sparse_objects_result = cw::server::materialize_shm_sparse_objects(
        sparse, settings.abi, new_layout, logical_shm, &sparse_execute);
    const auto sparse_objects_finished = std::chrono::steady_clock::now();

    if (sparse_objects_result != cw::server::shm_sparse_result::success) {
        std::cerr << "Sparse SHM objects failed: "
                  << static_cast<int>(sparse_objects_result) << '\n';
        return 6;
    }

    const auto expected_reference_writes =
        materialization.canonical_references + materialization.references;

    if (sparse_execute.reference_writes != expected_reference_writes ||
        sparse_execute.store_writes != materialization.scalar_writes) {
        std::cerr << "Sparse SHM write-count mismatch"
                  << ",reference_expected=" << expected_reference_writes
                  << ",reference_actual=" << sparse_execute.reference_writes
                  << ",store_expected=" << materialization.scalar_writes
                  << ",store_actual=" << sparse_execute.store_writes << '\n';
        return 6;
    }

    if (sparse_prepare.action_size != 16) {
        std::cerr
            << "Sparse SHM compact action size mismatch: "
            << sparse_prepare.action_size
            << '\n';

        return 6;
    }

    // SHM-SPARSE-PRETOUCH-01:
    // Recreate a zeroed FIXED_DIRECT mapping, fault every logical page
    // before sparse execution, and time pretouch separately.
    shm.reset();

    const auto pretouched_shm_created =
        shm.create(
            settings.shm.name,
            mapped_size,
            static_cast<std::uintptr_t>(
                settings.shm.fixed_base_address));

    if (pretouched_shm_created !=
        cw::server::fixed_shared_memory_result::success) {

        std::cerr
            << "Cannot recreate fixed SHM for pretouched sparse path: "
            << static_cast<int>(pretouched_shm_created)
            << '\n';

        return 7;
    }

    logical_shm = shm.bytes().first(logical_size);

    if (page_size == 0) {
        std::cerr << "Invalid fixed SHM page size" << '\n';
        return 7;
    }

    const auto sparse_pretouch_started =
        std::chrono::steady_clock::now();

    for (std::size_t offset = 0;
         offset < logical_size;
         offset += page_size) {

        auto* page =
            reinterpret_cast<volatile std::uint8_t*>(
                logical_shm.data() + offset);

        *page = 0;
    }

    const auto sparse_pretouch_finished =
        std::chrono::steady_clock::now();

    cw::server::shm_sparse_execute_telemetry
        sparse_pretouched_execute{};

    const auto sparse_pretouched_canonical_started =
        std::chrono::steady_clock::now();

    const auto sparse_pretouched_canonical_result =
        cw::server::materialize_shm_sparse_canonical(
            sparse, settings.abi, new_layout, logical_shm,
            &sparse_pretouched_execute);

    const auto sparse_pretouched_canonical_finished =
        std::chrono::steady_clock::now();

    if (sparse_pretouched_canonical_result !=
        cw::server::shm_sparse_result::success) {

        std::cerr << "Pretouched sparse SHM canonical failed: "
                  << static_cast<int>(sparse_pretouched_canonical_result)
                  << '\n';
        return 7;
    }

    const auto sparse_pretouched_objects_started =
        std::chrono::steady_clock::now();

    const auto sparse_pretouched_objects_result =
        cw::server::materialize_shm_sparse_objects(
            sparse, settings.abi, new_layout, logical_shm,
            &sparse_pretouched_execute);

    const auto sparse_pretouched_objects_finished =
        std::chrono::steady_clock::now();

    if (sparse_pretouched_objects_result !=
        cw::server::shm_sparse_result::success) {

        std::cerr << "Pretouched sparse SHM objects failed: "
                  << static_cast<int>(sparse_pretouched_objects_result)
                  << '\n';
        return 7;
    }

    if (sparse_pretouched_execute.reference_writes !=
            expected_reference_writes ||
        sparse_pretouched_execute.store_writes !=
            materialization.scalar_writes ||
        sparse_pretouched_execute.action_visits !=
            sparse_execute.action_visits ||
        sparse_call_profile.call_visits !=
            sparse_execute.call_visits ||
        sparse_call_profile.call_visits !=
            sparse_pretouched_execute.call_visits) {

        std::cerr
            << "Pretouched sparse SHM execution mismatch"
            << ",reference_expected=" << expected_reference_writes
            << ",reference_actual=" << sparse_pretouched_execute.reference_writes
            << ",store_expected=" << materialization.scalar_writes
            << ",store_actual=" << sparse_pretouched_execute.store_writes
            << ",action_visits_expected=" << sparse_execute.action_visits
            << ",action_visits_actual=" << sparse_pretouched_execute.action_visits
            << ",call_visits_profile=" << sparse_call_profile.call_visits
            << ",call_visits_fresh=" << sparse_execute.call_visits
            << ",call_visits_pretouched=" << sparse_pretouched_execute.call_visits
            << '\n';

        return 7;
    }


    // SHM-TYPE-AREA-01:
    // Measure the dense Type Area on a separately recreated, pretouched
    // FIXED_DIRECT mapping. The timed executor has no G/project access and
    // receives no identity_ref values.
    shm.reset();

    const auto type_area_shm_created =
        shm.create(
            settings.shm.name,
            mapped_size,
            static_cast<std::uintptr_t>(
                settings.shm.fixed_base_address));

    if (type_area_shm_created !=
        cw::server::fixed_shared_memory_result::success) {

        std::cerr
            << "Cannot recreate fixed SHM for Type Area path: "
            << static_cast<int>(
                type_area_shm_created)
            << '\n';

        return 8;
    }

    logical_shm =
        shm.bytes().first(
            logical_size);

    const auto type_area_pretouch_started =
        std::chrono::steady_clock::now();

    for (std::size_t offset = 0;
         offset < logical_size;
         offset += page_size) {

        auto* page =
            reinterpret_cast<volatile std::uint8_t*>(
                logical_shm.data() + offset);

        *page = 0;
    }

    const auto type_area_pretouch_finished =
        std::chrono::steady_clock::now();

    cw::server::shm_type_area_execute_telemetry
        type_area_execute{};

    const auto type_area_canonical_started =
        std::chrono::steady_clock::now();

    const auto type_area_canonical_result =
        cw::server::materialize_shm_type_area_canonical(
            type_area,
            settings.abi,
            new_layout,
            logical_shm,
            &type_area_execute);

    const auto type_area_canonical_finished =
        std::chrono::steady_clock::now();

    if (type_area_canonical_result !=
        cw::server::shm_type_area_result::success) {

        std::cerr
            << "SHM Type Area canonical failed: "
            << static_cast<int>(
                type_area_canonical_result)
            << '\n';

        return 8;
    }

    const auto type_area_objects_started =
        std::chrono::steady_clock::now();

    const auto type_area_objects_result =
        cw::server::materialize_shm_type_area_objects(
            type_area,
            settings.abi,
            new_layout,
            logical_shm,
            &type_area_execute);

    const auto type_area_objects_finished =
        std::chrono::steady_clock::now();

    if (type_area_objects_result !=
        cw::server::shm_type_area_result::success) {

        std::cerr
            << "SHM Type Area objects failed: "
            << static_cast<int>(
                type_area_objects_result)
            << '\n';

        return 8;
    }

    const auto expected_absolute_references =
        materialization.canonical_references +
        materialization.reference_unconnected;

    if (type_area_execute.reference_writes !=
            expected_reference_writes ||
        type_area_execute.relative_reference_writes !=
            materialization.reference_member_bindings ||
        type_area_execute.absolute_reference_writes !=
            expected_absolute_references ||
        type_area_execute.object_reference_writes !=
            materialization.reference_object_bindings ||
        type_area_execute.store_writes !=
            materialization.scalar_writes ||
        type_area_execute.objects !=
            materialization.objects) {

        std::cerr
            << "SHM Type Area physical write mismatch"
            << ",reference_expected="
            << expected_reference_writes
            << ",reference_actual="
            << type_area_execute.reference_writes
            << ",relative_expected="
            << materialization.reference_member_bindings
            << ",relative_actual="
            << type_area_execute.relative_reference_writes
            << ",absolute_expected="
            << expected_absolute_references
            << ",absolute_actual="
            << type_area_execute.absolute_reference_writes
            << ",object_expected="
            << materialization.reference_object_bindings
            << ",object_actual="
            << type_area_execute.object_reference_writes
            << ",store_expected="
            << materialization.scalar_writes
            << ",store_actual="
            << type_area_execute.store_writes
            << ",objects_expected="
            << materialization.objects
            << ",objects_actual="
            << type_area_execute.objects
            << '\n';

        return 8;
    }


    // SHM-TYPE-BATCH-01:
    // Local Type APIs are retained once. Root objects are grouped by API and
    // structural child traversal occurs once per fixed-size object batch.
    shm.reset();

    const auto type_batch_shm_created =
        shm.create(
            settings.shm.name,
            mapped_size,
            static_cast<std::uintptr_t>(
                settings.shm.fixed_base_address));

    if (type_batch_shm_created !=
        cw::server::fixed_shared_memory_result::
            success) {

        std::cerr
            << "Cannot recreate fixed SHM for Type Batch path: "
            << static_cast<int>(
                type_batch_shm_created)
            << '\n';

        return 9;
    }

    logical_shm =
        shm.bytes().first(
            logical_size);

    const auto type_batch_pretouch_started =
        std::chrono::steady_clock::now();

    for (std::size_t offset = 0;
         offset < logical_size;
         offset += page_size) {

        auto* page =
            reinterpret_cast<volatile std::uint8_t*>(
                logical_shm.data() + offset);

        *page = 0;
    }

    const auto type_batch_pretouch_finished =
        std::chrono::steady_clock::now();

    cw::server::shm_type_batch_execute_telemetry
        type_batch_execute{};

    const auto type_batch_canonical_started =
        std::chrono::steady_clock::now();

    const auto type_batch_canonical_result =
        cw::server::materialize_shm_type_batch_canonical(
            type_batch,
            settings.abi,
            new_layout,
            logical_shm,
            &type_batch_execute);

    const auto type_batch_canonical_finished =
        std::chrono::steady_clock::now();

    if (type_batch_canonical_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "SHM Type Batch canonical failed: "
            << static_cast<int>(
                type_batch_canonical_result)
            << '\n';

        return 9;
    }

    const auto type_batch_objects_started =
        std::chrono::steady_clock::now();

    const auto type_batch_objects_result =
        cw::server::materialize_shm_type_batch_objects(
            type_batch,
            settings.abi,
            new_layout,
            logical_shm,
            &type_batch_execute);

    const auto type_batch_objects_finished =
        std::chrono::steady_clock::now();

    if (type_batch_objects_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "SHM Type Batch objects failed: "
            << static_cast<int>(
                type_batch_objects_result)
            << '\n';

        return 9;
    }

    if (type_batch_execute.reference_writes !=
            expected_reference_writes ||
        type_batch_execute.relative_reference_writes !=
            materialization.reference_member_bindings ||
        type_batch_execute.absolute_reference_writes !=
            expected_absolute_references ||
        type_batch_execute.object_reference_writes !=
            materialization.reference_object_bindings ||
        type_batch_execute.store_writes !=
            materialization.scalar_writes ||
        type_batch_execute.objects !=
            materialization.objects) {

        std::cerr
            << "SHM Type Batch physical write mismatch"
            << ",reference_expected="
            << expected_reference_writes
            << ",reference_actual="
            << type_batch_execute.reference_writes
            << ",relative_expected="
            << materialization.reference_member_bindings
            << ",relative_actual="
            << type_batch_execute.relative_reference_writes
            << ",absolute_expected="
            << expected_absolute_references
            << ",absolute_actual="
            << type_batch_execute.absolute_reference_writes
            << ",object_expected="
            << materialization.reference_object_bindings
            << ",object_actual="
            << type_batch_execute.object_reference_writes
            << ",store_expected="
            << materialization.scalar_writes
            << ",store_actual="
            << type_batch_execute.store_writes
            << ",objects_expected="
            << materialization.objects
            << ",objects_actual="
            << type_batch_execute.objects
            << '\n';

        return 9;
    }



    struct hybrid_budget_measurement final {
        std::uint64_t budget_bytes = 0;
        double prepare_ms = 0.0;
        double pretouch_ms = 0.0;
        double canonical_ms = 0.0;
        double objects_ms = 0.0;
        cw::server::shm_hybrid_04m_prepare_telemetry prepare{};
        cw::server::shm_hybrid_04m_execute_telemetry execute{};
    };

    const auto run_hybrid_budget =
        [&](
            std::uint64_t budget_bytes,
            hybrid_budget_measurement& measurement)
            -> bool {

        measurement = {};
        measurement.budget_bytes = budget_bytes;

        cw::server::shm_hybrid_04m hybrid;

        const auto prepare_started =
            std::chrono::steady_clock::now();

        const auto prepare_result =
            cw::server::prepare_shm_hybrid_budget(
                type_batch,
                budget_bytes,
                hybrid,
                &measurement.prepare);

        const auto prepare_finished =
            std::chrono::steady_clock::now();

        measurement.prepare_ms =
            ms(prepare_finished - prepare_started);

        if (prepare_result !=
            cw::server::shm_hybrid_04m_result::success) {

            std::cerr
                << "SHM Hybrid budget prepare failed"
                << ",budget=" << budget_bytes
                << ",result="
                << static_cast<int>(prepare_result)
                << '\n';

            return false;
        }

        if (measurement.prepare.budget_bytes !=
                budget_bytes ||
            measurement.prepare.flat_payload_bytes >
                budget_bytes ||
            measurement.prepare.total_runtime_metadata_bytes !=
                type_batch_prepare.resident_bytes +
                    measurement.prepare.resident_bytes) {

            std::cerr
                << "SHM Hybrid budget prepare invariant failed"
                << ",budget=" << budget_bytes
                << ",reported_budget="
                << measurement.prepare.budget_bytes
                << ",payload="
                << measurement.prepare.flat_payload_bytes
                << ",base="
                << type_batch_prepare.resident_bytes
                << ",hybrid="
                << measurement.prepare.resident_bytes
                << ",total="
                << measurement.prepare.total_runtime_metadata_bytes
                << '\n';

            return false;
        }

        shm.reset();

        const auto shm_created =
            shm.create(
                settings.shm.name,
                mapped_size,
                static_cast<std::uintptr_t>(
                    settings.shm.fixed_base_address));

        if (shm_created !=
            cw::server::fixed_shared_memory_result::success) {

            std::cerr
                << "Cannot recreate fixed SHM for Hybrid budget"
                << ",budget=" << budget_bytes
                << ",result="
                << static_cast<int>(shm_created)
                << '\n';

            return false;
        }

        logical_shm =
            shm.bytes().first(
                logical_size);

        const auto pretouch_started =
            std::chrono::steady_clock::now();

        for (std::size_t offset = 0;
             offset < logical_size;
             offset += page_size) {

            auto* page =
                reinterpret_cast<volatile std::uint8_t*>(
                    logical_shm.data() + offset);

            *page = 0;
        }

        const auto pretouch_finished =
            std::chrono::steady_clock::now();

        measurement.pretouch_ms =
            ms(pretouch_finished - pretouch_started);

        cw::server::shm_type_batch_execute_telemetry
            canonical_execute{};

        const auto canonical_started =
            std::chrono::steady_clock::now();

        const auto canonical_result =
            cw::server::materialize_shm_type_batch_canonical(
                type_batch,
                settings.abi,
                new_layout,
                logical_shm,
                &canonical_execute);

        const auto canonical_finished =
            std::chrono::steady_clock::now();

        measurement.canonical_ms =
            ms(canonical_finished - canonical_started);

        if (canonical_result !=
            cw::server::shm_type_batch_result::success) {

            std::cerr
                << "SHM Hybrid budget canonical failed"
                << ",budget=" << budget_bytes
                << ",result="
                << static_cast<int>(canonical_result)
                << '\n';

            return false;
        }

        const auto objects_started =
            std::chrono::steady_clock::now();

        const auto objects_result =
            cw::server::materialize_shm_hybrid_04m_objects(
                type_batch,
                hybrid,
                settings.abi,
                new_layout,
                logical_shm,
                &measurement.execute);

        const auto objects_finished =
            std::chrono::steady_clock::now();

        measurement.objects_ms =
            ms(objects_finished - objects_started);

        if (objects_result !=
            cw::server::shm_hybrid_04m_result::success) {

            std::cerr
                << "SHM Hybrid budget objects failed"
                << ",budget=" << budget_bytes
                << ",result="
                << static_cast<int>(objects_result)
                << '\n';

            return false;
        }

        if (canonical_execute.reference_writes !=
                materialization.canonical_references ||
            measurement.execute.reference_writes !=
                materialization.references ||
            measurement.execute.relative_reference_writes !=
                materialization.reference_member_bindings ||
            measurement.execute.absolute_reference_writes !=
                materialization.reference_unconnected ||
            measurement.execute.object_reference_writes !=
                materialization.reference_object_bindings ||
            measurement.execute.store_writes !=
                materialization.scalar_writes ||
            measurement.execute.objects !=
                materialization.objects ||
            measurement.execute.hot_object_roots !=
                measurement.prepare.hot_object_roots ||
            measurement.execute.cold_object_roots !=
                measurement.prepare.cold_object_roots) {

            std::cerr
                << "SHM Hybrid budget physical write mismatch"
                << ",budget=" << budget_bytes
                << ",canonical_expected="
                << materialization.canonical_references
                << ",canonical_actual="
                << canonical_execute.reference_writes
                << ",object_refs_expected="
                << materialization.references
                << ",object_refs_actual="
                << measurement.execute.reference_writes
                << ",relative_expected="
                << materialization.reference_member_bindings
                << ",relative_actual="
                << measurement.execute.relative_reference_writes
                << ",absolute_expected="
                << materialization.reference_unconnected
                << ",absolute_actual="
                << measurement.execute.absolute_reference_writes
                << ",object_binding_expected="
                << materialization.reference_object_bindings
                << ",object_binding_actual="
                << measurement.execute.object_reference_writes
                << ",stores_expected="
                << materialization.scalar_writes
                << ",stores_actual="
                << measurement.execute.store_writes
                << ",objects_expected="
                << materialization.objects
                << ",objects_actual="
                << measurement.execute.objects
                << '\n';

            return false;
        }

        return true;
    };

    hybrid_budget_measurement hybrid_sweep_01m;
    hybrid_budget_measurement hybrid_sweep_02m;
    hybrid_budget_measurement hybrid_sweep_08m;
    hybrid_budget_measurement hybrid_sweep_16m;
    hybrid_budget_measurement hybrid_sweep_32m;

    if (!run_hybrid_budget(
            1ull * 1024ull * 1024ull,
            hybrid_sweep_01m) ||
        !run_hybrid_budget(
            2ull * 1024ull * 1024ull,
            hybrid_sweep_02m) ||
        !run_hybrid_budget(
            8ull * 1024ull * 1024ull,
            hybrid_sweep_08m) ||
        !run_hybrid_budget(
            16ull * 1024ull * 1024ull,
            hybrid_sweep_16m) ||
        !run_hybrid_budget(
            32ull * 1024ull * 1024ull,
            hybrid_sweep_32m)) {

        return 11;
    }

    // SHM-HYBRID-04M-01:
    // Canonical remains compact/local. Hot selected object roots use
    // object-major flat APIs; cold roots retain local batched execution.
    shm.reset();

    const auto hybrid_04m_shm_created =
        shm.create(
            settings.shm.name,
            mapped_size,
            static_cast<std::uintptr_t>(
                settings.shm.fixed_base_address));

    if (hybrid_04m_shm_created !=
        cw::server::fixed_shared_memory_result::
            success) {

        std::cerr
            << "Cannot recreate fixed SHM for Hybrid 4M path: "
            << static_cast<int>(
                hybrid_04m_shm_created)
            << '\n';

        return 10;
    }

    logical_shm =
        shm.bytes().first(
            logical_size);

    const auto hybrid_04m_pretouch_started =
        std::chrono::steady_clock::now();

    for (std::size_t offset = 0;
         offset < logical_size;
         offset += page_size) {

        auto* page =
            reinterpret_cast<volatile std::uint8_t*>(
                logical_shm.data() + offset);

        *page = 0;
    }

    const auto hybrid_04m_pretouch_finished =
        std::chrono::steady_clock::now();

    cw::server::shm_type_batch_execute_telemetry
        hybrid_04m_canonical_execute{};

    const auto hybrid_04m_canonical_started =
        std::chrono::steady_clock::now();

    const auto hybrid_04m_canonical_result =
        cw::server::materialize_shm_type_batch_canonical(
            type_batch,
            settings.abi,
            new_layout,
            logical_shm,
            &hybrid_04m_canonical_execute);

    const auto hybrid_04m_canonical_finished =
        std::chrono::steady_clock::now();

    if (hybrid_04m_canonical_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "SHM Hybrid 4M canonical failed: "
            << static_cast<int>(
                hybrid_04m_canonical_result)
            << '\n';

        return 10;
    }

    cw::server::shm_hybrid_04m_execute_telemetry
        hybrid_04m_execute{};

    const auto hybrid_04m_objects_started =
        std::chrono::steady_clock::now();

    const auto hybrid_04m_objects_result =
        cw::server::materialize_shm_hybrid_04m_objects(
            type_batch,
            hybrid_04m,
            settings.abi,
            new_layout,
            logical_shm,
            &hybrid_04m_execute);

    const auto hybrid_04m_objects_finished =
        std::chrono::steady_clock::now();

    if (hybrid_04m_objects_result !=
        cw::server::shm_hybrid_04m_result::success) {

        std::cerr
            << "SHM Hybrid 4M objects failed: "
            << static_cast<int>(
                hybrid_04m_objects_result)
            << '\n';

        return 10;
    }

    if (hybrid_04m_canonical_execute.reference_writes !=
            materialization.canonical_references ||
        hybrid_04m_execute.reference_writes !=
            materialization.references ||
        hybrid_04m_execute.relative_reference_writes !=
            materialization.reference_member_bindings ||
        hybrid_04m_execute.absolute_reference_writes !=
            materialization.reference_unconnected ||
        hybrid_04m_execute.object_reference_writes !=
            materialization.reference_object_bindings ||
        hybrid_04m_execute.store_writes !=
            materialization.scalar_writes ||
        hybrid_04m_execute.objects !=
            materialization.objects ||
        hybrid_04m_execute.hot_object_roots !=
            hybrid_04m_prepare.hot_object_roots ||
        hybrid_04m_execute.cold_object_roots !=
            hybrid_04m_prepare.cold_object_roots) {

        std::cerr
            << "SHM Hybrid 4M physical write mismatch"
            << ",canonical_expected="
            << materialization.canonical_references
            << ",canonical_actual="
            << hybrid_04m_canonical_execute.reference_writes
            << ",object_refs_expected="
            << materialization.references
            << ",object_refs_actual="
            << hybrid_04m_execute.reference_writes
            << ",relative_expected="
            << materialization.reference_member_bindings
            << ",relative_actual="
            << hybrid_04m_execute.relative_reference_writes
            << ",absolute_expected="
            << materialization.reference_unconnected
            << ",absolute_actual="
            << hybrid_04m_execute.absolute_reference_writes
            << ",object_binding_expected="
            << materialization.reference_object_bindings
            << ",object_binding_actual="
            << hybrid_04m_execute.object_reference_writes
            << ",stores_expected="
            << materialization.scalar_writes
            << ",stores_actual="
            << hybrid_04m_execute.store_writes
            << ",objects_expected="
            << materialization.objects
            << ",objects_actual="
            << hybrid_04m_execute.objects
            << ",hot_roots_expected="
            << hybrid_04m_prepare.hot_object_roots
            << ",hot_roots_actual="
            << hybrid_04m_execute.hot_object_roots
            << ",cold_roots_expected="
            << hybrid_04m_prepare.cold_object_roots
            << ",cold_roots_actual="
            << hybrid_04m_execute.cold_object_roots
            << '\n';

        return 10;
    }


    // SHM-TYPE-INLINE-08-01:
    // Same Type Batch executor, separately prepared image with small leaf
    // child APIs fused bottom-up during prepare.
    shm.reset();

    const auto type_inline08_shm_created =
        shm.create(
            settings.shm.name,
            mapped_size,
            static_cast<std::uintptr_t>(
                settings.shm.fixed_base_address));

    if (type_inline08_shm_created !=
        cw::server::fixed_shared_memory_result::
            success) {

        std::cerr
            << "Cannot recreate fixed SHM for Type Inline08 path: "
            << static_cast<int>(
                type_inline08_shm_created)
            << '\n';

        return 10;
    }

    logical_shm =
        shm.bytes().first(
            logical_size);

    const auto type_inline08_pretouch_started =
        std::chrono::steady_clock::now();

    for (std::size_t offset = 0;
         offset < logical_size;
         offset += page_size) {

        auto* page =
            reinterpret_cast<volatile std::uint8_t*>(
                logical_shm.data() + offset);

        *page = 0;
    }

    const auto type_inline08_pretouch_finished =
        std::chrono::steady_clock::now();

    cw::server::shm_type_batch_execute_telemetry
        type_inline08_execute{};

    const auto type_inline08_canonical_started =
        std::chrono::steady_clock::now();

    const auto type_inline08_canonical_result =
        cw::server::materialize_shm_type_batch_canonical(
            type_inline08,
            settings.abi,
            new_layout,
            logical_shm,
            &type_inline08_execute);

    const auto type_inline08_canonical_finished =
        std::chrono::steady_clock::now();

    if (type_inline08_canonical_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "SHM Type Inline08 canonical failed: "
            << static_cast<int>(
                type_inline08_canonical_result)
            << '\n';

        return 10;
    }

    const auto type_inline08_objects_started =
        std::chrono::steady_clock::now();

    const auto type_inline08_objects_result =
        cw::server::materialize_shm_type_batch_objects(
            type_inline08,
            settings.abi,
            new_layout,
            logical_shm,
            &type_inline08_execute);

    const auto type_inline08_objects_finished =
        std::chrono::steady_clock::now();

    if (type_inline08_objects_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "SHM Type Inline08 objects failed: "
            << static_cast<int>(
                type_inline08_objects_result)
            << '\n';

        return 10;
    }

    if (type_inline08_execute.reference_writes !=
            expected_reference_writes ||
        type_inline08_execute.relative_reference_writes !=
            materialization.reference_member_bindings ||
        type_inline08_execute.absolute_reference_writes !=
            expected_absolute_references ||
        type_inline08_execute.object_reference_writes !=
            materialization.reference_object_bindings ||
        type_inline08_execute.store_writes !=
            materialization.scalar_writes ||
        type_inline08_execute.objects !=
            materialization.objects) {

        std::cerr
            << "SHM Type Inline08 physical write mismatch"
            << ",reference_expected="
            << expected_reference_writes
            << ",reference_actual="
            << type_inline08_execute.reference_writes
            << ",relative_expected="
            << materialization.reference_member_bindings
            << ",relative_actual="
            << type_inline08_execute.relative_reference_writes
            << ",absolute_expected="
            << expected_absolute_references
            << ",absolute_actual="
            << type_inline08_execute.absolute_reference_writes
            << ",object_expected="
            << materialization.reference_object_bindings
            << ",object_actual="
            << type_inline08_execute.object_reference_writes
            << ",store_expected="
            << materialization.scalar_writes
            << ",store_actual="
            << type_inline08_execute.store_writes
            << ",objects_expected="
            << materialization.objects
            << ",objects_actual="
            << type_inline08_execute.objects
            << '\n';

        return 10;
    }


    // SHM-TYPE-INLINE-08-OBJECT-MAJOR-01:
    // Same INLINE-08 physical image; only object execution order changes.
    shm.reset();

    const auto type_inline08_object_major_shm_created =
        shm.create(
            settings.shm.name,
            mapped_size,
            static_cast<std::uintptr_t>(
                settings.shm.fixed_base_address));

    if (type_inline08_object_major_shm_created !=
        cw::server::fixed_shared_memory_result::
            success) {

        std::cerr
            << "Cannot recreate fixed SHM for INLINE-08 object-major path: "
            << static_cast<int>(
                type_inline08_object_major_shm_created)
            << '\n';

        return 12;
    }

    logical_shm =
        shm.bytes().first(
            logical_size);

    const auto type_inline08_object_major_pretouch_started =
        std::chrono::steady_clock::now();

    for (std::size_t offset = 0;
         offset < logical_size;
         offset += page_size) {

        auto* page =
            reinterpret_cast<volatile std::uint8_t*>(
                logical_shm.data() + offset);

        *page = 0;
    }

    const auto type_inline08_object_major_pretouch_finished =
        std::chrono::steady_clock::now();

    cw::server::shm_type_batch_execute_telemetry
        type_inline08_object_major_execute{};

    const auto type_inline08_object_major_canonical_started =
        std::chrono::steady_clock::now();

    const auto type_inline08_object_major_canonical_result =
        cw::server::materialize_shm_type_batch_canonical(
            type_inline08,
            settings.abi,
            new_layout,
            logical_shm,
            &type_inline08_object_major_execute);

    const auto type_inline08_object_major_canonical_finished =
        std::chrono::steady_clock::now();

    if (type_inline08_object_major_canonical_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "INLINE-08 object-major canonical failed: "
            << static_cast<int>(
                type_inline08_object_major_canonical_result)
            << '\n';

        return 12;
    }

    const auto type_inline08_object_major_objects_started =
        std::chrono::steady_clock::now();

    const auto type_inline08_object_major_objects_result =
        cw::server::
            materialize_shm_type_batch_objects_object_major(
                type_inline08,
                settings.abi,
                new_layout,
                logical_shm,
                &type_inline08_object_major_execute);

    const auto type_inline08_object_major_objects_finished =
        std::chrono::steady_clock::now();

    if (type_inline08_object_major_objects_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "INLINE-08 object-major objects failed: "
            << static_cast<int>(
                type_inline08_object_major_objects_result)
            << '\n';

        return 12;
    }

    if (type_inline08_object_major_execute.reference_writes !=
            expected_reference_writes ||
        type_inline08_object_major_execute.relative_reference_writes !=
            materialization.reference_member_bindings ||
        type_inline08_object_major_execute.absolute_reference_writes !=
            expected_absolute_references ||
        type_inline08_object_major_execute.object_reference_writes !=
            materialization.reference_object_bindings ||
        type_inline08_object_major_execute.store_writes !=
            materialization.scalar_writes ||
        type_inline08_object_major_execute.objects !=
            materialization.objects) {

        std::cerr
            << "INLINE-08 object-major physical write mismatch"
            << ",reference_expected="
            << expected_reference_writes
            << ",reference_actual="
            << type_inline08_object_major_execute.reference_writes
            << ",relative_expected="
            << materialization.reference_member_bindings
            << ",relative_actual="
            << type_inline08_object_major_execute.relative_reference_writes
            << ",absolute_expected="
            << expected_absolute_references
            << ",absolute_actual="
            << type_inline08_object_major_execute.absolute_reference_writes
            << ",object_expected="
            << materialization.reference_object_bindings
            << ",object_actual="
            << type_inline08_object_major_execute.object_reference_writes
            << ",store_expected="
            << materialization.scalar_writes
            << ",store_actual="
            << type_inline08_object_major_execute.store_writes
            << ",objects_expected="
            << materialization.objects
            << ",objects_actual="
            << type_inline08_object_major_execute.objects
            << '\n';

        return 12;
    }


    shm.reset();

    const auto type_subtree08_shm_created =
        shm.create(
            settings.shm.name,
            mapped_size,
            static_cast<std::uintptr_t>(
                settings.shm.fixed_base_address));

    if (type_subtree08_shm_created !=
        cw::server::fixed_shared_memory_result::success) {

        std::cerr
            << "Cannot recreate fixed SHM for SUBTREE-INLINE-08: "
            << static_cast<int>(
                type_subtree08_shm_created)
            << '\n';

        return 13;
    }

    logical_shm =
        shm.bytes().first(
            logical_size);

    const auto type_subtree08_pretouch_started =
        std::chrono::steady_clock::now();

    for (std::size_t offset = 0;
         offset < logical_size;
         offset += page_size) {

        auto* page =
            reinterpret_cast<volatile std::uint8_t*>(
                logical_shm.data() + offset);

        *page = 0;
    }

    const auto type_subtree08_pretouch_finished =
        std::chrono::steady_clock::now();

    cw::server::shm_type_batch_execute_telemetry
        type_subtree08_execute{};

    const auto type_subtree08_canonical_started =
        std::chrono::steady_clock::now();

    const auto type_subtree08_canonical_result =
        cw::server::materialize_shm_type_batch_canonical(
            type_subtree08,
            settings.abi,
            new_layout,
            logical_shm,
            &type_subtree08_execute);

    const auto type_subtree08_canonical_finished =
        std::chrono::steady_clock::now();

    if (type_subtree08_canonical_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "SUBTREE-INLINE-08 canonical failed: "
            << static_cast<int>(
                type_subtree08_canonical_result)
            << '\n';

        return 13;
    }

    const auto type_subtree08_objects_started =
        std::chrono::steady_clock::now();

    const auto type_subtree08_objects_result =
        cw::server::materialize_shm_type_batch_objects_object_major(
            type_subtree08,
            settings.abi,
            new_layout,
            logical_shm,
            &type_subtree08_execute);

    const auto type_subtree08_objects_finished =
        std::chrono::steady_clock::now();

    if (type_subtree08_objects_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "SUBTREE-INLINE-08 object-major objects failed: "
            << static_cast<int>(
                type_subtree08_objects_result)
            << '\n';

        return 13;
    }

    if (type_subtree08_execute.reference_writes !=
            expected_reference_writes ||
        type_subtree08_execute.relative_reference_writes !=
            materialization.reference_member_bindings ||
        type_subtree08_execute.absolute_reference_writes !=
            expected_absolute_references ||
        type_subtree08_execute.object_reference_writes !=
            materialization.reference_object_bindings ||
        type_subtree08_execute.store_writes !=
            materialization.scalar_writes ||
        type_subtree08_execute.objects !=
            materialization.objects) {

        std::cerr
            << "SUBTREE-INLINE-08 physical write mismatch"
            << '\n';

        return 13;
    }


    shm.reset();

    const auto type_inline16_object_major_shm_created =
        shm.create(
            settings.shm.name,
            mapped_size,
            static_cast<std::uintptr_t>(
                settings.shm.fixed_base_address));

    if (type_inline16_object_major_shm_created !=
        cw::server::fixed_shared_memory_result::success) {

        std::cerr
            << "Cannot recreate fixed SHM for INLINE-16 object-major: "
            << static_cast<int>(
                type_inline16_object_major_shm_created)
            << '\n';

        return 14;
    }

    logical_shm =
        shm.bytes().first(
            logical_size);

    const auto type_inline16_object_major_pretouch_started =
        std::chrono::steady_clock::now();

    for (std::size_t offset = 0;
         offset < logical_size;
         offset += page_size) {

        auto* page =
            reinterpret_cast<volatile std::uint8_t*>(
                logical_shm.data() + offset);

        *page = 0;
    }

    const auto type_inline16_object_major_pretouch_finished =
        std::chrono::steady_clock::now();

    cw::server::shm_type_batch_execute_telemetry
        type_inline16_object_major_execute{};

    const auto type_inline16_object_major_canonical_started =
        std::chrono::steady_clock::now();

    const auto type_inline16_object_major_canonical_result =
        cw::server::materialize_shm_type_batch_canonical(
            type_inline16,
            settings.abi,
            new_layout,
            logical_shm,
            &type_inline16_object_major_execute);

    const auto type_inline16_object_major_canonical_finished =
        std::chrono::steady_clock::now();

    if (type_inline16_object_major_canonical_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "INLINE-16 object-major canonical failed: "
            << static_cast<int>(
                type_inline16_object_major_canonical_result)
            << '\n';

        return 14;
    }

    const auto type_inline16_object_major_objects_started =
        std::chrono::steady_clock::now();

    const auto type_inline16_object_major_objects_result =
        cw::server::
            materialize_shm_type_batch_objects_object_major(
                type_inline16,
                settings.abi,
                new_layout,
                logical_shm,
                &type_inline16_object_major_execute);

    const auto type_inline16_object_major_objects_finished =
        std::chrono::steady_clock::now();

    if (type_inline16_object_major_objects_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "INLINE-16 object-major objects failed: "
            << static_cast<int>(
                type_inline16_object_major_objects_result)
            << '\n';

        return 14;
    }

    if (type_inline16_object_major_execute.reference_writes !=
            expected_reference_writes ||
        type_inline16_object_major_execute.relative_reference_writes !=
            materialization.reference_member_bindings ||
        type_inline16_object_major_execute.absolute_reference_writes !=
            expected_absolute_references ||
        type_inline16_object_major_execute.object_reference_writes !=
            materialization.reference_object_bindings ||
        type_inline16_object_major_execute.store_writes !=
            materialization.scalar_writes ||
        type_inline16_object_major_execute.objects !=
            materialization.objects) {

        std::cerr
            << "INLINE-16 object-major physical write mismatch"
            << ",reference_expected="
            << expected_reference_writes
            << ",reference_actual="
            << type_inline16_object_major_execute.reference_writes
            << ",store_expected="
            << materialization.scalar_writes
            << ",store_actual="
            << type_inline16_object_major_execute.store_writes
            << '\n';

        return 14;
    }


    shm.reset();

    const auto type_inline32_object_major_shm_created =
        shm.create(
            settings.shm.name,
            mapped_size,
            static_cast<std::uintptr_t>(
                settings.shm.fixed_base_address));

    if (type_inline32_object_major_shm_created !=
        cw::server::fixed_shared_memory_result::success) {

        std::cerr
            << "Cannot recreate fixed SHM for INLINE-32 object-major: "
            << static_cast<int>(
                type_inline32_object_major_shm_created)
            << '\n';

        return 14;
    }

    logical_shm =
        shm.bytes().first(
            logical_size);

    const auto type_inline32_object_major_pretouch_started =
        std::chrono::steady_clock::now();

    for (std::size_t offset = 0;
         offset < logical_size;
         offset += page_size) {

        auto* page =
            reinterpret_cast<volatile std::uint8_t*>(
                logical_shm.data() + offset);

        *page = 0;
    }

    const auto type_inline32_object_major_pretouch_finished =
        std::chrono::steady_clock::now();

    cw::server::shm_type_batch_execute_telemetry
        type_inline32_object_major_execute{};

    const auto type_inline32_object_major_canonical_started =
        std::chrono::steady_clock::now();

    const auto type_inline32_object_major_canonical_result =
        cw::server::materialize_shm_type_batch_canonical(
            type_inline32,
            settings.abi,
            new_layout,
            logical_shm,
            &type_inline32_object_major_execute);

    const auto type_inline32_object_major_canonical_finished =
        std::chrono::steady_clock::now();

    if (type_inline32_object_major_canonical_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "INLINE-32 object-major canonical failed: "
            << static_cast<int>(
                type_inline32_object_major_canonical_result)
            << '\n';

        return 14;
    }

    const auto type_inline32_object_major_objects_started =
        std::chrono::steady_clock::now();

    const auto type_inline32_object_major_objects_result =
        cw::server::
            materialize_shm_type_batch_objects_object_major(
                type_inline32,
                settings.abi,
                new_layout,
                logical_shm,
                &type_inline32_object_major_execute);

    const auto type_inline32_object_major_objects_finished =
        std::chrono::steady_clock::now();

    if (type_inline32_object_major_objects_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "INLINE-32 object-major objects failed: "
            << static_cast<int>(
                type_inline32_object_major_objects_result)
            << '\n';

        return 14;
    }

    if (type_inline32_object_major_execute.reference_writes !=
            expected_reference_writes ||
        type_inline32_object_major_execute.relative_reference_writes !=
            materialization.reference_member_bindings ||
        type_inline32_object_major_execute.absolute_reference_writes !=
            expected_absolute_references ||
        type_inline32_object_major_execute.object_reference_writes !=
            materialization.reference_object_bindings ||
        type_inline32_object_major_execute.store_writes !=
            materialization.scalar_writes ||
        type_inline32_object_major_execute.objects !=
            materialization.objects) {

        std::cerr
            << "INLINE-32 object-major physical write mismatch"
            << ",reference_expected="
            << expected_reference_writes
            << ",reference_actual="
            << type_inline32_object_major_execute.reference_writes
            << ",store_expected="
            << materialization.scalar_writes
            << ",store_actual="
            << type_inline32_object_major_execute.store_writes
            << '\n';

        return 14;
    }


    shm.reset();

    const auto type_inline64_object_major_shm_created =
        shm.create(
            settings.shm.name,
            mapped_size,
            static_cast<std::uintptr_t>(
                settings.shm.fixed_base_address));

    if (type_inline64_object_major_shm_created !=
        cw::server::fixed_shared_memory_result::success) {

        std::cerr
            << "Cannot recreate fixed SHM for INLINE-64 object-major: "
            << static_cast<int>(
                type_inline64_object_major_shm_created)
            << '\n';

        return 14;
    }

    logical_shm =
        shm.bytes().first(
            logical_size);

    const auto type_inline64_object_major_pretouch_started =
        std::chrono::steady_clock::now();

    for (std::size_t offset = 0;
         offset < logical_size;
         offset += page_size) {

        auto* page =
            reinterpret_cast<volatile std::uint8_t*>(
                logical_shm.data() + offset);

        *page = 0;
    }

    const auto type_inline64_object_major_pretouch_finished =
        std::chrono::steady_clock::now();

    cw::server::shm_type_batch_execute_telemetry
        type_inline64_object_major_execute{};

    const auto type_inline64_object_major_canonical_started =
        std::chrono::steady_clock::now();

    const auto type_inline64_object_major_canonical_result =
        cw::server::materialize_shm_type_batch_canonical(
            type_inline64,
            settings.abi,
            new_layout,
            logical_shm,
            &type_inline64_object_major_execute);

    const auto type_inline64_object_major_canonical_finished =
        std::chrono::steady_clock::now();

    if (type_inline64_object_major_canonical_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "INLINE-64 object-major canonical failed: "
            << static_cast<int>(
                type_inline64_object_major_canonical_result)
            << '\n';

        return 14;
    }

    const auto type_inline64_object_major_objects_started =
        std::chrono::steady_clock::now();

    const auto type_inline64_object_major_objects_result =
        cw::server::
            materialize_shm_type_batch_objects_object_major(
                type_inline64,
                settings.abi,
                new_layout,
                logical_shm,
                &type_inline64_object_major_execute);

    const auto type_inline64_object_major_objects_finished =
        std::chrono::steady_clock::now();

    if (type_inline64_object_major_objects_result !=
        cw::server::shm_type_batch_result::success) {

        std::cerr
            << "INLINE-64 object-major objects failed: "
            << static_cast<int>(
                type_inline64_object_major_objects_result)
            << '\n';

        return 14;
    }

    if (type_inline64_object_major_execute.reference_writes !=
            expected_reference_writes ||
        type_inline64_object_major_execute.relative_reference_writes !=
            materialization.reference_member_bindings ||
        type_inline64_object_major_execute.absolute_reference_writes !=
            expected_absolute_references ||
        type_inline64_object_major_execute.object_reference_writes !=
            materialization.reference_object_bindings ||
        type_inline64_object_major_execute.store_writes !=
            materialization.scalar_writes ||
        type_inline64_object_major_execute.objects !=
            materialization.objects) {

        std::cerr
            << "INLINE-64 object-major physical write mismatch"
            << ",reference_expected="
            << expected_reference_writes
            << ",reference_actual="
            << type_inline64_object_major_execute.reference_writes
            << ",store_expected="
            << materialization.scalar_writes
            << ",store_actual="
            << type_inline64_object_major_execute.store_writes
            << '\n';

        return 14;
    }

    std::cout
        << "mode=shm-layout-compare"
        << ",old_layout_ms="
        << ms(old_finished - old_started)
        << ",new_layout_ms="
        << ms(new_finished - new_started)
        << ",shm_canonical_ms="
        << ms(canonical_finished - canonical_started)
        << ",shm_objects_ms="
        << ms(objects_finished - objects_started)
        << ",sparse_prepare_ms="
        << ms(sparse_prepare_finished - sparse_prepare_started)
        << ",sparse_canonical_ms="
        << ms(sparse_canonical_finished - sparse_canonical_started)
        << ",sparse_objects_ms="
        << ms(sparse_objects_finished - sparse_objects_started)
        << ",sparse_pretouch_ms="
        << ms(sparse_pretouch_finished - sparse_pretouch_started)
        << ",sparse_pretouched_canonical_ms="
        << ms(sparse_pretouched_canonical_finished - sparse_pretouched_canonical_started)
        << ",sparse_pretouched_objects_ms="
        << ms(sparse_pretouched_objects_finished - sparse_pretouched_objects_started)
        << ",type_area_prepare_ms="
        << ms(type_area_prepare_finished - type_area_prepare_started)
        << ",type_area_pretouch_ms="
        << ms(type_area_pretouch_finished - type_area_pretouch_started)
        << ",type_area_canonical_ms="
        << ms(type_area_canonical_finished - type_area_canonical_started)
        << ",type_area_objects_ms="
        << ms(type_area_objects_finished - type_area_objects_started)
        << ",type_batch_prepare_ms="
        << ms(type_batch_prepare_finished - type_batch_prepare_started)
        << ",type_batch_pretouch_ms="
        << ms(type_batch_pretouch_finished - type_batch_pretouch_started)
        << ",type_batch_canonical_ms="
        << ms(type_batch_canonical_finished - type_batch_canonical_started)
        << ",type_batch_objects_ms="
        << ms(type_batch_objects_finished - type_batch_objects_started)
        << ",hybrid_profile_ms="
        << ms(hybrid_profile_finished - hybrid_profile_started)
        << ",hybrid_04m_prepare_ms="
        << ms(hybrid_04m_prepare_finished - hybrid_04m_prepare_started)
        << ",hybrid_04m_pretouch_ms="
        << ms(hybrid_04m_pretouch_finished - hybrid_04m_pretouch_started)
        << ",hybrid_04m_canonical_ms="
        << ms(hybrid_04m_canonical_finished - hybrid_04m_canonical_started)
        << ",hybrid_04m_objects_ms="
        << ms(hybrid_04m_objects_finished - hybrid_04m_objects_started)
        << ",hybrid_sweep_01m_prepare_ms=" << hybrid_sweep_01m.prepare_ms
        << ",hybrid_sweep_01m_pretouch_ms=" << hybrid_sweep_01m.pretouch_ms
        << ",hybrid_sweep_01m_canonical_ms=" << hybrid_sweep_01m.canonical_ms
        << ",hybrid_sweep_01m_objects_ms=" << hybrid_sweep_01m.objects_ms
        << ",hybrid_sweep_02m_prepare_ms=" << hybrid_sweep_02m.prepare_ms
        << ",hybrid_sweep_02m_pretouch_ms=" << hybrid_sweep_02m.pretouch_ms
        << ",hybrid_sweep_02m_canonical_ms=" << hybrid_sweep_02m.canonical_ms
        << ",hybrid_sweep_02m_objects_ms=" << hybrid_sweep_02m.objects_ms
        << ",hybrid_sweep_08m_prepare_ms=" << hybrid_sweep_08m.prepare_ms
        << ",hybrid_sweep_08m_pretouch_ms=" << hybrid_sweep_08m.pretouch_ms
        << ",hybrid_sweep_08m_canonical_ms=" << hybrid_sweep_08m.canonical_ms
        << ",hybrid_sweep_08m_objects_ms=" << hybrid_sweep_08m.objects_ms
        << ",hybrid_sweep_16m_prepare_ms=" << hybrid_sweep_16m.prepare_ms
        << ",hybrid_sweep_16m_pretouch_ms=" << hybrid_sweep_16m.pretouch_ms
        << ",hybrid_sweep_16m_canonical_ms=" << hybrid_sweep_16m.canonical_ms
        << ",hybrid_sweep_16m_objects_ms=" << hybrid_sweep_16m.objects_ms
        << ",hybrid_sweep_32m_prepare_ms=" << hybrid_sweep_32m.prepare_ms
        << ",hybrid_sweep_32m_pretouch_ms=" << hybrid_sweep_32m.pretouch_ms
        << ",hybrid_sweep_32m_canonical_ms=" << hybrid_sweep_32m.canonical_ms
        << ",hybrid_sweep_32m_objects_ms=" << hybrid_sweep_32m.objects_ms
        << ",type_inline08_prepare_ms="
        << ms(type_inline08_prepare_finished - type_inline08_prepare_started)
        << ",type_inline08_pretouch_ms="
        << ms(type_inline08_pretouch_finished - type_inline08_pretouch_started)
        << ",type_inline08_canonical_ms="
        << ms(type_inline08_canonical_finished - type_inline08_canonical_started)
        << ",type_inline08_objects_ms="
        << ms(type_inline08_objects_finished - type_inline08_objects_started)
        << ",type_inline08_object_major_pretouch_ms="
        << ms(type_inline08_object_major_pretouch_finished -
              type_inline08_object_major_pretouch_started)
        << ",type_inline08_object_major_canonical_ms="
        << ms(type_inline08_object_major_canonical_finished -
              type_inline08_object_major_canonical_started)
        << ",type_inline08_object_major_objects_ms="
        << ms(type_inline08_object_major_objects_finished -
              type_inline08_object_major_objects_started)
        << ",type_subtree08_prepare_ms="
        << ms(type_subtree08_prepare_finished - type_subtree08_prepare_started)
        << ",type_subtree08_pretouch_ms="
        << ms(type_subtree08_pretouch_finished - type_subtree08_pretouch_started)
        << ",type_subtree08_canonical_ms="
        << ms(type_subtree08_canonical_finished - type_subtree08_canonical_started)
        << ",type_subtree08_objects_ms="
        << ms(type_subtree08_objects_finished - type_subtree08_objects_started)
        << ",type_inline16_prepare_ms="
        << ms(type_inline16_prepare_finished - type_inline16_prepare_started)
        << ",type_inline16_object_major_pretouch_ms="
        << ms(type_inline16_object_major_pretouch_finished -
              type_inline16_object_major_pretouch_started)
        << ",type_inline16_object_major_canonical_ms="
        << ms(type_inline16_object_major_canonical_finished -
              type_inline16_object_major_canonical_started)
        << ",type_inline16_object_major_objects_ms="
        << ms(type_inline16_object_major_objects_finished -
              type_inline16_object_major_objects_started)
        << ",type_inline32_prepare_ms="
        << ms(type_inline32_prepare_finished - type_inline32_prepare_started)
        << ",type_inline32_object_major_pretouch_ms="
        << ms(type_inline32_object_major_pretouch_finished -
              type_inline32_object_major_pretouch_started)
        << ",type_inline32_object_major_canonical_ms="
        << ms(type_inline32_object_major_canonical_finished -
              type_inline32_object_major_canonical_started)
        << ",type_inline32_object_major_objects_ms="
        << ms(type_inline32_object_major_objects_finished -
              type_inline32_object_major_objects_started)
        << ",type_inline64_prepare_ms="
        << ms(type_inline64_prepare_finished - type_inline64_prepare_started)
        << ",type_inline64_object_major_pretouch_ms="
        << ms(type_inline64_object_major_pretouch_finished -
              type_inline64_object_major_pretouch_started)
        << ",type_inline64_object_major_canonical_ms="
        << ms(type_inline64_object_major_canonical_finished -
              type_inline64_object_major_canonical_started)
        << ",type_inline64_object_major_objects_ms="
        << ms(type_inline64_object_major_objects_finished -
              type_inline64_object_major_objects_started)
        << ",runtime_bytes="
        << old_layout.size()
        << ",alignment="
        << old_layout.alignment()
        << ",types_checked="
        << counts.type_slots
        << ",members_checked="
        << counts.member_slots
        << ",bases_checked="
        << counts.base_slots
        << ",objects_checked="
        << counts.object_slots
        << ",unconnected_checked="
        << counts.unconnected
        << ",shm_canonical_values="
        << materialization.canonical_values
        << ",shm_canonical_references="
        << materialization.canonical_references
        << ",shm_objects="
        << materialization.objects
        << ",shm_records="
        << materialization.records
        << ",shm_bases="
        << materialization.bases
        << ",shm_members="
        << materialization.members
        << ",shm_arrays="
        << materialization.arrays
        << ",shm_array_elements="
        << materialization.array_elements
        << ",shm_scalar_writes="
        << materialization.scalar_writes
        << ",shm_zero_noops="
        << materialization.zero_noops
        << ",shm_references="
        << materialization.references
        << ",shm_reference_chain_steps="
        << materialization.reference_chain_steps
        << ",shm_reference_unconnected="
        << materialization.reference_unconnected
        << ",shm_reference_member_bindings="
        << materialization.reference_member_bindings
        << ",shm_reference_object_bindings="
        << materialization.reference_object_bindings
        << ",sparse_resident_bytes=" << sparse_prepare.resident_bytes
        << ",sparse_action_size=" << sparse_prepare.action_size
        << ",sparse_hot_action_bytes=" << sparse_prepare.hot_action_bytes
        << ",sparse_absolute_offsets=" << sparse_prepare.absolute_offsets
        << ",sparse_absolute_offset_bytes=" << sparse_prepare.absolute_offset_bytes
        << ",sparse_constants=" << sparse_prepare.constants
        << ",sparse_constant_bytes=" << sparse_prepare.constant_bytes
        << ",sparse_repeat_descriptors=" << sparse_prepare.repeat_descriptors
        << ",sparse_repeat_descriptor_bytes=" << sparse_prepare.repeat_descriptor_bytes
        << ",sparse_root_bytes=" << sparse_prepare.root_bytes
        << ",sparse_actions=" << sparse_prepare.actions
        << ",sparse_canonical_roots=" << sparse_prepare.canonical_roots
        << ",sparse_object_roots=" << sparse_prepare.object_roots
        << ",sparse_named_programs=" << sparse_prepare.named_programs
        << ",sparse_derived_programs=" << sparse_prepare.derived_programs
        << ",sparse_empty_programs=" << sparse_prepare.empty_programs
        << ",sparse_ref_relative_actions=" << sparse_prepare.reference_relative_actions
        << ",sparse_ref_absolute_actions=" << sparse_prepare.reference_absolute_actions
        << ",sparse_store_actions=" << sparse_prepare.store_actions
        << ",sparse_call_actions=" << sparse_prepare.call_actions
        << ",sparse_repeat_actions=" << sparse_prepare.repeat_actions
        << ",sparse_reference_chain_steps_resolved=" << sparse_prepare.reference_chain_steps_resolved
        << ",sparse_zero_actions_elided=" << sparse_prepare.zero_actions_elided
        << ",sparse_action_visits=" << sparse_execute.action_visits
        << ",sparse_reference_writes=" << sparse_execute.reference_writes
        << ",sparse_store_writes=" << sparse_execute.store_writes
        << ",sparse_call_visits=" << sparse_execute.call_visits
        << ",sparse_repeat_visits=" << sparse_execute.repeat_visits
        << ",sparse_repeat_iterations=" << sparse_execute.repeat_iterations
        << ",sparse_pretouched_action_visits=" << sparse_pretouched_execute.action_visits
        << ",sparse_pretouched_reference_writes=" << sparse_pretouched_execute.reference_writes
        << ",sparse_pretouched_store_writes=" << sparse_pretouched_execute.store_writes
        << ",sparse_pretouched_call_visits=" << sparse_pretouched_execute.call_visits
        << ",sparse_pretouched_repeat_visits=" << sparse_pretouched_execute.repeat_visits
        << ",sparse_pretouched_repeat_iterations=" << sparse_pretouched_execute.repeat_iterations
        << ",sparse_profile_call_actions=" << sparse_call_profile.call_actions
        << ",sparse_profile_call_actions_leaf=" << sparse_call_profile.call_actions_leaf
        << ",sparse_profile_call_actions_contains_call=" << sparse_call_profile.call_actions_contains_call
        << ",sparse_profile_call_actions_contains_repeat=" << sparse_call_profile.call_actions_contains_repeat
        << ",sparse_profile_call_actions_child_1=" << sparse_call_profile.call_actions_by_child_size[0]
        << ",sparse_profile_call_actions_child_2=" << sparse_call_profile.call_actions_by_child_size[1]
        << ",sparse_profile_call_actions_child_3=" << sparse_call_profile.call_actions_by_child_size[2]
        << ",sparse_profile_call_actions_child_4=" << sparse_call_profile.call_actions_by_child_size[3]
        << ",sparse_profile_call_actions_child_5_8=" << sparse_call_profile.call_actions_by_child_size[4]
        << ",sparse_profile_call_actions_child_9_16=" << sparse_call_profile.call_actions_by_child_size[5]
        << ",sparse_profile_call_actions_child_gt16=" << sparse_call_profile.call_actions_by_child_size[6]
        << ",sparse_profile_leaf_call_actions_child_1=" << sparse_call_profile.leaf_call_actions_by_child_size[0]
        << ",sparse_profile_leaf_call_actions_child_2=" << sparse_call_profile.leaf_call_actions_by_child_size[1]
        << ",sparse_profile_leaf_call_actions_child_3=" << sparse_call_profile.leaf_call_actions_by_child_size[2]
        << ",sparse_profile_leaf_call_actions_child_4=" << sparse_call_profile.leaf_call_actions_by_child_size[3]
        << ",sparse_profile_leaf_call_actions_child_5_8=" << sparse_call_profile.leaf_call_actions_by_child_size[4]
        << ",sparse_profile_leaf_call_actions_child_9_16=" << sparse_call_profile.leaf_call_actions_by_child_size[5]
        << ",sparse_profile_leaf_call_actions_child_gt16=" << sparse_call_profile.leaf_call_actions_by_child_size[6]
        << ",sparse_profile_call_visits=" << sparse_call_profile.call_visits
        << ",sparse_profile_call_visits_leaf=" << sparse_call_profile.call_visits_leaf
        << ",sparse_profile_call_visits_contains_call=" << sparse_call_profile.call_visits_contains_call
        << ",sparse_profile_call_visits_contains_repeat=" << sparse_call_profile.call_visits_contains_repeat
        << ",sparse_profile_call_visits_child_1=" << sparse_call_profile.call_visits_by_child_size[0]
        << ",sparse_profile_call_visits_child_2=" << sparse_call_profile.call_visits_by_child_size[1]
        << ",sparse_profile_call_visits_child_3=" << sparse_call_profile.call_visits_by_child_size[2]
        << ",sparse_profile_call_visits_child_4=" << sparse_call_profile.call_visits_by_child_size[3]
        << ",sparse_profile_call_visits_child_5_8=" << sparse_call_profile.call_visits_by_child_size[4]
        << ",sparse_profile_call_visits_child_9_16=" << sparse_call_profile.call_visits_by_child_size[5]
        << ",sparse_profile_call_visits_child_gt16=" << sparse_call_profile.call_visits_by_child_size[6]
        << ",sparse_profile_leaf_call_visits_child_1=" << sparse_call_profile.leaf_call_visits_by_child_size[0]
        << ",sparse_profile_leaf_call_visits_child_2=" << sparse_call_profile.leaf_call_visits_by_child_size[1]
        << ",sparse_profile_leaf_call_visits_child_3=" << sparse_call_profile.leaf_call_visits_by_child_size[2]
        << ",sparse_profile_leaf_call_visits_child_4=" << sparse_call_profile.leaf_call_visits_by_child_size[3]
        << ",sparse_profile_leaf_call_visits_child_5_8=" << sparse_call_profile.leaf_call_visits_by_child_size[4]
        << ",sparse_profile_leaf_call_visits_child_9_16=" << sparse_call_profile.leaf_call_visits_by_child_size[5]
        << ",sparse_profile_leaf_call_visits_child_gt16=" << sparse_call_profile.leaf_call_visits_by_child_size[6]
        << ",type_area_prefinal_resident_bytes=" << type_area_prepare.prefinal_resident_bytes
        << ",type_area_reclaimed_bytes=" << type_area_prepare.reclaimed_bytes
        << ",type_area_prefinal_type_apis=" << type_area_prepare.prefinal_type_apis
        << ",type_area_reachable_type_apis=" << type_area_prepare.reachable_type_apis
        << ",type_area_discarded_type_apis=" << type_area_prepare.discarded_type_apis
        << ",type_area_discarded_relative_refs=" << type_area_prepare.discarded_relative_references
        << ",type_area_discarded_absolute_refs=" << type_area_prepare.discarded_absolute_references
        << ",type_area_discarded_object_refs=" << type_area_prepare.discarded_object_references
        << ",type_area_discarded_stores=" << type_area_prepare.discarded_stores
        << ",type_area_discarded_repeats=" << type_area_prepare.discarded_repeats
        << ",type_area_discarded_constants=" << type_area_prepare.discarded_constants
        << ",type_area_resident_bytes=" << type_area_prepare.resident_bytes
        << ",type_area_type_apis=" << type_area_prepare.type_apis
        << ",type_area_named_types_prepared=" << type_area_prepare.named_types_prepared
        << ",type_area_derived_types_prepared=" << type_area_prepare.derived_types_prepared
        << ",type_area_canonical_roots=" << type_area_prepare.canonical_roots
        << ",type_area_objects=" << type_area_prepare.objects
        << ",type_area_object_patches=" << type_area_prepare.object_patches
        << ",type_area_relative_refs=" << type_area_prepare.relative_references
        << ",type_area_absolute_refs=" << type_area_prepare.absolute_references
        << ",type_area_object_refs=" << type_area_prepare.object_references
        << ",type_area_stores=" << type_area_prepare.stores
        << ",type_area_repeats=" << type_area_prepare.repeats
        << ",type_area_identity_resolutions=" << type_area_prepare.semantic_identity_resolutions
        << ",type_area_object_binding_resolutions=" << type_area_prepare.object_binding_resolutions
        << ",type_area_reference_chain_steps_resolved=" << type_area_prepare.reference_chain_steps_resolved
        << ",type_area_flattened_api_copies=" << type_area_prepare.flattened_api_copies
        << ",type_area_zero_ops_elided=" << type_area_prepare.zero_operations_elided
        << ",type_area_type_api_bytes=" << type_area_prepare.type_api_bytes
        << ",type_area_relative_ref_bytes=" << type_area_prepare.relative_reference_bytes
        << ",type_area_absolute_ref_bytes=" << type_area_prepare.absolute_reference_bytes
        << ",type_area_object_ref_bytes=" << type_area_prepare.object_reference_bytes
        << ",type_area_store_bytes=" << type_area_prepare.store_bytes
        << ",type_area_repeat_bytes=" << type_area_prepare.repeat_bytes
        << ",type_area_constant_bytes=" << type_area_prepare.constant_bytes
        << ",type_area_object_where_bytes=" << type_area_prepare.object_where_bytes
        << ",type_area_object_runtime_bytes=" << type_area_prepare.object_runtime_bytes
        << ",type_area_canonical_root_bytes=" << type_area_prepare.canonical_root_bytes
        << ",type_area_object_patch_bytes=" << type_area_prepare.object_patch_bytes
        << ",type_area_api_applications=" << type_area_execute.api_applications
        << ",type_area_reference_writes=" << type_area_execute.reference_writes
        << ",type_area_relative_reference_writes=" << type_area_execute.relative_reference_writes
        << ",type_area_absolute_reference_writes=" << type_area_execute.absolute_reference_writes
        << ",type_area_object_reference_writes=" << type_area_execute.object_reference_writes
        << ",type_area_store_writes=" << type_area_execute.store_writes
        << ",type_area_repeat_visits=" << type_area_execute.repeat_visits
        << ",type_area_repeat_iterations=" << type_area_execute.repeat_iterations
        << ",type_area_object_patch_writes=" << type_area_execute.object_patch_writes
        << ",type_batch_resident_bytes=" << type_batch_prepare.resident_bytes
        << ",type_batch_type_apis=" << type_batch_prepare.type_apis
        << ",type_batch_named_types_prepared=" << type_batch_prepare.named_types_prepared
        << ",type_batch_derived_types_prepared=" << type_batch_prepare.derived_types_prepared
        << ",type_batch_canonical_roots=" << type_batch_prepare.canonical_roots
        << ",type_batch_objects=" << type_batch_prepare.objects
        << ",type_batch_object_patches=" << type_batch_prepare.object_patches
        << ",type_batch_relative_refs=" << type_batch_prepare.relative_references
        << ",type_batch_absolute_refs=" << type_batch_prepare.absolute_references
        << ",type_batch_object_refs=" << type_batch_prepare.object_references
        << ",type_batch_stores=" << type_batch_prepare.stores
        << ",type_batch_children=" << type_batch_prepare.children
        << ",type_batch_repeats=" << type_batch_prepare.repeats
        << ",type_batch_object_groups=" << type_batch_prepare.object_groups
        << ",type_batch_canonical_groups=" << type_batch_prepare.canonical_groups
        << ",type_batch_grouped_object_roots=" << type_batch_prepare.grouped_object_roots
        << ",type_batch_grouped_canonical_roots=" << type_batch_prepare.grouped_canonical_roots
        << ",type_batch_batch_size=" << type_batch_prepare.batch_size
        << ",type_batch_identity_resolutions=" << type_batch_prepare.semantic_identity_resolutions
        << ",type_batch_object_binding_resolutions=" << type_batch_prepare.object_binding_resolutions
        << ",type_batch_reference_chain_steps_resolved=" << type_batch_prepare.reference_chain_steps_resolved
        << ",type_batch_child_edges=" << type_batch_prepare.child_edges
        << ",type_batch_zero_ops_elided=" << type_batch_prepare.zero_operations_elided
        << ",type_batch_type_api_bytes=" << type_batch_prepare.type_api_bytes
        << ",type_batch_relative_ref_bytes=" << type_batch_prepare.relative_reference_bytes
        << ",type_batch_absolute_ref_bytes=" << type_batch_prepare.absolute_reference_bytes
        << ",type_batch_object_ref_bytes=" << type_batch_prepare.object_reference_bytes
        << ",type_batch_store_bytes=" << type_batch_prepare.store_bytes
        << ",type_batch_child_bytes=" << type_batch_prepare.child_bytes
        << ",type_batch_repeat_bytes=" << type_batch_prepare.repeat_bytes
        << ",type_batch_constant_bytes=" << type_batch_prepare.constant_bytes
        << ",type_batch_object_where_bytes=" << type_batch_prepare.object_where_bytes
        << ",type_batch_object_runtime_bytes=" << type_batch_prepare.object_runtime_bytes
        << ",type_batch_canonical_root_bytes=" << type_batch_prepare.canonical_root_bytes
        << ",type_batch_object_group_bytes=" << type_batch_prepare.object_group_bytes
        << ",type_batch_object_group_offset_bytes=" << type_batch_prepare.object_group_offset_bytes
        << ",type_batch_canonical_group_bytes=" << type_batch_prepare.canonical_group_bytes
        << ",type_batch_canonical_group_offset_bytes=" << type_batch_prepare.canonical_group_offset_bytes
        << ",type_batch_object_patch_bytes=" << type_batch_prepare.object_patch_bytes
        << ",type_batch_api_applications=" << type_batch_execute.api_applications
        << ",type_batch_batch_api_applications=" << type_batch_execute.batch_api_applications
        << ",type_batch_child_visits=" << type_batch_execute.child_visits
        << ",type_batch_reference_writes=" << type_batch_execute.reference_writes
        << ",type_batch_relative_reference_writes=" << type_batch_execute.relative_reference_writes
        << ",type_batch_absolute_reference_writes=" << type_batch_execute.absolute_reference_writes
        << ",type_batch_object_reference_writes=" << type_batch_execute.object_reference_writes
        << ",type_batch_store_writes=" << type_batch_execute.store_writes
        << ",type_batch_repeat_visits=" << type_batch_execute.repeat_visits
        << ",type_batch_repeat_iterations=" << type_batch_execute.repeat_iterations
        << ",type_batch_object_batches=" << type_batch_execute.object_batches
        << ",type_batch_canonical_batches=" << type_batch_execute.canonical_batches
        << ",type_batch_object_patch_writes=" << type_batch_execute.object_patch_writes
        << ",hybrid_object_groups=" << hybrid_profile.object_groups
        << ",hybrid_object_roots=" << hybrid_profile.object_roots
        << ",hybrid_candidate_type_apis=" << hybrid_profile.candidate_type_apis
        << ",hybrid_singleton_groups=" << hybrid_profile.singleton_groups
        << ",hybrid_groups_le_2=" << hybrid_profile.groups_le_2
        << ",hybrid_groups_le_4=" << hybrid_profile.groups_le_4
        << ",hybrid_groups_le_8=" << hybrid_profile.groups_le_8
        << ",hybrid_groups_le_16=" << hybrid_profile.groups_le_16
        << ",hybrid_groups_le_32=" << hybrid_profile.groups_le_32
        << ",hybrid_groups_le_64=" << hybrid_profile.groups_le_64
        << ",hybrid_groups_gt_64=" << hybrid_profile.groups_gt_64
        << ",hybrid_max_group_roots=" << hybrid_profile.max_group_roots
        << ",hybrid_all_candidate_flat_bytes=" << hybrid_profile.all_candidate_flat_bytes
        << ",hybrid_all_weighted_child_visits=" << hybrid_profile.all_weighted_child_visits
        << ",hybrid_all_batch_child_visits=" << hybrid_profile.all_batch_child_visits
        << ",hybrid_1m_selected=" << hybrid_profile.budgets[0].selected_type_apis
        << ",hybrid_1m_flat_bytes=" << hybrid_profile.budgets[0].flat_bytes
        << ",hybrid_1m_object_roots=" << hybrid_profile.budgets[0].object_roots
        << ",hybrid_1m_weighted_child_visits=" << hybrid_profile.budgets[0].weighted_child_visits
        << ",hybrid_1m_batch_child_visits=" << hybrid_profile.budgets[0].batch_child_visits
        << ",hybrid_4m_selected=" << hybrid_profile.budgets[1].selected_type_apis
        << ",hybrid_4m_flat_bytes=" << hybrid_profile.budgets[1].flat_bytes
        << ",hybrid_4m_object_roots=" << hybrid_profile.budgets[1].object_roots
        << ",hybrid_4m_weighted_child_visits=" << hybrid_profile.budgets[1].weighted_child_visits
        << ",hybrid_4m_batch_child_visits=" << hybrid_profile.budgets[1].batch_child_visits
        << ",hybrid_8m_selected=" << hybrid_profile.budgets[2].selected_type_apis
        << ",hybrid_8m_flat_bytes=" << hybrid_profile.budgets[2].flat_bytes
        << ",hybrid_8m_object_roots=" << hybrid_profile.budgets[2].object_roots
        << ",hybrid_8m_weighted_child_visits=" << hybrid_profile.budgets[2].weighted_child_visits
        << ",hybrid_8m_batch_child_visits=" << hybrid_profile.budgets[2].batch_child_visits
        << ",hybrid_16m_selected=" << hybrid_profile.budgets[3].selected_type_apis
        << ",hybrid_16m_flat_bytes=" << hybrid_profile.budgets[3].flat_bytes
        << ",hybrid_16m_object_roots=" << hybrid_profile.budgets[3].object_roots
        << ",hybrid_16m_weighted_child_visits=" << hybrid_profile.budgets[3].weighted_child_visits
        << ",hybrid_16m_batch_child_visits=" << hybrid_profile.budgets[3].batch_child_visits
        << ",hybrid_32m_selected=" << hybrid_profile.budgets[4].selected_type_apis
        << ",hybrid_32m_flat_bytes=" << hybrid_profile.budgets[4].flat_bytes
        << ",hybrid_32m_object_roots=" << hybrid_profile.budgets[4].object_roots
        << ",hybrid_32m_weighted_child_visits=" << hybrid_profile.budgets[4].weighted_child_visits
        << ",hybrid_32m_batch_child_visits=" << hybrid_profile.budgets[4].batch_child_visits
        << ",hybrid_64m_selected=" << hybrid_profile.budgets[5].selected_type_apis
        << ",hybrid_64m_flat_bytes=" << hybrid_profile.budgets[5].flat_bytes
        << ",hybrid_64m_object_roots=" << hybrid_profile.budgets[5].object_roots
        << ",hybrid_64m_weighted_child_visits=" << hybrid_profile.budgets[5].weighted_child_visits
        << ",hybrid_64m_batch_child_visits=" << hybrid_profile.budgets[5].batch_child_visits
        << ",hybrid_04m_budget_bytes=" << hybrid_04m_prepare.budget_bytes
        << ",hybrid_04m_candidate_type_apis=" << hybrid_04m_prepare.candidate_type_apis
        << ",hybrid_04m_selected_type_apis=" << hybrid_04m_prepare.selected_type_apis
        << ",hybrid_04m_hot_object_roots=" << hybrid_04m_prepare.hot_object_roots
        << ",hybrid_04m_cold_object_roots=" << hybrid_04m_prepare.cold_object_roots
        << ",hybrid_04m_selected_weighted_child_visits=" << hybrid_04m_prepare.selected_weighted_child_visits
        << ",hybrid_04m_all_weighted_child_visits=" << hybrid_04m_prepare.all_weighted_child_visits
        << ",hybrid_04m_selected_weighted_physical_writes=" << hybrid_04m_prepare.selected_weighted_physical_writes
        << ",hybrid_04m_all_weighted_physical_writes=" << hybrid_04m_prepare.all_weighted_physical_writes
        << ",hybrid_04m_selected_weighted_work=" << hybrid_04m_prepare.selected_weighted_work
        << ",hybrid_04m_all_weighted_work=" << hybrid_04m_prepare.all_weighted_work
        << ",hybrid_04m_flat_payload_bytes=" << hybrid_04m_prepare.flat_payload_bytes
        << ",hybrid_04m_selection_map_bytes=" << hybrid_04m_prepare.selection_map_bytes
        << ",hybrid_04m_resident_bytes=" << hybrid_04m_prepare.resident_bytes
        << ",hybrid_04m_base_type_batch_bytes=" << hybrid_04m_prepare.base_type_batch_bytes
        << ",hybrid_04m_total_runtime_metadata_bytes=" << hybrid_04m_prepare.total_runtime_metadata_bytes
        << ",hybrid_04m_flat_apis=" << hybrid_04m_prepare.flat_apis
        << ",hybrid_04m_flat_relative_refs=" << hybrid_04m_prepare.flat_relative_references
        << ",hybrid_04m_flat_absolute_refs=" << hybrid_04m_prepare.flat_absolute_references
        << ",hybrid_04m_flat_object_refs=" << hybrid_04m_prepare.flat_object_references
        << ",hybrid_04m_flat_stores=" << hybrid_04m_prepare.flat_stores
        << ",hybrid_04m_flat_repeats=" << hybrid_04m_prepare.flat_repeats
        << ",hybrid_04m_flat_api_applications=" << hybrid_04m_execute.flat_api_applications
        << ",hybrid_04m_local_batch_api_applications=" << hybrid_04m_execute.local_batch_api_applications
        << ",hybrid_04m_local_child_visits=" << hybrid_04m_execute.local_child_visits
        << ",hybrid_04m_cold_batches=" << hybrid_04m_execute.cold_batches
        << ",hybrid_04m_reference_writes=" << hybrid_04m_execute.reference_writes
        << ",hybrid_04m_relative_reference_writes=" << hybrid_04m_execute.relative_reference_writes
        << ",hybrid_04m_absolute_reference_writes=" << hybrid_04m_execute.absolute_reference_writes
        << ",hybrid_04m_object_reference_writes=" << hybrid_04m_execute.object_reference_writes
        << ",hybrid_04m_store_writes=" << hybrid_04m_execute.store_writes
        << ",hybrid_04m_repeat_visits=" << hybrid_04m_execute.repeat_visits
        << ",hybrid_04m_repeat_iterations=" << hybrid_04m_execute.repeat_iterations
        << ",hybrid_04m_object_patch_writes=" << hybrid_04m_execute.object_patch_writes
        << ",hybrid_sweep_01m_selected=" << hybrid_sweep_01m.prepare.selected_type_apis
        << ",hybrid_sweep_01m_hot_roots=" << hybrid_sweep_01m.prepare.hot_object_roots
        << ",hybrid_sweep_01m_flat_bytes=" << hybrid_sweep_01m.prepare.flat_payload_bytes
        << ",hybrid_sweep_01m_total_metadata_bytes=" << hybrid_sweep_01m.prepare.total_runtime_metadata_bytes
        << ",hybrid_sweep_01m_selected_work=" << hybrid_sweep_01m.prepare.selected_weighted_work
        << ",hybrid_sweep_02m_selected=" << hybrid_sweep_02m.prepare.selected_type_apis
        << ",hybrid_sweep_02m_hot_roots=" << hybrid_sweep_02m.prepare.hot_object_roots
        << ",hybrid_sweep_02m_flat_bytes=" << hybrid_sweep_02m.prepare.flat_payload_bytes
        << ",hybrid_sweep_02m_total_metadata_bytes=" << hybrid_sweep_02m.prepare.total_runtime_metadata_bytes
        << ",hybrid_sweep_02m_selected_work=" << hybrid_sweep_02m.prepare.selected_weighted_work
        << ",hybrid_sweep_08m_selected=" << hybrid_sweep_08m.prepare.selected_type_apis
        << ",hybrid_sweep_08m_hot_roots=" << hybrid_sweep_08m.prepare.hot_object_roots
        << ",hybrid_sweep_08m_flat_bytes=" << hybrid_sweep_08m.prepare.flat_payload_bytes
        << ",hybrid_sweep_08m_total_metadata_bytes=" << hybrid_sweep_08m.prepare.total_runtime_metadata_bytes
        << ",hybrid_sweep_08m_selected_work=" << hybrid_sweep_08m.prepare.selected_weighted_work
        << ",hybrid_sweep_16m_selected=" << hybrid_sweep_16m.prepare.selected_type_apis
        << ",hybrid_sweep_16m_hot_roots=" << hybrid_sweep_16m.prepare.hot_object_roots
        << ",hybrid_sweep_16m_flat_bytes=" << hybrid_sweep_16m.prepare.flat_payload_bytes
        << ",hybrid_sweep_16m_total_metadata_bytes=" << hybrid_sweep_16m.prepare.total_runtime_metadata_bytes
        << ",hybrid_sweep_16m_selected_work=" << hybrid_sweep_16m.prepare.selected_weighted_work
        << ",hybrid_sweep_32m_selected=" << hybrid_sweep_32m.prepare.selected_type_apis
        << ",hybrid_sweep_32m_hot_roots=" << hybrid_sweep_32m.prepare.hot_object_roots
        << ",hybrid_sweep_32m_flat_bytes=" << hybrid_sweep_32m.prepare.flat_payload_bytes
        << ",hybrid_sweep_32m_total_metadata_bytes=" << hybrid_sweep_32m.prepare.total_runtime_metadata_bytes
        << ",hybrid_sweep_32m_selected_work=" << hybrid_sweep_32m.prepare.selected_weighted_work
        << ",type_inline08_resident_bytes=" << type_inline08_prepare.resident_bytes
        << ",type_inline08_type_apis=" << type_inline08_prepare.type_apis
        << ",type_inline08_relative_refs=" << type_inline08_prepare.relative_references
        << ",type_inline08_absolute_refs=" << type_inline08_prepare.absolute_references
        << ",type_inline08_object_refs=" << type_inline08_prepare.object_references
        << ",type_inline08_stores=" << type_inline08_prepare.stores
        << ",type_inline08_children=" << type_inline08_prepare.children
        << ",type_inline08_repeats=" << type_inline08_prepare.repeats
        << ",type_inline08_leaf_limit=" << type_inline08_prepare.inline_leaf_limit
        << ",type_inline08_leaf_children=" << type_inline08_prepare.inline_leaf_children
        << ",type_inline08_leaf_operations=" << type_inline08_prepare.inline_leaf_operations
        << ",type_inline08_leaf_relative_refs=" << type_inline08_prepare.inline_leaf_relative_references
        << ",type_inline08_leaf_absolute_refs=" << type_inline08_prepare.inline_leaf_absolute_references
        << ",type_inline08_leaf_object_refs=" << type_inline08_prepare.inline_leaf_object_references
        << ",type_inline08_leaf_stores=" << type_inline08_prepare.inline_leaf_stores
        << ",type_inline08_child_edges_retained=" << type_inline08_prepare.child_edges
        << ",type_inline08_api_applications=" << type_inline08_execute.api_applications
        << ",type_inline08_child_visits=" << type_inline08_execute.child_visits
        << ",type_inline08_repeat_visits=" << type_inline08_execute.repeat_visits
        << ",type_inline08_repeat_iterations=" << type_inline08_execute.repeat_iterations
        << ",type_inline08_object_batches=" << type_inline08_execute.object_batches
        << ",type_inline08_reference_writes=" << type_inline08_execute.reference_writes
        << ",type_inline08_store_writes=" << type_inline08_execute.store_writes
        << ",type_inline08_object_major_api_applications="
        << type_inline08_object_major_execute.api_applications
        << ",type_inline08_object_major_child_visits="
        << type_inline08_object_major_execute.child_visits
        << ",type_inline08_object_major_repeat_visits="
        << type_inline08_object_major_execute.repeat_visits
        << ",type_inline08_object_major_repeat_iterations="
        << type_inline08_object_major_execute.repeat_iterations
        << ",type_inline08_object_major_reference_writes="
        << type_inline08_object_major_execute.reference_writes
        << ",type_inline08_object_major_store_writes="
        << type_inline08_object_major_execute.store_writes
        << ",type_subtree08_resident_bytes=" << type_subtree08_prepare.resident_bytes
        << ",type_subtree08_type_apis=" << type_subtree08_prepare.type_apis
        << ",type_subtree08_relative_refs=" << type_subtree08_prepare.relative_references
        << ",type_subtree08_absolute_refs=" << type_subtree08_prepare.absolute_references
        << ",type_subtree08_object_refs=" << type_subtree08_prepare.object_references
        << ",type_subtree08_stores=" << type_subtree08_prepare.stores
        << ",type_subtree08_children=" << type_subtree08_prepare.children
        << ",type_subtree08_repeats=" << type_subtree08_prepare.repeats
        << ",type_subtree08_limit=" << type_subtree08_prepare.inline_subtree_limit
        << ",type_subtree08_inlined_children=" << type_subtree08_prepare.inline_subtree_children
        << ",type_subtree08_inlined_operations=" << type_subtree08_prepare.inline_subtree_operations
        << ",type_subtree08_api_applications=" << type_subtree08_execute.api_applications
        << ",type_subtree08_child_visits=" << type_subtree08_execute.child_visits
        << ",type_subtree08_repeat_visits=" << type_subtree08_execute.repeat_visits
        << ",type_subtree08_repeat_iterations=" << type_subtree08_execute.repeat_iterations
        << ",type_subtree08_reference_writes=" << type_subtree08_execute.reference_writes
        << ",type_subtree08_store_writes=" << type_subtree08_execute.store_writes
        << ",type_inline16_resident_bytes="
        << type_inline16_prepare.resident_bytes
        << ",type_inline16_leaf_limit="
        << type_inline16_prepare.inline_leaf_limit
        << ",type_inline16_leaf_children="
        << type_inline16_prepare.inline_leaf_children
        << ",type_inline16_leaf_operations="
        << type_inline16_prepare.inline_leaf_operations
        << ",type_inline16_children="
        << type_inline16_prepare.children
        << ",type_inline16_object_major_api_applications="
        << type_inline16_object_major_execute.api_applications
        << ",type_inline16_object_major_child_visits="
        << type_inline16_object_major_execute.child_visits
        << ",type_inline16_object_major_repeat_visits="
        << type_inline16_object_major_execute.repeat_visits
        << ",type_inline16_object_major_repeat_iterations="
        << type_inline16_object_major_execute.repeat_iterations

        << ",type_inline32_resident_bytes="
        << type_inline32_prepare.resident_bytes
        << ",type_inline32_leaf_limit="
        << type_inline32_prepare.inline_leaf_limit
        << ",type_inline32_leaf_children="
        << type_inline32_prepare.inline_leaf_children
        << ",type_inline32_leaf_operations="
        << type_inline32_prepare.inline_leaf_operations
        << ",type_inline32_children="
        << type_inline32_prepare.children
        << ",type_inline32_object_major_api_applications="
        << type_inline32_object_major_execute.api_applications
        << ",type_inline32_object_major_child_visits="
        << type_inline32_object_major_execute.child_visits
        << ",type_inline32_object_major_repeat_visits="
        << type_inline32_object_major_execute.repeat_visits
        << ",type_inline32_object_major_repeat_iterations="
        << type_inline32_object_major_execute.repeat_iterations

        << ",type_inline64_resident_bytes="
        << type_inline64_prepare.resident_bytes
        << ",type_inline64_leaf_limit="
        << type_inline64_prepare.inline_leaf_limit
        << ",type_inline64_leaf_children="
        << type_inline64_prepare.inline_leaf_children
        << ",type_inline64_leaf_operations="
        << type_inline64_prepare.inline_leaf_operations
        << ",type_inline64_children="
        << type_inline64_prepare.children
        << ",type_inline64_object_major_api_applications="
        << type_inline64_object_major_execute.api_applications
        << ",type_inline64_object_major_child_visits="
        << type_inline64_object_major_execute.child_visits
        << ",type_inline64_object_major_repeat_visits="
        << type_inline64_object_major_execute.repeat_visits
        << ",type_inline64_object_major_repeat_iterations="
        << type_inline64_object_major_execute.repeat_iterations
        << ",mismatches=0"

        << '\n';

    return 0;
}
