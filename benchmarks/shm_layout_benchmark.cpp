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
            sparse_execute.action_visits) {

        std::cerr
            << "Pretouched sparse SHM execution mismatch"
            << ",reference_expected=" << expected_reference_writes
            << ",reference_actual=" << sparse_pretouched_execute.reference_writes
            << ",store_expected=" << materialization.scalar_writes
            << ",store_actual=" << sparse_pretouched_execute.store_writes
            << ",action_visits_expected=" << sparse_execute.action_visits
            << ",action_visits_actual=" << sparse_pretouched_execute.action_visits
            << '\n';

        return 7;
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
        << ",mismatches=0"
        << '\n';

    return 0;
}
