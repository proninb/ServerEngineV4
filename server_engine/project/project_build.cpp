#include "project_build.hpp"

#include "project_lifecycle_context.hpp"
#include "project_rebuild.hpp"
#include "project_path.hpp"
#include "runtime/project_runtime.hpp"
#include "project_configuration_loader.hpp"
#include "construction/execution_lanes.hpp"
#include "assign/assign_input.hpp"
#include "frontend/source_discovery.hpp"
#include "parser/parser.hpp"
#include "project_configuration_manifest_store.hpp"
#include "persistence/project_artifact.hpp"
#include "persistence/compiled_project_build.hpp"
#include "../writable_file_mapping.hpp"
#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cw::server {
namespace {

using build_clock = std::chrono::steady_clock;

[[nodiscard]] std::uint64_t elapsed_ns(
    build_clock::time_point begin,
    build_clock::time_point end) noexcept {

    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - begin).count());
}

class build_telemetry_scope final {
public:
    explicit build_telemetry_scope(
        project_build_telemetry* value) noexcept
        : telemetry(value),
          begin(build_clock::now()) {
    }

    ~build_telemetry_scope() {
        if (telemetry != nullptr) {
            telemetry->total_ns =
                elapsed_ns(
                    begin,
                    build_clock::now());
        }
    }

private:
    project_build_telemetry* telemetry = nullptr;
    build_clock::time_point begin;
};

struct build_candidate_paths final {
    std::filesystem::path compiled;
    std::filesystem::path source;
    std::filesystem::path database;
    std::filesystem::path manifest;
};

[[nodiscard]] bool make_build_candidate_paths(
    const project_artifact_layout& layout,
    build_candidate_paths& output) noexcept {

    output = {};

    try {
        output.compiled =
            layout.compiled;

        output.source =
            layout.source_save;

        output.database =
            layout.database;

        output.manifest =
            layout.manifest;

        output.compiled +=
            ".build-new";

        output.source +=
            ".build-new";

        output.database +=
            ".build-new";

        output.manifest +=
            ".build-new";

        const std::array<
            const std::filesystem::path*,
            8>
            paths{
                &layout.compiled,
                &layout.source_save,
                &layout.database,
                &layout.manifest,
                &output.compiled,
                &output.source,
                &output.database,
                &output.manifest,
            };

        for (std::size_t left = 0;
             left < paths.size();
             ++left) {

            if (paths[left] == nullptr ||
                paths[left]->empty()) {

                output = {};
                return false;
            }

            for (std::size_t right =
                     left + 1;
                 right < paths.size();
                 ++right) {

                if (*paths[left] ==
                    *paths[right]) {

                    output = {};
                    return false;
                }
            }
        }

        return true;
    }
    catch (...) {
        output = {};
        return false;
    }
}

void remove_build_candidate(
    const std::filesystem::path& path) noexcept {

    if (path.empty()) {
        return;
    }

    try {
        std::error_code error;

        (void)std::filesystem::remove(
            path,
            error);
    }
    catch (...) {
    }
}

void remove_build_candidates(
    const build_candidate_paths& paths) noexcept {

    remove_build_candidate(
        paths.compiled);

    remove_build_candidate(
        paths.source);

    remove_build_candidate(
        paths.database);

    remove_build_candidate(
        paths.manifest);
}

class build_candidate_cleanup final {
public:
    explicit build_candidate_cleanup(
        const build_candidate_paths& value) noexcept
        : paths(&value) {
    }

    ~build_candidate_cleanup() {
        if (paths != nullptr) {
            remove_build_candidates(
                *paths);
        }
    }

    build_candidate_cleanup(
        const build_candidate_cleanup&) = delete;

    build_candidate_cleanup& operator=(
        const build_candidate_cleanup&) = delete;

    void release() noexcept {
        paths = nullptr;
    }

private:
    const build_candidate_paths* paths = nullptr;
};

enum class build_artifact_promotion_result : std::uint8_t {
    success,
    invalid_input,
    io_failed,
    rollback_failed,
};

struct build_artifact_promotion_entry final {
    const std::filesystem::path* final_path = nullptr;
    const std::filesystem::path* candidate_path = nullptr;
    std::filesystem::path rollback_path;
    bool enabled = false;
    bool old_moved = false;
    bool candidate_moved = false;
};

[[nodiscard]] bool restore_build_artifacts(
    std::array<build_artifact_promotion_entry, 4>& entries) noexcept {

    bool restored = true;

    for (std::size_t offset = 0;
         offset < entries.size();
         ++offset) {

        auto& entry =
            entries[
                entries.size() -
                1 -
                offset];

        if (!entry.enabled) {
            continue;
        }

        if (entry.candidate_moved) {
            std::error_code error;

            std::filesystem::rename(
                *entry.final_path,
                *entry.candidate_path,
                error);

            if (error) {
                error.clear();

                (void)std::filesystem::remove(
                    *entry.final_path,
                    error);

                if (error) {
                    restored = false;
                }
            }

            entry.candidate_moved = false;
        }

        if (entry.old_moved) {
            std::error_code error;

            std::filesystem::rename(
                entry.rollback_path,
                *entry.final_path,
                error);

            if (error) {
                restored = false;
            }
            else {
                entry.old_moved = false;
            }
        }
    }

    return restored;
}

[[nodiscard]] build_artifact_promotion_result
promote_build_artifacts(
    const project_artifact_layout& layout,
    const build_candidate_paths& candidates,
    bool compiled_candidate,
    bool database_candidate,
    bool manifest_candidate) noexcept {

    std::array<
        build_artifact_promotion_entry,
        4>
        entries{{
            {
                &layout.compiled,
                &candidates.compiled,
                {},
                compiled_candidate,
            },
            {
                &layout.source_save,
                &candidates.source,
                {},
                true,
            },
            {
                &layout.database,
                &candidates.database,
                {},
                database_candidate,
            },
            {
                &layout.manifest,
                &candidates.manifest,
                {},
                manifest_candidate,
            },
        }};

    try {
        for (auto& entry :
             entries) {

            if (!entry.enabled ||
                entry.final_path == nullptr ||
                entry.candidate_path == nullptr ||
                entry.final_path->empty() ||
                entry.candidate_path->empty() ||
                *entry.final_path ==
                    *entry.candidate_path) {

                if (entry.enabled) {
                    return build_artifact_promotion_result::
                        invalid_input;
                }

                continue;
            }

            entry.rollback_path =
                *entry.final_path;

            entry.rollback_path +=
                ".build-old";

            if (entry.rollback_path.empty() ||
                entry.rollback_path ==
                    *entry.final_path ||
                entry.rollback_path ==
                    *entry.candidate_path) {

                return build_artifact_promotion_result::
                    invalid_input;
            }
        }

        for (std::size_t left = 0;
             left < entries.size();
             ++left) {

            if (!entries[left].enabled) {
                continue;
            }

            const std::array<
                const std::filesystem::path*,
                3>
                left_paths{
                    entries[left].final_path,
                    entries[left].candidate_path,
                    &entries[left].rollback_path,
                };

            for (std::size_t right =
                     left + 1;
                 right < entries.size();
                 ++right) {

                if (!entries[right].enabled) {
                    continue;
                }

                const std::array<
                    const std::filesystem::path*,
                    3>
                    right_paths{
                        entries[right].final_path,
                        entries[right].candidate_path,
                        &entries[right].rollback_path,
                    };

                for (const auto* left_path :
                     left_paths) {

                    for (const auto* right_path :
                         right_paths) {

                        if (*left_path ==
                            *right_path) {

                            return build_artifact_promotion_result::
                                invalid_input;
                        }
                    }
                }
            }
        }

        for (auto& entry :
             entries) {

            if (!entry.enabled) {
                continue;
            }

            std::error_code error;

            const auto final_exists =
                std::filesystem::is_regular_file(
                    *entry.final_path,
                    error);

            if (error ||
                !final_exists) {

                return build_artifact_promotion_result::
                    io_failed;
            }

            error.clear();

            const auto candidate_exists =
                std::filesystem::is_regular_file(
                    *entry.candidate_path,
                    error);

            if (error ||
                !candidate_exists) {

                return build_artifact_promotion_result::
                    io_failed;
            }

            error.clear();

            (void)std::filesystem::remove(
                entry.rollback_path,
                error);

            if (error) {
                return build_artifact_promotion_result::
                    io_failed;
            }
        }

        for (auto& entry :
             entries) {

            if (!entry.enabled) {
                continue;
            }

            std::error_code error;

            std::filesystem::rename(
                *entry.final_path,
                entry.rollback_path,
                error);

            if (error) {
                return restore_build_artifacts(
                    entries)
                    ? build_artifact_promotion_result::
                        io_failed
                    : build_artifact_promotion_result::
                        rollback_failed;
            }

            entry.old_moved = true;
        }

        for (auto& entry :
             entries) {

            if (!entry.enabled) {
                continue;
            }

            std::error_code error;

            std::filesystem::rename(
                *entry.candidate_path,
                *entry.final_path,
                error);

            if (error) {
                return restore_build_artifacts(
                    entries)
                    ? build_artifact_promotion_result::
                        io_failed
                    : build_artifact_promotion_result::
                        rollback_failed;
            }

            entry.candidate_moved = true;
        }

        // This is the BUILD persisted-state commit point. From here the complete
        // new artifact set is visible. Rollback files are cleanup-only.
        for (auto& entry :
             entries) {

            if (!entry.enabled) {
                continue;
            }

            std::error_code error;

            (void)std::filesystem::remove(
                entry.rollback_path,
                error);
        }

        return build_artifact_promotion_result::
            success;
    }
    catch (...) {
        return restore_build_artifacts(
            entries)
            ? build_artifact_promotion_result::
                io_failed
            : build_artifact_promotion_result::
                rollback_failed;
    }
}

[[nodiscard]] server_status emit_build_source_diagnostic(
    file_context& files,
    file_id file,
    source_range range,
    const diagnostic_descriptor& descriptor,
    std::string_view detail,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (!file ||
        !files.contains(file) ||
        !files.content_available(file)) {

        return server_status::success;
    }

    try {
        const auto path_view =
            files.path(file);

        const std::filesystem::path path{
            path_view.begin(),
            path_view.end()};

        const auto source =
            files.content(file);

        const auto diagnostic_file =
            diagnostics.add_source(
                path,
                std::string{
                    source.data(),
                    source.size()});

        const auto location =
            diagnostics.locate(
                diagnostic_file,
                range.offset,
                range.length);

        diagnostics.emit(
            diagnostic(
                descriptor,
                operation)
                .location(location)
                .detail(detail)
                .build());

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

[[nodiscard]] server_status emit_build_parser_warnings(
    file_context& files,
    const std::vector<parser_warning>& warnings,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    for (const auto& warning :
         warnings) {

        const diagnostic_descriptor* descriptor =
            nullptr;

        switch (warning.kind) {
        case parser_warning_kind::
                duplicate_initialization:
            descriptor =
                &diagnostics::
                    project_duplicate_initialization;
            break;
        }

        if (descriptor == nullptr) {
            return server_status::
                project_configuration_invalid;
        }

        const auto emitted =
            emit_build_source_diagnostic(
                files,
                warning.file,
                warning.source,
                *descriptor,
                warning.detail,
                operation,
                diagnostics);

        if (!succeeded(emitted)) {
            return emitted;
        }
    }

    return server_status::success;
}


[[nodiscard]] server_status report_manifest_open(
    read_only_file_mapping_result result,
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (result == read_only_file_mapping_result::failed) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_manifest_io_failed,
                operation)
                .file(path)
                .detail("Cannot memory-map project.manifest")
                .build());

        return server_status::io_error;
    }

    diagnostics.emit(
        diagnostic(
            result == read_only_file_mapping_result::not_found
                ? diagnostics::project_manifest_missing
                : diagnostics::project_manifest_invalid,
            operation)
            .file(path)
            .detail(
                result == read_only_file_mapping_result::not_found
                    ? "BUILD requires project.manifest; REBUILD is required when persisted BUILD state is missing"
                    : "Persisted project.manifest is empty")
            .build());

    return server_status::project_artifact_invalid;
}

[[nodiscard]] server_status report_source_open(
    read_only_file_mapping_result result,
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (result == read_only_file_mapping_result::failed) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_source_save_io_failed,
                operation)
                .file(path)
                .detail("Cannot memory-map source.bin")
                .build());

        return server_status::io_error;
    }

    diagnostics.emit(
        diagnostic(
            diagnostics::project_source_save_invalid,
            operation)
            .file(path)
            .detail(
                result == read_only_file_mapping_result::not_found
                    ? "BUILD requires source.bin; REBUILD is required when persisted BUILD state is missing"
                    : "Persisted source.bin is empty")
            .build());

    return server_status::project_artifact_invalid;
}

[[nodiscard]] server_status report_database_open(
    read_only_file_mapping_result result,
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (result == read_only_file_mapping_result::failed) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_database_io_failed,
                operation)
                .file(path)
                .detail("Cannot memory-map database.bin")
                .build());

        return server_status::io_error;
    }

    diagnostics.emit(
        diagnostic(
            diagnostics::project_database_invalid,
            operation)
            .file(path)
            .detail(
                result == read_only_file_mapping_result::not_found
                    ? "BUILD requires database.bin when affected files need retained lexical state; REBUILD is required when it is missing"
                    : "Persisted database.bin is empty")
            .build());

    return server_status::project_artifact_invalid;
}

[[nodiscard]] server_status report_compiled_open(
    read_only_file_mapping_result result,
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (result == read_only_file_mapping_result::failed) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_io_failed,
                operation)
                .file(path)
                .detail("Cannot memory-map compiled.bin for BUILD baseline")
                .build());

        return server_status::io_error;
    }

    diagnostics.emit(
        diagnostic(
            diagnostics::project_compiled_invalid,
            operation)
            .file(path)
            .detail(
                result == read_only_file_mapping_result::not_found
                    ? "BUILD requires compiled.bin as string/identity/G baseline; REBUILD is required when it is missing"
                    : "Persisted compiled.bin is empty")
            .build());

    return server_status::project_artifact_invalid;
}

[[nodiscard]] constexpr bool file_id_less(
    file_id left,
    file_id right) noexcept {

    return left.value() <
        right.value();
}

void normalize_file_ids(
    std::vector<file_id>& values) {

    std::sort(
        values.begin(),
        values.end(),
        file_id_less);

    values.erase(
        std::unique(
            values.begin(),
            values.end()),
        values.end());
}

[[nodiscard]] bool contains_file_id(
    std::span<const file_id> values,
    file_id value) noexcept {

    return std::binary_search(
        values.begin(),
        values.end(),
        value,
        file_id_less);
}


class build_direct_compiled_guard final {
public:
    explicit build_direct_compiled_guard(
        const project_artifact_layout& value) noexcept
        : layout(&value) {
    }

    ~build_direct_compiled_guard() {
        if (!mutated ||
            released ||
            layout == nullptr) {

            return;
        }

        remove_build_candidate(
            layout->source_save);

        remove_build_candidate(
            layout->database);

        remove_build_candidate(
            layout->manifest);
    }

    void mark_mutated() noexcept {
        mutated = true;
    }

    void release() noexcept {
        released = true;
    }

private:
    const project_artifact_layout* layout = nullptr;
    bool mutated = false;
    bool released = false;
};

struct direct_object_write_state final {
    writable_file_mapping* mapping = nullptr;
    compiled_project_view* compiled = nullptr;
    const string_table* strings = nullptr;
    const identity_space* identities = nullptr;
    build_direct_compiled_guard* guard = nullptr;
    bool supported = true;
    bool mutated = false;
};

[[nodiscard]] server_status direct_object_write_sink(
    void* opaque,
    const graph_delta& graph,
    const graph_delta_object_change& change) noexcept {

    auto& state =
        *static_cast<
            direct_object_write_state*>(
                opaque);

    if (!state.supported) {
        return server_status::success;
    }

    if (state.mapping == nullptr ||
        state.compiled == nullptr ||
        state.strings == nullptr ||
        state.identities == nullptr ||
        state.guard == nullptr ||
        !state.mapping->valid() ||
        change.kind !=
            graph_delta_change_kind::patch ||
        graph.appended_type_count() != 0 ||
        graph.appended_object_count() != 0 ||
        graph.appended_link_count() != 0 ||
        !graph.member_entries().empty() ||
        !graph.base_entries().empty() ||
        !graph.derived_type_entries().empty() ||
        !graph.endpoint_path_entries().empty() ||
        !graph.endpoint_path_step_entries().empty() ||
        state.strings->size() !=
            state.compiled->string_count() ||
        state.identities->size() !=
            state.compiled->identity_count()) {

        state.supported = false;
        return server_status::success;
    }

    const auto applied =
        apply_compiled_project_graph_object_write(
            graph,
            change,
            state.mapping->bytes());

    if (applied !=
        compiled_project_image_result::
            success) {

        return applied ==
                compiled_project_image_result::
                    failed
            ? server_status::io_error
            : server_status::
                project_artifact_invalid;
    }

    if (state.compiled->bind(
            state.mapping->bytes()) !=
        compiled_project_image_result::
            success) {

        return server_status::
            project_artifact_invalid;
    }

    state.mutated = true;
    state.guard->mark_mutated();

    return server_status::success;
}

[[nodiscard]] bool compiled_source_delta_unchanged(
    const compiled_project_view& baseline,
    const source_map_delta& delta,
    std::span<const file_id> invalidated_roots) noexcept {

    const auto roots =
        delta.root_entries();

    if (roots.size() !=
        invalidated_roots.size()) {

        return false;
    }

    const auto contributions =
        delta.contribution_entries();

    for (const auto& root :
         roots) {

        if (!contains_file_id(
                invalidated_roots,
                root.root) ||
            root.contributions.begin >
                contributions.size() ||
            root.contributions.count >
                contributions.size() -
                    root.contributions.begin) {

            return false;
        }

        source_map_range persisted;

        if (!baseline.source_root(
                root.root,
                persisted) ||
            persisted.count !=
                root.contributions.count) {

            return false;
        }

        for (std::uint32_t index = 0;
             index <
                root.contributions.count;
             ++index) {

            source_contribution_record
                old_value;

            const auto current =
                contributions[
                    static_cast<std::size_t>(
                        root.contributions.begin) +
                    index];

            if (!baseline.source_contribution(
                    persisted.begin + index,
                    old_value) ||
                old_value.file !=
                    current.file ||
                old_value.data !=
                    current.data) {

                return false;
            }
        }
    }

    return true;
}

void append_file_id_difference(
    const std::vector<file_id>& left,
    const std::vector<file_id>& right,
    std::vector<file_id>& output) {

    std::size_t left_index = 0;
    std::size_t right_index = 0;

    while (left_index < left.size()) {
        while (right_index < right.size() &&
               right[right_index].value() <
                   left[left_index].value()) {

            ++right_index;
        }

        if (right_index >= right.size() ||
            left[left_index] !=
                right[right_index]) {

            output.push_back(
                left[left_index]);
        }

        ++left_index;
    }
}

enum class configuration_root_selection : std::uint8_t {
    semantic,
    assign,
};

[[nodiscard]] server_status collect_persisted_configuration_roots(
    const std::filesystem::path& root_project_path,
    const project_configuration_manifest& manifest,
    const source_save_view& source,
    configuration_root_selection selection,
    std::vector<file_id>& roots) noexcept {

    roots.clear();

    if (!source.valid() ||
        manifest.files.empty()) {

        return server_status::
            project_artifact_invalid;
    }

    std::filesystem::path root;

    if (resolve_project_path(
            root_project_path,
            root) !=
        project_path_result::success) {

        return server_status::io_error;
    }

    try {
        std::vector<std::filesystem::path>
            resolved_paths;

        resolved_paths.reserve(
            manifest.files.size());

        for (std::size_t index = 0;
             index < manifest.files.size();
             ++index) {

            const auto& proof =
                manifest.files[index];

            std::filesystem::path path;

            if (index == 0) {
                if (proof.declaring_file !=
                        invalid_configuration_file ||
                    proof.path_type !=
                        project_configuration_path_type::relative ||
                    proof.path !=
                        root.filename()) {

                    return server_status::
                        project_artifact_invalid;
                }

                path = root;
            } else {
                if (proof.declaring_file >=
                    index) {

                    return server_status::
                        project_artifact_invalid;
                }

                const auto& parent =
                    resolved_paths[
                        proof.declaring_file];

                const auto input =
                    proof.path_type ==
                        project_configuration_path_type::absolute
                    ? proof.path
                    : parent.parent_path() /
                        proof.path;

                if (resolve_project_path(
                        input,
                        path) !=
                    project_path_result::success) {

                    return server_status::io_error;
                }
            }

            resolved_paths.push_back(
                path);

            file_id project_file;

            const auto found =
                source.find_path(
                    path,
                    project_file);

            if (!succeeded(found)) {
                return found;
            }

            source_save_file_view
                project_state;

            if (!project_file ||
                !source.file(
                    project_file,
                    project_state) ||
                !project_state.current_member ||
                project_state.kind !=
                    file_kind::project) {

                return server_status::
                    project_artifact_invalid;
            }

            for (const auto dependency :
                 project_state.dependencies) {

                source_save_file_view
                    dependency_state;

                if (!source.file(
                        dependency,
                        dependency_state) ||
                    !dependency_state.current_member) {

                    return server_status::
                        project_artifact_invalid;
                }

                const auto selected =
                    selection ==
                        configuration_root_selection::
                            semantic
                    ? dependency_state.kind ==
                            file_kind::header ||
                      dependency_state.kind ==
                            file_kind::source
                    : dependency_state.kind ==
                        file_kind::assign;

                if (selected) {
                    roots.push_back(
                        dependency);
                }
            }
        }

        normalize_file_ids(
            roots);

        return server_status::success;
    }
    catch (...) {
        roots.clear();
        return server_status::io_error;
    }
}

struct build_initialization_invalidation_metrics final {
    std::size_t visited_roots = 0;
    std::uint64_t producer_targets = 0;
    std::size_t tombstones = 0;
};

[[nodiscard]] server_status invalidate_persisted_initializations(
    const source_save_view& persisted,
    graph_delta& changes,
    const std::vector<file_id>& roots,
    build_initialization_invalidation_metrics* metrics = nullptr) noexcept {

    if (metrics != nullptr) {
        *metrics = {};
    }

    if (!persisted.valid() ||
        !changes.baseline_bound()) {

        return server_status::
            project_artifact_invalid;
    }

    build_initialization_invalidation_metrics
        local;

    local.visited_roots =
        roots.size();

    const auto before =
        changes.initialization_count();

    for (const auto root : roots) {
        std::size_t count = 0;

        if (!persisted.initialization_targets(
                root,
                count)) {

            return server_status::
                project_artifact_invalid;
        }

        if (count >
            (std::numeric_limits<
                std::uint64_t>::max)() -
                local.producer_targets) {

            return server_status::
                project_artifact_invalid;
        }

        local.producer_targets +=
            static_cast<std::uint64_t>(
                count);

        for (std::size_t index = 0;
             index < count;
             ++index) {

            object_endpoint target;

            if (!persisted.initialization_target(
                    root,
                    index,
                    target)) {

                return server_status::
                    project_artifact_invalid;
            }

            const auto invalidated =
                changes.invalidate_initialization(
                    target);

            if (!succeeded(invalidated)) {
                return invalidated;
            }
        }
    }

    const auto after =
        changes.initialization_count();

    if (after > before) {
        return server_status::
            project_artifact_invalid;
    }

    local.tombstones =
        before - after;

    if (metrics != nullptr) {
        *metrics = local;
    }

    return server_status::success;
}


[[nodiscard]] server_status load_root_preprocessor_configuration(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    preprocessor_configuration& output) {

    output = {};

    std::filesystem::path root;

    if (resolve_project_path(
            project_path,
            root) !=
        project_path_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_configuration_read_failed,
                operation)
                .file(project_path)
                .detail(
                    "Cannot resolve root Project configuration path for BUILD semantic replay")
                .build());

        return server_status::io_error;
    }

    file_content_snapshot snapshot;

    const auto acquired =
        acquire_file_content(
            root,
            snapshot);

    if (acquired !=
        file_content_result::acquired) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_configuration_read_failed,
                operation)
                .file(root)
                .detail(
                    acquired ==
                            file_content_result::missing
                        ? "Root Project configuration is missing during BUILD semantic replay"
                        : acquired ==
                                file_content_result::changed_during_read
                            ? "Root Project configuration changed during BUILD semantic replay acquisition"
                            : acquired ==
                                    file_content_result::allocation_failed
                                ? "Cannot allocate root Project configuration snapshot for BUILD semantic replay"
                                : "Cannot acquire root Project configuration for BUILD semantic replay")
                .build());

        return acquired ==
                file_content_result::allocation_failed
            ? server_status::io_error
            : server_status::
                project_configuration_invalid;
    }

    std::vector<project_configuration_dependency>
        dependencies;

    return read_project_configuration(
        snapshot.bytes,
        root,
        operation,
        diagnostics,
        dependencies,
        project_configuration_scope::root,
        &output);
}


}

server_status build_project(
    const std::filesystem::path& project_path,
    const server_settings_configuration& settings,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output,
    project_build_telemetry* telemetry) {

    output.reset();

    if (telemetry != nullptr) {
        *telemetry = {};
    }

    build_telemetry_scope
        telemetry_scope{
            telemetry};

    build_context context{
        settings};

    project_artifact_layout layout;

    if (make_project_artifact_layout(
            project_path,
            context.settings.files,
            layout) != project_artifact_layout_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_build_incomplete,
                operation)
                .file(project_path)
                .detail("Cannot construct Project artifact layout")
                .build());

        return server_status::io_error;
    }

    const auto source_opened =
        context.source_mapping.open(
            layout.source_save);

    if (source_opened ==
        read_only_file_mapping_result::not_found) {

        if (telemetry != nullptr) {
            telemetry->rebuild_fallback = true;
        }

        return rebuild_project(
            project_path,
            settings,
            operation,
            diagnostics,
            output);
    }

    if (source_opened !=
        read_only_file_mapping_result::success) {

        return report_source_open(
            source_opened,
            layout.source_save,
            operation,
            diagnostics);
    }

    const auto manifest_opened =
        context.manifest_mapping.open(
            layout.manifest);

    if (manifest_opened !=
        read_only_file_mapping_result::success) {

        return report_manifest_open(
            manifest_opened,
            layout.manifest,
            operation,
            diagnostics);
    }

    const auto manifest_decoded =
        decode_project_configuration_manifest(
            context.manifest_mapping.bytes(),
            context.manifest);

    if (manifest_decoded !=
        project_configuration_manifest_store_result::success) {

        diagnostics.emit(
            diagnostic(
                manifest_decoded ==
                        project_configuration_manifest_store_result::io_failed
                    ? diagnostics::project_manifest_io_failed
                    : diagnostics::project_manifest_invalid,
                operation)
                .file(layout.manifest)
                .detail(
                    "Committed Project configuration manifest could not be decoded")
                .build());

        return manifest_decoded ==
                project_configuration_manifest_store_result::io_failed
            ? server_status::io_error
            : server_status::project_artifact_invalid;
    }

    project_configuration_manifest_verification verification =
        project_configuration_manifest_verification::changed;

    const auto verified =
        verify_project_configuration_manifest(
            project_path,
            context.manifest,
            operation,
            diagnostics,
            verification);

    if (!succeeded(verified)) {
        return verified;
    }

    const bool configuration_probe_changed =
        verification !=
            project_configuration_manifest_verification::
                unchanged;

    if (context.source.bind(
            context.source_mapping.bytes()) !=
        source_save_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "Committed source.bin failed structural binding")
                .build());

        return server_status::project_artifact_invalid;
    }

    const auto files_bound =
        context.files.bind_baseline(
            context.source);

    if (!succeeded(files_bound)) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "Committed source.bin could not initialize mmap-backed BUILD File Context")
                .build());

        return files_bound;
    }

    project_configuration_manifest
        committed_manifest;

    std::vector<file_id>
        old_configuration_roots;

    std::vector<file_id>
        current_configuration_roots;

    std::vector<file_id>
        old_assign_roots;

    std::vector<file_id>
        current_assign_roots;

    bool configuration_identity_changed = false;
    bool preprocessor_changed = false;
    bool preprocessor_loaded = false;

    if (configuration_probe_changed) {
        committed_manifest =
            std::move(
                context.manifest);

        const auto composed =
            compose_project_configuration(
                project_path,
                operation,
                diagnostics,
                context.manifest,
                context.files,
                context.preprocessor,
                &current_configuration_roots,
                &current_assign_roots);

        if (!succeeded(composed)) {
            return composed;
        }

        preprocessor_loaded = true;

        configuration_identity_changed =
            !(context.manifest.configuration_hash ==
              committed_manifest.configuration_hash);

        if (configuration_identity_changed) {
            const auto old_roots_collected =
                collect_persisted_configuration_roots(
                    project_path,
                    committed_manifest,
                    context.source,
                    configuration_root_selection::
                        semantic,
                    old_configuration_roots);

            if (!succeeded(
                    old_roots_collected)) {

                diagnostics.emit(
                    diagnostic(
                        diagnostics::project_source_save_invalid,
                        operation)
                        .file(layout.source_save)
                        .detail(
                            "Committed Project composition could not recover OLD semantic roots from SourceSave")
                        .build());

                return old_roots_collected;
            }

            const auto old_assign_collected =
                collect_persisted_configuration_roots(
                    project_path,
                    committed_manifest,
                    context.source,
                    configuration_root_selection::
                        assign,
                    old_assign_roots);

            if (!succeeded(
                    old_assign_collected)) {

                diagnostics.emit(
                    diagnostic(
                        diagnostics::
                            project_source_save_invalid,
                        operation)
                        .file(layout.source_save)
                        .detail(
                            "Committed Project composition could not recover OLD Assign roots from SourceSave")
                        .build());

                return old_assign_collected;
            }

            preprocessor_changed =
                !(context.manifest.preprocessor_hash ==
                  committed_manifest.preprocessor_hash);
        }
    }

    std::vector<file_id> candidates;
    source_save_change_scan scan;

    const auto scanned =
        scan_source_save_change_candidates(
            context.source,
            candidates,
            &scan);

    if (!succeeded(scanned)) {
        diagnostics.emit(
            diagnostic(
                scanned == server_status::io_error
                    ? diagnostics::project_source_save_io_failed
                    : diagnostics::project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "Physical change-candidate discovery over committed SourceSave failed")
                .build());

        return scanned;
    }

    std::vector<file_id> semantic_changed;
    source_save_change_classification_metrics
        classification;

    const auto classified =
        classify_source_save_changes(
            context.source,
            candidates,
            context.files,
            semantic_changed,
            &classification);

    if (telemetry != nullptr &&
        succeeded(classified)) {

        telemetry->candidate_files =
            static_cast<std::uint64_t>(
                candidates.size());

        telemetry->changed_files =
            static_cast<std::uint64_t>(
                semantic_changed.size());
    }

    if (!succeeded(classified)) {
        diagnostics.emit(
            diagnostic(
                classified == server_status::io_error
                    ? diagnostics::project_source_save_io_failed
                    : diagnostics::project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "Exact SourceSave candidate acquisition/classification failed")
                .build());

        return classified;
    }

    std::vector<file_id> affected;

    source_save_affected_metrics
        affected_metrics;

    const auto collected =
        collect_source_save_affected(
            context.source,
            semantic_changed,
            affected,
            &affected_metrics);

    if (telemetry != nullptr &&
        succeeded(collected)) {

        telemetry->affected_files =
            static_cast<std::uint64_t>(
                affected.size());
    }

    if (!succeeded(collected)) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "OLD reverse dependency topology failed while computing affected closure")
                .build());

        return collected;
    }

    std::vector<file_id>
        affected_semantic_roots;

    const auto roots_collected =
        collect_source_save_semantic_roots(
            context.source,
            affected,
            affected_semantic_roots);

    if (telemetry != nullptr &&
        succeeded(roots_collected)) {

        telemetry->affected_roots =
            static_cast<std::uint64_t>(
                affected_semantic_roots.size());
    }

    if (!succeeded(roots_collected)) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "OLD physical dependency topology failed while selecting affected semantic roots")
                .build());

        return roots_collected;
    }

    std::vector<file_id>
        semantic_invalidated_roots;

    std::vector<file_id>
        semantic_replay_roots;

    try {
        semantic_invalidated_roots =
            affected_semantic_roots;

        if (!configuration_identity_changed) {
            semantic_replay_roots =
                affected_semantic_roots;
        } else {
            semantic_replay_roots.reserve(
                affected_semantic_roots.size() +
                current_configuration_roots.size());

            for (const auto root :
                 affected_semantic_roots) {

                if (contains_file_id(
                        current_configuration_roots,
                        root)) {

                    semantic_replay_roots.push_back(
                        root);
                }
            }

            append_file_id_difference(
                old_configuration_roots,
                current_configuration_roots,
                semantic_invalidated_roots);

            append_file_id_difference(
                current_configuration_roots,
                old_configuration_roots,
                semantic_replay_roots);

            if (preprocessor_changed) {
                semantic_invalidated_roots.insert(
                    semantic_invalidated_roots.end(),
                    old_configuration_roots.begin(),
                    old_configuration_roots.end());

                semantic_replay_roots.insert(
                    semantic_replay_roots.end(),
                    current_configuration_roots.begin(),
                    current_configuration_roots.end());
            }

            normalize_file_ids(
                semantic_invalidated_roots);

            normalize_file_ids(
                semantic_replay_roots);
        }
    }
    catch (...) {
        return server_status::io_error;
    }

    std::vector<file_id>
        lexical_replacement_files;

    try {
        lexical_replacement_files =
            semantic_changed;

        if (configuration_identity_changed) {
            for (const auto root :
                 current_configuration_roots) {

                if (root.value() >
                    context.source.file_count()) {

                    lexical_replacement_files.push_back(
                        root);
                }
            }
        }

        normalize_file_ids(
            lexical_replacement_files);
    }
    catch (...) {
        return server_status::io_error;
    }

    std::vector<file_id>
        assign_replacement_roots;

    std::vector<file_id>
        assign_parse_roots;

    try {
        for (const auto file :
             semantic_changed) {

            if (context.files.kind(file) ==
                file_kind::assign) {

                assign_replacement_roots.push_back(
                    file);
            }
        }

        if (configuration_identity_changed) {
            append_file_id_difference(
                old_assign_roots,
                current_assign_roots,
                assign_replacement_roots);

            append_file_id_difference(
                current_assign_roots,
                old_assign_roots,
                assign_replacement_roots);
        }

        normalize_file_ids(
            assign_replacement_roots);

        for (const auto root :
             assign_replacement_roots) {

            const auto current =
                configuration_identity_changed
                ? contains_file_id(
                    current_assign_roots,
                    root)
                : true;

            if (!current) {
                continue;
            }

            if (!context.files.contains(root) ||
                context.files.kind(root) !=
                    file_kind::assign) {

                return server_status::
                    project_artifact_invalid;
            }

            assign_parse_roots.push_back(
                root);
        }
    }
    catch (...) {
        return server_status::io_error;
    }

    if (!semantic_replay_roots.empty() &&
        !preprocessor_loaded) {

        const auto preprocessor_loaded_status =
            load_root_preprocessor_configuration(
                project_path,
                operation,
                diagnostics,
                context.preprocessor);

        if (!succeeded(
                preprocessor_loaded_status)) {

            return preprocessor_loaded_status;
        }

        preprocessor_loaded = true;
    }

    const auto compiled_opened =
        context.compiled_mapping.open(
            layout.compiled);

    if (compiled_opened !=
        read_only_file_mapping_result::success) {

        return report_compiled_open(
            compiled_opened,
            layout.compiled,
            operation,
            diagnostics);
    }

    if (context.compiled.bind(
            context.compiled_mapping.bytes()) !=
        compiled_project_image_result::success ||
        context.compiled.source_file_count() != context.source.file_count() ||
        context.compiled.type_count() != context.source.type_presence_count() ||
        context.compiled.object_count() != context.source.object_presence_count() ||
        context.compiled.link_count() != context.source.link_presence_count()) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_invalid,
                operation)
                .file(layout.compiled)
                .detail(
                    "Committed compiled.bin failed structural BUILD-baseline binding or SourceSave presence cardinalities disagree")
                .build());

        return server_status::project_artifact_invalid;
    }

    // RUNTIME-BIN-02 scope boundary: only PUBLISH and LOAD are developed
    // in this slice. A semantic-changing BUILD must not mutate final G
    // until runtime.bin BUILD production/promotion is designed separately.
    if (!semantic_changed.empty() ||
        configuration_identity_changed) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_build_incomplete,
                operation)
                .file(layout.compiled)
                .detail(
                    "RUNTIME-BIN-02 supports PUBLISH/LOAD only; "
                    "semantic-changing BUILD is deferred")
                .build());

        return server_status::project_artifact_invalid;
    }

    if (semantic_changed.empty() &&
        !configuration_identity_changed) {

        // Exact no-change BUILD: REBUILD already produced this G and the
        // current inputs are byte-identical. No Graph reconstruction, Parser,
        // database.bin mapping, or graph_delta publication is necessary.
        const auto runtime_begin =
            build_clock::now();

        const auto status =
            create_resident_project(
                project_path,
                settings,
                operation,
                diagnostics,
                std::move(
                    context.compiled_mapping),
                context.compiled,
                output);

        if (telemetry != nullptr) {
            telemetry->runtime_publication_ns =
                elapsed_ns(
                    runtime_begin,
                    build_clock::now());
        }

        return status;
    }

    const auto sparse_reconstruction_begin =
        build_clock::now();

    if (!assign_parse_roots.empty()) {
        const auto materialized =
            materialize_assign_inputs(
                context.files,
                assign_parse_roots);

        if (!succeeded(materialized)) {
            diagnostics.emit(
                diagnostic(
                    diagnostics::
                        project_assign_invalid,
                    operation)
                    .detail(
                        "BUILD could not materialize selected Assign replacement inputs")
                    .build());

            return materialized;
        }

        assign_parse_failure
            assign_failure;

        const auto parsed =
            parse_assign_inputs(
                context.files,
                assign_parse_roots,
                context.assign_changes,
                &assign_failure);

        if (!succeeded(parsed)) {
            if (assign_failure.file) {
                const source_range range{
                    assign_failure.offset,
                    assign_failure.length,
                };

                const auto emitted =
                    emit_build_source_diagnostic(
                        context.files,
                        assign_failure.file,
                        range,
                        diagnostics::
                            project_assign_invalid,
                        assign_failure.detail.empty()
                            ? std::string_view{
                                "Assign input is invalid"}
                            : assign_failure.detail,
                        operation,
                        diagnostics);

                if (!succeeded(emitted)) {
                    return emitted;
                }
            }

            return parsed;
        }
    }
    else {
        context.assign_changes.clear();
    }

    context.compiled.reset();
    context.compiled_mapping.reset();

    if (context.compiled_write_mapping.
            open_existing(
                layout.compiled) !=
            writable_file_mapping_result::
                success ||
        context.compiled.bind(
            context.compiled_write_mapping.
                bytes()) !=
            compiled_project_image_result::
                success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::
                    project_compiled_invalid,
                operation)
                .file(layout.compiled)
                .detail(
                    "BUILD could not reopen final compiled.bin as a writable mmap baseline")
                .build());

        return server_status::
            project_artifact_invalid;
    }

    build_direct_compiled_guard
        direct_compiled_guard{
            layout};

    const auto graph_bound =
        context.graph_changes.bind_baseline(
            context.compiled);

    if (!succeeded(graph_bound)) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_invalid,
                operation)
                .file(layout.compiled)
                .detail(
                    "Committed compiled.bin could not initialize BUILD-local sparse graph_delta")
                .build());

        return graph_bound;
    }

    std::vector<file_id>
        semantic_dependency_roots;

    source_save_semantic_dependency_metrics
        semantic_dependency_metrics;

    const auto semantic_dependency_collected =
        collect_source_save_semantic_dependency_closure(
            context.source,
            context.compiled,
            semantic_invalidated_roots,
            semantic_dependency_roots,
            &semantic_dependency_metrics);

    if (!succeeded(
            semantic_dependency_collected)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "Persisted semantic dependency topology failed while expanding OLD invalidated roots")
                .build());

        return semantic_dependency_collected;
    }

    semantic_invalidated_roots =
        semantic_dependency_roots;

    build_initialization_invalidation_metrics
        initialization_invalidation_metrics;

    const auto initialization_invalidated =
        invalidate_persisted_initializations(
            context.source,
            context.graph_changes,
            semantic_invalidated_roots,
            &initialization_invalidation_metrics);

    if (!succeeded(
            initialization_invalidated)) {

        if (initialization_invalidated ==
            server_status::io_error) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::
                        project_build_incomplete,
                    operation)
                    .file(layout.compiled)
                    .detail(
                        "BUILD could not allocate sparse object-initialization tombstones")
                    .build());

            return initialization_invalidated;
        }

        diagnostics.emit(
            diagnostic(
                diagnostics::
                    project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "Persisted source.bin initialization provenance disagrees with compiled.bin canonical initialization baseline")
                .build());

        return initialization_invalidated;
    }

    source_save_semantic_invalidation_plan
        semantic_invalidation_plan;

    source_save_semantic_invalidation_metrics
        semantic_invalidation_metrics;

    const auto semantic_invalidation_collected =
        collect_source_save_semantic_invalidation(
            context.source,
            context.compiled,
            semantic_invalidated_roots,
            semantic_invalidation_plan,
            &semantic_invalidation_metrics);

    if (telemetry != nullptr &&
        succeeded(
            semantic_invalidation_collected)) {

        telemetry->invalidated_roots =
            static_cast<std::uint64_t>(
                semantic_invalidated_roots.size());

        telemetry->replay_roots =
            static_cast<std::uint64_t>(
                semantic_replay_roots.size());

        telemetry->retire_types =
            static_cast<std::uint64_t>(
                semantic_invalidation_plan.
                    retire_types.size());

        telemetry->clear_type_definitions =
            static_cast<std::uint64_t>(
                semantic_invalidation_plan.
                    clear_type_definitions.size());

        telemetry->retire_objects =
            static_cast<std::uint64_t>(
                semantic_invalidation_plan.
                    retire_objects.size());

        telemetry->retire_links =
            static_cast<std::uint64_t>(
                semantic_invalidation_plan.
                    retire_links.size());
    }

    if (!succeeded(
            semantic_invalidation_collected)) {

        diagnostics.emit(
            diagnostic(
                semantic_invalidation_collected ==
                        server_status::io_error
                    ? diagnostics::
                        project_build_incomplete
                    : diagnostics::
                        project_source_save_invalid,
                operation)
                .file(
                    semantic_invalidation_collected ==
                            server_status::io_error
                        ? layout.compiled
                        : layout.source_save)
                .detail(
                    semantic_invalidation_collected ==
                            server_status::io_error
                        ? "BUILD could not allocate sparse semantic invalidation state"
                        : "Persisted semantic root ownership/presence disagrees with compiled.bin")
                .build());

        return semantic_invalidation_collected;
    }

    const auto apply_semantic_invalidation =
        [&]() -> server_status {

            for (const auto link :
                 semantic_invalidation_plan.
                     retire_links) {

                const auto status =
                    context.graph_changes.retire(
                        link);

                if (!succeeded(status)) {
                    return status;
                }
            }

            for (const auto object :
                 semantic_invalidation_plan.
                     retire_objects) {

                const auto status =
                    context.graph_changes.retire(
                        object);

                if (!succeeded(status)) {
                    return status;
                }
            }

            for (const auto type :
                 semantic_invalidation_plan.
                     clear_type_definitions) {

                const auto status =
                    context.graph_changes.
                        clear_definition(
                            type);

                if (!succeeded(status)) {
                    return status;
                }
            }

            for (const auto type :
                 semantic_invalidation_plan.
                     retire_types) {

                const auto status =
                    context.graph_changes.retire(
                        type);

                if (!succeeded(status)) {
                    return status;
                }
            }

            return server_status::success;
        };

    const auto semantic_invalidated =
        apply_semantic_invalidation();

    if (!succeeded(
            semantic_invalidated)) {

        diagnostics.emit(
            diagnostic(
                semantic_invalidated ==
                        server_status::io_error
                    ? diagnostics::
                        project_build_incomplete
                    : diagnostics::
                        project_compiled_invalid,
                operation)
                .file(layout.compiled)
                .detail(
                    semantic_invalidated ==
                            server_status::io_error
                        ? "BUILD could not materialize sparse Graph semantic tombstones"
                        : "compiled.bin semantic baseline could not apply SourceSave invalidation plan")
                .build());

        return semantic_invalidated;
    }

    direct_object_write_state
        direct_object_state{
            &context.compiled_write_mapping,
            &context.compiled,
            &context.strings,
            &context.identities,
            &direct_compiled_guard,
        };

    compiled_project_graph_write_plan
        invalidation_write_plan;

    const auto invalidation_plan_prepared =
        prepare_compiled_project_graph_write_plan(
            context.graph_changes,
            invalidation_write_plan);

    if (invalidation_plan_prepared !=
            compiled_project_image_result::
                success ||
        invalidation_write_plan.
            type_patch_count != 0 ||
        invalidation_write_plan.
            link_patch_count != 0 ||
        invalidation_write_plan.
            initialization_change_count != 0 ||
        invalidation_write_plan.
            appended_types.count != 0 ||
        invalidation_write_plan.
            appended_members.count != 0 ||
        invalidation_write_plan.
            appended_bases.count != 0 ||
        invalidation_write_plan.
            appended_objects.count != 0 ||
        invalidation_write_plan.
            appended_links.count != 0 ||
        invalidation_write_plan.
            appended_derived_types.count != 0 ||
        invalidation_write_plan.
            appended_endpoint_paths.count != 0 ||
        invalidation_write_plan.
            appended_endpoint_path_steps.count != 0) {

        direct_object_state.supported =
            false;
    }
    else if (invalidation_write_plan.
                 object_patch_count != 0) {

        const auto applied =
            apply_compiled_project_graph_fixed_writes(
                context.graph_changes,
                context.compiled_write_mapping.
                    bytes());

        if (applied !=
                compiled_project_image_result::
                    success ||
            context.compiled.bind(
                context.compiled_write_mapping.
                    bytes()) !=
                compiled_project_image_result::
                    success) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::
                        project_compiled_invalid,
                    operation)
                    .file(layout.compiled)
                    .detail(
                        "BUILD could not apply valid object invalidation directly to final compiled.bin")
                    .build());

            return server_status::
                project_artifact_invalid;
        }

        direct_object_state.mutated = true;
        direct_compiled_guard.mark_mutated();
    }

    try {
        for (const auto root :
             semantic_dependency_roots) {

            if (!configuration_identity_changed ||
                contains_file_id(
                    current_configuration_roots,
                    root)) {

                semantic_replay_roots.push_back(
                    root);
            }
        }

        normalize_file_ids(
            semantic_replay_roots);
    }
    catch (...) {
        return server_status::io_error;
    }

    if (!semantic_replay_roots.empty() &&
        !preprocessor_loaded) {

        const auto preprocessor_loaded_status =
            load_root_preprocessor_configuration(
                project_path,
                operation,
                diagnostics,
                context.preprocessor);

        if (!succeeded(
                preprocessor_loaded_status)) {

            return preprocessor_loaded_status;
        }

        preprocessor_loaded = true;
    }

    if (!succeeded(
            context.strings.bind_baseline(
                context.compiled)) ||
        !succeeded(
            context.identities.bind_baseline(
                context.compiled))) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_invalid,
                operation)
                .file(layout.compiled)
                .detail(
                    "Committed compiled.bin could not initialize append-only BUILD string/identity overlays")
                .build());

        return server_status::project_artifact_invalid;
    }

    context.graph_changes.set_object_change_sink(
        &direct_object_state,
        direct_object_write_sink);

    bool database_bound = false;

    const auto database_cardinality_changed =
        context.files.size() !=
            context.source.file_count();

    if (!semantic_replay_roots.empty() ||
        database_cardinality_changed) {
        const auto database_opened =
            context.database_mapping.open(
                layout.database);

        if (database_opened !=
            read_only_file_mapping_result::success) {

            return report_database_open(
                database_opened,
                layout.database,
                operation,
                diagnostics);
        }

        if (context.database.bind(
                context.database_mapping.bytes()) !=
                database_image_result::success ||
            context.database.file_count() !=
                context.source.file_count()) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::project_database_invalid,
                    operation)
                    .file(layout.database)
                    .detail(
                        "Committed database.bin failed structural binding or does not match SourceSave file_id cardinality")
                    .build());

            return server_status::project_artifact_invalid;
        }

        const auto content_bound =
            context.files.bind_content_baseline(
                context.database.content_baseline());

        if (!succeeded(content_bound)) {
            diagnostics.emit(
                diagnostic(
                    diagnostics::project_database_invalid,
                    operation)
                    .file(layout.database)
                    .detail(
                        "Committed database.bin could not initialize mmap-backed BUILD source-content baseline")
                    .build());

            return content_bound;
        }

        const auto lexical_bound =
            context.lexical.bind_baseline(
                context.database.lexical_baseline(),
                execution_lane_capacity());

        if (!succeeded(lexical_bound) ||
            context.lexical.size() != context.database.file_count()) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::project_database_invalid,
                    operation)
                    .file(layout.database)
                    .detail(
                        "Committed database.bin could not initialize mmap-backed BUILD lexical baseline")
                    .build());

            return succeeded(lexical_bound)
                ? server_status::project_artifact_invalid
                : lexical_bound;
        }

        database_bound = true;
    }

    source_preparation_failure lexical_failure;
    source_replacement_metrics lexical_metrics;

    if (!semantic_replay_roots.empty()) {
        if (!database_bound) {
            return server_status::project_artifact_invalid;
        }

        const auto replaced = replace_source_lexical_state(
            context.files,
            context.lexical,
            lexical_replacement_files,
            &lexical_failure,
            &lexical_metrics);

        if (!succeeded(replaced)) {
            if (lexical_failure.kind == source_preparation_failure_kind::lexical &&
                lexical_failure.file && context.files.contains(lexical_failure.file)) {
                try {
                    const auto path_view = context.files.path(lexical_failure.file);
                    diagnostics.emit(
                        diagnostic(diagnostics::project_lexical_error, operation)
                            .file(std::filesystem::path{path_view.begin(), path_view.end()})
                            .detail(lexical_error_message(lexical_failure.lexical.reason))
                            .build());
                }
                catch (...) {
                    return server_status::io_error;
                }
            }
            return replaced;
        }
    }

    parser_failure semantic_failure;
    std::vector<parser_warning>
        semantic_warnings;

    if (!semantic_replay_roots.empty()) {
        const auto parsed =
            parse_semantic_roots(
                context.files,
                context.lexical,
                semantic_replay_roots,
                context.preprocessor,
                context.strings,
                context.identities,
                context.graph_changes,
                context.source_changes,
                &semantic_failure,
                &semantic_warnings);

        const auto warnings_emitted =
            emit_build_parser_warnings(
                context.files,
                semantic_warnings,
                operation,
                diagnostics);

        if (!succeeded(
                warnings_emitted)) {

            return warnings_emitted;
        }

        if (!succeeded(parsed)) {
            if (semantic_failure.file) {
                const auto emitted =
                    emit_build_source_diagnostic(
                        context.files,
                        semantic_failure.file,
                        semantic_failure.source,
                        semantic_failure.kind ==
                                parser_failure_kind::
                                    lexical
                            ? diagnostics::
                                project_lexical_error
                            : semantic_failure.kind ==
                                    parser_failure_kind::
                                        preprocessing
                                ? diagnostics::
                                    project_preprocessing_error
                                : diagnostics::
                                    project_semantic_error,
                        semantic_failure.detail.empty()
                            ? std::string_view{
                                "Sparse BUILD Parser/Semantic replay failed"}
                            : semantic_failure.detail,
                        operation,
                        diagnostics);

                if (!succeeded(emitted)) {
                    return emitted;
                }
            }

            return parsed;
        }
    }
    else {
        const auto reset =
            context.source_changes.reset();

        if (!succeeded(reset)) {
            return reset;
        }
    }

    const auto topology_finalized =
        context.files.finalize_dependency_topology();

    if (!succeeded(
            topology_finalized)) {

        diagnostics.emit(
            diagnostic(
                topology_finalized ==
                        server_status::io_error
                    ? diagnostics::
                        project_build_incomplete
                    : diagnostics::
                        project_source_save_invalid,
                operation)
                .file(
                    topology_finalized ==
                            server_status::io_error
                        ? project_path
                        : layout.source_save)
                .detail(
                    topology_finalized ==
                            server_status::io_error
                        ? "BUILD could not finalize sparse physical dependency topology"
                        : "Sparse BUILD physical dependency replacement is inconsistent with persisted source.bin")
                .build());

        return topology_finalized;
    }

    const auto source_candidate_bound =
        context.source_candidate.bind(
            context.source,
            context.compiled,
            context.source_changes,
            semantic_invalidated_roots);

    if (!succeeded(
            source_candidate_bound)) {

        diagnostics.emit(
            diagnostic(
                source_candidate_bound ==
                        server_status::io_error
                    ? diagnostics::
                        project_build_incomplete
                    : diagnostics::
                        project_source_save_invalid,
                operation)
                .file(
                    source_candidate_bound ==
                            server_status::io_error
                        ? project_path
                        : layout.source_save)
                .detail(
                    source_candidate_bound ==
                            server_status::io_error
                        ? "BUILD could not allocate sparse merged Source Map overlay"
                        : "OLD Source Map/SourceSave and sparse replay could not form one BUILD candidate")
                .build());

        return source_candidate_bound;
    }

    const auto assign_candidate_bound =
        context.assign_candidate.bind(
            context.compiled,
            context.assign_changes,
            assign_replacement_roots);

    if (!succeeded(
            assign_candidate_bound)) {

        diagnostics.emit(
            diagnostic(
                assign_candidate_bound ==
                        server_status::io_error
                    ? diagnostics::
                        project_build_incomplete
                    : diagnostics::
                        project_compiled_invalid,
                operation)
                .file(layout.compiled)
                .detail(
                    assign_candidate_bound ==
                            server_status::io_error
                        ? "BUILD could not allocate sparse Assign candidate state"
                        : "Persisted Assign records and sparse replacements could not form one ordered BUILD candidate")
                .build());

        return assign_candidate_bound;
    }

    if (telemetry != nullptr) {
        telemetry->sparse_reconstruction_ns =
            elapsed_ns(
                sparse_reconstruction_begin,
                build_clock::now());

        telemetry->graph_type_patches =
            static_cast<std::uint64_t>(
                context.graph_changes.
                    type_patch_count());

        telemetry->graph_appended_types =
            static_cast<std::uint64_t>(
                context.graph_changes.
                    appended_type_count());

        telemetry->graph_object_patches =
            static_cast<std::uint64_t>(
                context.graph_changes.
                    object_patch_count());

        telemetry->graph_appended_objects =
            static_cast<std::uint64_t>(
                context.graph_changes.
                    appended_object_count());

        telemetry->graph_link_patches =
            static_cast<std::uint64_t>(
                context.graph_changes.
                    link_patch_count());

        telemetry->graph_appended_links =
            static_cast<std::uint64_t>(
                context.graph_changes.
                    appended_link_count());
    }

    const auto dense_projection_begin =
        build_clock::now();

    const auto graph_candidate_prepared =
        context.graph_candidate.prepare(
            context.graph_changes);

    if (telemetry != nullptr) {
        telemetry->dense_projection_ns =
            elapsed_ns(
                dense_projection_begin,
                build_clock::now());
    }

    if (!succeeded(
            graph_candidate_prepared)) {

        diagnostics.emit(
            diagnostic(
                graph_candidate_prepared ==
                        server_status::io_error
                    ? diagnostics::
                        project_build_incomplete
                    : diagnostics::
                        project_compiled_invalid,
                operation)
                .file(layout.compiled)
                .detail(
                    graph_candidate_prepared ==
                            server_status::io_error
                        ? "BUILD could not allocate dense final-G remap state"
                        : "Sparse Graph candidate cannot project to one dense final G")
                .build());

        return graph_candidate_prepared;
    }

    const bool direct_compiled_ready =
        direct_object_state.supported &&
        context.graph_changes.
            type_patch_count() == 0 &&
        context.graph_changes.
            appended_type_count() == 0 &&
        context.graph_changes.
            appended_object_count() == 0 &&
        context.graph_changes.
            link_patch_count() == 0 &&
        context.graph_changes.
            appended_link_count() == 0 &&
        context.graph_changes.
            initialization_change_count() == 0 &&
        context.graph_changes.
            member_entries().empty() &&
        context.graph_changes.
            base_entries().empty() &&
        context.graph_changes.
            derived_type_entries().empty() &&
        context.graph_changes.
            endpoint_path_entries().empty() &&
        context.graph_changes.
            endpoint_path_step_entries().empty() &&
        context.graph_changes.
            stale_type_count() == 0 &&
        context.graph_changes.
            stale_object_count() == 0 &&
        context.graph_changes.
            stale_link_count() == 0 &&
        context.graph_candidate.type_count() ==
            context.compiled.type_count() &&
        context.graph_candidate.object_count() ==
            context.compiled.object_count() &&
        context.graph_candidate.link_count() ==
            context.compiled.link_count() &&
        context.strings.size() ==
            context.compiled.string_count() &&
        context.identities.size() ==
            context.compiled.identity_count() &&
        context.files.size() ==
            context.compiled.source_file_count() &&
        context.assign_candidate.
            replaced_file_count() == 0 &&
        !configuration_identity_changed &&
        compiled_source_delta_unchanged(
            context.compiled,
            context.source_changes,
            semantic_invalidated_roots);

    const auto artifact_materialization_begin =
        build_clock::now();

    const bool compiled_candidate_required =
        !direct_compiled_ready;

    compiled_project_layout
        compiled_candidate_layout;

    if (compiled_candidate_required) {
        const auto compiled_candidate_prepared =
            prepare_build_compiled_project_layout(
                context.strings,
                context.identities,
                context.graph_changes,
                context.graph_candidate,
                context.assign_candidate,
                context.files,
                context.source_candidate,
                compiled_candidate_layout);

        if (compiled_candidate_prepared !=
            compiled_project_image_result::
                success) {

            diagnostics.emit(
                diagnostic(
                    compiled_candidate_prepared ==
                            compiled_project_image_result::
                                failed
                        ? diagnostics::
                            project_build_incomplete
                        : diagnostics::
                            project_compiled_invalid,
                    operation)
                    .file(layout.compiled)
                    .detail(
                        compiled_candidate_prepared ==
                                compiled_project_image_result::
                                    failed
                            ? "BUILD could not prepare exact final compiled.bin layout"
                            : "Sparse BUILD candidates disagree with final compiled.bin layout contract")
                    .build());

            return compiled_candidate_prepared ==
                    compiled_project_image_result::
                        failed
                ? server_status::io_error
                : server_status::
                    project_artifact_invalid;
        }
    }

    const bool database_candidate_required =
        database_bound &&
        (!lexical_replacement_files.empty() ||
         context.files.size() !=
            context.database.file_count());

    const bool manifest_candidate_required =
        configuration_probe_changed;

    source_save_layout
        source_candidate_layout;

    const source_save_build_options
        source_candidate_options{
            scan.next_checkpoint};

    const auto source_candidate_prepared =
        prepare_build_source_save_layout(
            context.files,
            context.identities,
            context.graph_changes,
            context.graph_candidate,
            context.source_candidate,
            source_candidate_options,
            source_candidate_layout);

    if (source_candidate_prepared !=
        source_save_result::success) {

        diagnostics.emit(
            diagnostic(
                source_candidate_prepared ==
                        source_save_result::failed
                    ? diagnostics::
                        project_source_save_io_failed
                    : diagnostics::
                        project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "BUILD could not prepare the final source.bin v8 candidate layout")
                .build());

        return source_candidate_prepared ==
                source_save_result::failed
            ? server_status::io_error
            : server_status::
                project_artifact_invalid;
    }

    database_layout
        database_candidate_layout;

    if (database_candidate_required) {
        const auto database_prepared =
            prepare_database_layout(
                context.files,
                context.lexical,
                database_candidate_layout);

        if (database_prepared !=
            database_image_result::success) {

            diagnostics.emit(
                diagnostic(
                    database_prepared ==
                            database_image_result::
                                failed
                        ? diagnostics::
                            project_database_io_failed
                        : diagnostics::
                            project_database_invalid,
                    operation)
                    .file(layout.database)
                    .detail(
                        "BUILD could not prepare the final database.bin candidate layout")
                    .build());

            return database_prepared ==
                    database_image_result::
                        failed
                ? server_status::io_error
                : server_status::
                    project_artifact_invalid;
        }
    }

    project_configuration_manifest_layout
        manifest_candidate_layout;

    if (manifest_candidate_required) {
        const auto manifest_prepared =
            prepare_project_configuration_manifest_layout(
                context.manifest,
                manifest_candidate_layout);

        if (manifest_prepared !=
            project_configuration_manifest_store_result::
                success) {

            diagnostics.emit(
                diagnostic(
                    manifest_prepared ==
                            project_configuration_manifest_store_result::
                                io_failed
                        ? diagnostics::
                            project_manifest_io_failed
                        : diagnostics::
                            project_manifest_invalid,
                    operation)
                    .file(layout.manifest)
                    .detail(
                        "BUILD could not prepare the final project.manifest candidate layout")
                    .build());

            return manifest_prepared ==
                    project_configuration_manifest_store_result::
                        io_failed
                ? server_status::io_error
                : server_status::
                    project_artifact_invalid;
        }
    }

    build_candidate_paths
        candidate_paths;

    if (!make_build_candidate_paths(
            layout,
            candidate_paths)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::
                    project_build_incomplete,
                operation)
                .file(layout.root)
                .detail(
                    "BUILD candidate artifact paths are invalid or collide with configured artifact paths")
                .build());

        return server_status::io_error;
    }

    remove_build_candidates(
        candidate_paths);

    build_candidate_cleanup
        candidate_cleanup{
            candidate_paths};

    writable_file_mapping
        compiled_candidate_mapping;

    writable_file_mapping
        source_candidate_mapping;

    writable_file_mapping
        database_candidate_mapping;

    writable_file_mapping
        manifest_candidate_mapping;

    if ((compiled_candidate_required &&
         compiled_candidate_mapping.create(
             candidate_paths.compiled,
             compiled_candidate_layout.size()) !=
             writable_file_mapping_result::
                 success) ||
        source_candidate_mapping.create(
            candidate_paths.source,
            source_candidate_layout.size()) !=
            writable_file_mapping_result::
                success ||
        (database_candidate_required &&
         database_candidate_mapping.create(
             candidate_paths.database,
             database_candidate_layout.size()) !=
             writable_file_mapping_result::
                 success) ||
        (manifest_candidate_required &&
         manifest_candidate_mapping.create(
             candidate_paths.manifest,
             manifest_candidate_layout.size()) !=
             writable_file_mapping_result::
                 success)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::
                    project_build_incomplete,
                operation)
                .file(layout.root)
                .detail(
                    "BUILD could not create writable candidate artifact mappings")
                .build());

        return server_status::io_error;
    }

    compiled_project_view
        candidate_compiled;

    const compiled_project_view*
        final_compiled =
            &context.compiled;

    if (compiled_candidate_required) {
        const auto compiled_encoded =
            encode_build_compiled_project_image(
                context.strings,
                context.identities,
                context.graph_changes,
                context.graph_candidate,
                context.assign_candidate,
                context.files,
                context.source_candidate,
                compiled_candidate_layout,
                compiled_candidate_mapping.bytes());

        if (compiled_encoded !=
            compiled_project_image_result::
                success) {

            diagnostics.emit(
                diagnostic(
                    compiled_encoded ==
                            compiled_project_image_result::
                                failed
                        ? diagnostics::
                            project_compiled_io_failed
                        : diagnostics::
                            project_compiled_invalid,
                    operation)
                    .file(candidate_paths.compiled)
                    .detail(
                        "BUILD direct compiled.bin candidate encoding failed")
                    .build());

            return compiled_encoded ==
                    compiled_project_image_result::
                        failed
                ? server_status::io_error
                : server_status::
                    project_artifact_invalid;
        }

        if (candidate_compiled.bind(
                compiled_candidate_mapping.bytes()) !=
                compiled_project_image_result::
                    success ||
            candidate_compiled.verify_contents() !=
                compiled_project_image_result::
                    success) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::
                        project_compiled_invalid,
                    operation)
                    .file(candidate_paths.compiled)
                    .detail(
                        "BUILD compiled.bin candidate failed cold semantic verification")
                    .build());

            return server_status::
                project_artifact_invalid;
        }

        final_compiled =
            &candidate_compiled;
    }
    else if (context.compiled.verify_contents() !=
        compiled_project_image_result::
            success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::
                    project_compiled_invalid,
                operation)
                .file(layout.compiled)
                .detail(
                    "Directly patched BUILD compiled.bin failed cold semantic verification")
                .build());

        return server_status::
            project_artifact_invalid;
    }

    const auto source_encoded =
        encode_build_source_save_image(
            context.files,
            context.identities,
            context.graph_changes,
            context.graph_candidate,
            context.source_candidate,
            source_candidate_layout,
            source_candidate_mapping.bytes());

    if (source_encoded !=
        source_save_result::success ||
        validate_source_save_image(
            source_candidate_mapping.bytes()) !=
            source_save_result::success) {

        diagnostics.emit(
            diagnostic(
                source_encoded ==
                        source_save_result::failed
                    ? diagnostics::
                        project_source_save_io_failed
                    : diagnostics::
                        project_source_save_invalid,
                operation)
                .file(candidate_paths.source)
                .detail(
                    "BUILD source.bin candidate encoding or cold validation failed")
                .build());

        return source_encoded ==
                source_save_result::failed
            ? server_status::io_error
            : server_status::
                project_artifact_invalid;
    }

    source_save_view
        candidate_source;

    if (candidate_source.bind(
            source_candidate_mapping.bytes()) !=
            source_save_result::success ||
        verify_source_save_presence(
            candidate_source,
            *final_compiled) !=
            source_save_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::
                    project_source_save_invalid,
                operation)
                .file(candidate_paths.source)
                .detail(
                    "BUILD source.bin candidate disagrees with final compiled.bin semantic presence")
                .build());

        return server_status::
            project_artifact_invalid;
    }

    if (database_candidate_required) {
        const auto database_encoded =
            encode_database_image(
                context.files,
                context.lexical,
                database_candidate_layout,
                database_candidate_mapping.bytes());

        if (database_encoded !=
                database_image_result::success ||
            verify_database_image(
                database_candidate_mapping.bytes(),
                context.files,
                context.lexical) !=
                database_image_result::success) {

            diagnostics.emit(
                diagnostic(
                    database_encoded ==
                            database_image_result::
                                failed
                        ? diagnostics::
                            project_database_io_failed
                        : diagnostics::
                            project_database_invalid,
                    operation)
                    .file(candidate_paths.database)
                    .detail(
                        "BUILD database.bin candidate encoding or cold verification failed")
                    .build());

            return database_encoded ==
                    database_image_result::
                        failed
                ? server_status::io_error
                : server_status::
                    project_artifact_invalid;
        }
    }

    if (manifest_candidate_required) {
        const auto manifest_encoded =
            encode_project_configuration_manifest(
                context.manifest,
                manifest_candidate_layout,
                manifest_candidate_mapping.bytes());

        project_configuration_manifest
            decoded_manifest;

        if (manifest_encoded !=
                project_configuration_manifest_store_result::
                    success ||
            decode_project_configuration_manifest(
                manifest_candidate_mapping.bytes(),
                decoded_manifest) !=
                project_configuration_manifest_store_result::
                    success ||
            !(decoded_manifest.configuration_hash ==
              context.manifest.configuration_hash) ||
            !(decoded_manifest.preprocessor_hash ==
              context.manifest.preprocessor_hash) ||
            decoded_manifest.files.size() !=
                context.manifest.files.size()) {

            diagnostics.emit(
                diagnostic(
                    manifest_encoded ==
                            project_configuration_manifest_store_result::
                                io_failed
                        ? diagnostics::
                            project_manifest_io_failed
                        : diagnostics::
                            project_manifest_invalid,
                    operation)
                    .file(candidate_paths.manifest)
                    .detail(
                        "BUILD project.manifest candidate encoding or decode verification failed")
                    .build());

            return manifest_encoded ==
                    project_configuration_manifest_store_result::
                        io_failed
                ? server_status::io_error
                : server_status::
                    project_artifact_invalid;
        }
    }

    if ((compiled_candidate_required
            ? compiled_candidate_mapping.flush()
            : context.compiled_write_mapping.flush()) !=
            writable_file_mapping_result::
                success ||
        source_candidate_mapping.flush() !=
            writable_file_mapping_result::
                success ||
        (database_candidate_required &&
         database_candidate_mapping.flush() !=
             writable_file_mapping_result::
                 success) ||
        (manifest_candidate_required &&
         manifest_candidate_mapping.flush() !=
             writable_file_mapping_result::
                 success)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::
                    project_build_incomplete,
                operation)
                .file(layout.root)
                .detail(
                    "BUILD could not flush one or more validated candidate artifacts")
                .build());

        return server_status::io_error;
    }

    context.compiled.reset();
    context.compiled_write_mapping.reset();
    compiled_candidate_mapping.reset();
    source_candidate_mapping.reset();
    database_candidate_mapping.reset();
    manifest_candidate_mapping.reset();

    read_only_file_mapping
        resident_compiled_mapping;

    const auto& resident_compiled_path =
        compiled_candidate_required
        ? candidate_paths.compiled
        : layout.compiled;

    if (resident_compiled_mapping.open(
            resident_compiled_path) !=
            read_only_file_mapping_result::
                success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::
                    project_build_incomplete,
                operation)
                .file(resident_compiled_path)
                .detail(
                    compiled_candidate_required
                        ? "BUILD could not reopen flushed compiled.bin candidate"
                        : "BUILD could not reopen directly patched final compiled.bin")
                .build());

        return server_status::io_error;
    }

    compiled_project_view
        resident_compiled;

    if (resident_compiled.bind(
            resident_compiled_mapping.bytes()) !=
            compiled_project_image_result::
                success ||
        resident_compiled.verify_contents() !=
            compiled_project_image_result::
                success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::
                    project_compiled_invalid,
                operation)
                .file(resident_compiled_path)
                .detail(
                    compiled_candidate_required
                        ? "Reopened BUILD compiled.bin candidate failed cold verification"
                        : "Reopened directly patched BUILD compiled.bin failed cold verification")
                .build());

        return server_status::
            project_artifact_invalid;
    }

    {
        read_only_file_mapping
            reopened_source_mapping;

        if (reopened_source_mapping.open(
                candidate_paths.source) !=
                read_only_file_mapping_result::
                    success) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::
                        project_build_incomplete,
                    operation)
                    .file(candidate_paths.source)
                    .detail(
                        "BUILD could not reopen flushed source.bin candidate")
                    .build());

            return server_status::io_error;
        }

        source_save_view
            reopened_source;

        if (reopened_source.bind(
                reopened_source_mapping.bytes()) !=
                source_save_result::success ||
            validate_source_save_image(
                reopened_source_mapping.bytes()) !=
                source_save_result::success ||
            verify_source_save_presence(
                reopened_source,
                resident_compiled) !=
                source_save_result::success) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::
                        project_source_save_invalid,
                    operation)
                    .file(candidate_paths.source)
                    .detail(
                        "Reopened BUILD source.bin candidate failed cold cross-artifact verification")
                    .build());

            return server_status::
                project_artifact_invalid;
        }
    }

    if (database_candidate_required) {
        read_only_file_mapping
            reopened_database_mapping;

        if (reopened_database_mapping.open(
                candidate_paths.database) !=
                read_only_file_mapping_result::
                    success ||
            verify_database_image(
                reopened_database_mapping.bytes(),
                context.files,
                context.lexical) !=
                database_image_result::success) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::
                        project_database_invalid,
                    operation)
                    .file(candidate_paths.database)
                    .detail(
                        "Reopened BUILD database.bin candidate failed cold verification")
                    .build());

            return server_status::
                project_artifact_invalid;
        }
    }

    if (manifest_candidate_required) {
        read_only_file_mapping
            reopened_manifest_mapping;

        project_configuration_manifest
            reopened_manifest;

        if (reopened_manifest_mapping.open(
                candidate_paths.manifest) !=
                read_only_file_mapping_result::
                    success ||
            decode_project_configuration_manifest(
                reopened_manifest_mapping.bytes(),
                reopened_manifest) !=
                project_configuration_manifest_store_result::
                    success ||
            !(reopened_manifest.configuration_hash ==
              context.manifest.configuration_hash) ||
            !(reopened_manifest.preprocessor_hash ==
              context.manifest.preprocessor_hash) ||
            reopened_manifest.files.size() !=
                context.manifest.files.size()) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::
                        project_manifest_invalid,
                    operation)
                    .file(candidate_paths.manifest)
                    .detail(
                        "Reopened BUILD project.manifest candidate failed cold verification")
                    .build());

            return server_status::
                project_artifact_invalid;
        }
    }

    if (telemetry != nullptr) {
        telemetry->artifact_materialization_ns =
            elapsed_ns(
                artifact_materialization_begin,
                build_clock::now());
    }

    const auto runtime_publication_begin =
        build_clock::now();

    const auto resident_created =
        create_resident_project(
            project_path,
            settings,
            operation,
            diagnostics,
            std::move(
                resident_compiled_mapping),
            resident_compiled,
            output);

    if (telemetry != nullptr) {
        telemetry->runtime_publication_ns =
            elapsed_ns(
                runtime_publication_begin,
                build_clock::now());
    }

    if (!succeeded(
            resident_created)) {

        // Persisted Gn is still untouched here.
        return resident_created;
    }

    const auto promotion_begin =
        build_clock::now();

    const auto promoted =
        promote_build_artifacts(
            layout,
            candidate_paths,
            compiled_candidate_required,
            database_candidate_required,
            manifest_candidate_required);

    if (telemetry != nullptr) {
        telemetry->promotion_ns =
            elapsed_ns(
                promotion_begin,
                build_clock::now());
    }

    if (promoted !=
        build_artifact_promotion_result::
            success) {

        output.reset();

        diagnostics.emit(
            diagnostic(
                diagnostics::
                    project_build_incomplete,
                operation)
                .file(layout.root)
                .detail(
                    promoted ==
                            build_artifact_promotion_result::
                                rollback_failed
                        ? "BUILD artifact promotion failed and rollback of OLD persisted state also failed"
                        : promoted ==
                                build_artifact_promotion_result::
                                    invalid_input
                            ? "BUILD artifact promotion paths are invalid or collide"
                            : "BUILD artifact promotion failed; OLD persisted state was restored")
                .build());

        return promoted ==
                build_artifact_promotion_result::
                    invalid_input
            ? server_status::
                project_artifact_invalid
            : server_status::io_error;
    }

    candidate_cleanup.release();
    direct_compiled_guard.release();

    return server_status::success;
}

}
