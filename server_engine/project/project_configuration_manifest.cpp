#include "project_configuration_manifest.hpp"

#include "project_configuration_loader.hpp"
#include "file/file_context.hpp"
#include "project_path.hpp"
#include "../filesystem_path.hpp"
#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cw::server {
namespace {

void append_u32(
    std::string& output,
    std::uint32_t value) {

    for (std::size_t index = 0;
         index < 4;
         ++index) {

        output.push_back(
            static_cast<char>(
                (value >> (index * 8)) &
                0xffU));
    }
}

void append_u64(
    std::string& output,
    std::uint64_t value) {

    for (std::size_t index = 0;
         index < 8;
         ++index) {

        output.push_back(
            static_cast<char>(
                (value >> (index * 8)) &
                0xffU));
    }
}

[[nodiscard]] project_configuration_hash
calculate_configuration_hash_impl(
    std::span<const project_configuration_file_proof> files) {

    std::string canonical;
    canonical.append("CWCFG002", 8);

    append_u64(
        canonical,
        static_cast<std::uint64_t>(
            files.size()));

    for (const auto& file : files) {
        std::string path;

        if (filesystem_path_to_utf8(file.path, path) !=
            filesystem_path_result::success) {

            throw std::runtime_error(
                "Cannot encode Project configuration path as UTF-8");
        }

        append_u32(
            canonical,
            file.declaring_file);

        append_u32(
            canonical,
            static_cast<std::uint32_t>(
                file.path_type));

        append_u64(
            canonical,
            static_cast<std::uint64_t>(
                path.size()));

        canonical.append(
            path.data(),
            path.size());

        canonical.append(
            reinterpret_cast<const char*>(
                file.content_hash.bytes.data()),
            file.content_hash.bytes.size());
    }

    const auto digest =
        hash_file_content(
            canonical);

    project_configuration_hash output;
    output.bytes = digest.bytes;
    return output;
}

[[nodiscard]] server_status report_acquisition_failure(
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    file_content_result result) {

    diagnostics.emit(
        diagnostic(
            diagnostics::project_configuration_read_failed,
            operation)
            .file(path)
            .detail(
                result == file_content_result::missing
                    ? "Project configuration file does not exist"
                    : result == file_content_result::changed_during_read
                        ? "Project configuration changed during stable acquisition"
                        : result == file_content_result::allocation_failed
                            ? "Cannot allocate Project configuration snapshot"
                            : "Cannot acquire Project configuration snapshot")
            .build());

    return result == file_content_result::allocation_failed
        ? server_status::io_error
        : server_status::project_configuration_invalid;
}


inline constexpr std::size_t project_configuration_depth_limit =
    256;

struct pending_project_dependency final {
    const project_configuration_dependency* dependency = nullptr;
    std::filesystem::path path;
    file_id file{};
    file_acquire_job job;
    file_acquire_result result;
};

[[nodiscard]] server_status execute_parallel_project_acquires(
    std::span<pending_project_dependency> pending) noexcept {

    std::size_t acquire_count = 0;

    for (const auto& input : pending) {
        if (input.job.file) {
            ++acquire_count;
        }
    }

    if (acquire_count == 0) {
        return server_status::success;
    }

    const auto hardware =
        std::thread::hardware_concurrency();

    const auto worker_count =
        (std::min)(
            acquire_count,
            static_cast<std::size_t>(
                hardware == 0 ? 1 : hardware));

    std::atomic_size_t next{0};

    const auto execute = [&]() noexcept {
        for (;;) {
            const auto index =
                next.fetch_add(
                    1,
                    std::memory_order_relaxed);

            if (index >= pending.size()) {
                return;
            }

            auto& input =
                pending[index];

            if (!input.job.file) {
                continue;
            }

            file_context::execute_acquire(
                input.job,
                input.result);
        }
    };

    if (worker_count == 1) {
        execute();
        return server_status::success;
    }

    try {
        std::vector<std::jthread> workers;
        workers.reserve(worker_count - 1);

        for (std::size_t index = 1;
             index < worker_count;
             ++index) {

            workers.emplace_back(execute);
        }

        execute();
    }
    catch (...) {
        return server_status::io_error;
    }

    return server_status::success;
}

[[nodiscard]] file_content_result acquisition_result(
    file_acquire_result_kind kind) noexcept {

    switch (kind) {
    case file_acquire_result_kind::missing:
        return file_content_result::missing;

    case file_acquire_result_kind::changed_during_read:
        return file_content_result::changed_during_read;

    case file_acquire_result_kind::allocation_failed:
        return file_content_result::allocation_failed;

    case file_acquire_result_kind::unchanged:
    case file_acquire_result_kind::present:
    case file_acquire_result_kind::failed:
        return file_content_result::failed;
    }

    return file_content_result::failed;
}

class manifest_composer final {
public:
    manifest_composer(
        const std::filesystem::path& root_project_path,
        operation_id operation,
        diagnostic_collection& diagnostics,
        project_configuration_manifest& output,
        file_context* files,
        std::vector<project_preprocessor_configuration>& preprocessors)
        : root_project_path(
              root_project_path),
          operation(operation),
          diagnostics(diagnostics),
          output(output),
          files(files),
          preprocessors(preprocessors) {

        visited.reserve(32);
        active.reserve(16);
        unique_inputs.reserve(32);
        kinds.reserve(64);
    }

    [[nodiscard]] server_status compose() {
        output = {};
        preprocessors.clear();

        const auto root_result =
            resolve_project_path(
                root_project_path,
                root_path);

        if (root_result !=
            project_path_result::
                success) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::project_configuration_read_failed,
                    operation)
                    .file(root_project_path)
                    .detail(
                        "Cannot resolve root Project configuration path")
                    .build());

            return server_status::io_error;
        }

        const auto status =
            visit(
                root_path,
                invalid_configuration_file,
                project_configuration_path_type::relative,
                root_path.filename(),
                {},
                nullptr,
                1);

        if (!succeeded(status)) {
            output = {};
            preprocessors.clear();
            return status;
        }

        try {
            output.configuration_hash =
                calculate_project_configuration_hash(
                    output.files);
        }
        catch (...) {
            output = {};
            return server_status::io_error;
        }

        return server_status::success;
    }

private:
    [[nodiscard]] server_status visit(
        const std::filesystem::path& absolute_path,
        std::uint32_t declaring_file,
        project_configuration_path_type path_type,
        const std::filesystem::path& locator,
        file_id known_file = {},
        file_content_snapshot* prefetched_snapshot = nullptr,
        std::size_t depth = 1) {

        if (depth >
            project_configuration_depth_limit) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::project_configuration_depth_exceeded,
                    operation)
                    .file(absolute_path)
                    .detail(
                        "Project configuration nesting exceeds 256 levels")
                    .build());

            return server_status::
                project_configuration_invalid;
        }

        project_path_key key;

        const auto key_result =
            make_project_path_key(
                absolute_path,
                key);

        if (key_result !=
            project_path_result::
                success) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::project_configuration_read_failed,
                    operation)
                    .file(absolute_path)
                    .detail(
                        "Cannot construct platform filesystem-equivalence key for Project configuration path")
                    .build());

            return server_status::io_error;
        }

        const auto kind_status =
            register_kind(
                key,
                file_kind::project,
                absolute_path);

        if (!succeeded(kind_status)) {
            return kind_status;
        }

        if (active.contains(key)) {
            diagnostics.emit(
                diagnostic(
                    diagnostics::project_configuration_cycle,
                    operation)
                    .file(absolute_path)
                    .detail(
                        "Recursive Project configuration reference forms a cycle")
                    .build());

            return server_status::
                project_configuration_invalid;
        }

        if (visited.contains(key)) {
            diagnostics.emit(
                diagnostic(
                    diagnostics::project_duplicate_construction_input,
                    operation)
                    .file(absolute_path)
                    .detail(
                        "Project configuration is referenced more than once")
                    .build());

            return server_status::
                project_configuration_invalid;
        }

        active.insert(key);

        file_content_snapshot snapshot;
        file_id source_file =
            known_file;

        if (files != nullptr) {
            if (!source_file) {
                const auto resolved =
                    files->resolve(
                        absolute_path,
                        file_kind::project,
                        source_file);

                if (!succeeded(resolved)) {
                    active.erase(key);
                    return resolved;
                }
            } else if (
                !files->contains(source_file) ||
                files->kind(source_file) !=
                    file_kind::project) {

                active.erase(key);
                return server_status::
                    project_configuration_invalid;
            }
        }

        if (prefetched_snapshot != nullptr) {
            if (files == nullptr) {
                active.erase(key);
                return server_status::
                    project_configuration_invalid;
            }

            snapshot =
                std::move(
                    *prefetched_snapshot);
        } else if (files == nullptr) {
            const auto acquired =
                acquire_file_content(
                    absolute_path,
                    snapshot);

            if (acquired !=
                file_content_result::acquired) {

                active.erase(key);

                return report_acquisition_failure(
                    absolute_path,
                    operation,
                    diagnostics,
                    acquired);
            }
        } else {
            file_acquire_job job;

            const auto prepared =
                files->prepare_acquire(
                    source_file,
                    job);

            if (!succeeded(prepared)) {
                active.erase(key);
                return prepared;
            }

            file_acquire_result result;

            file_context::execute_acquire(
                job,
                result);

            if (result.kind !=
                file_acquire_result_kind::present) {

                active.erase(key);

                return report_acquisition_failure(
                    absolute_path,
                    operation,
                    diagnostics,
                    acquisition_result(
                        result.kind));
            }

            bool content_changed = false;

            const auto applied =
                files->apply_acquire(
                    result,
                    content_changed);

            if (!succeeded(applied)) {
                active.erase(key);
                return applied;
            }

            snapshot =
                std::move(result.snapshot);
        }

        std::vector<project_configuration_dependency>
            dependencies;

        project_preprocessor_configuration
            preprocessor;

        const auto status =
            read_project_configuration(
                snapshot.bytes,
                absolute_path,
                operation,
                diagnostics,
                dependencies,
                preprocessor);

        if (!succeeded(status)) {
            active.erase(key);
            return status;
        }

        try {
            if (output.files.size() >
                static_cast<std::size_t>(
                    (std::numeric_limits<std::uint32_t>::max)())) {

                active.erase(key);
                return server_status::io_error;
            }

            const auto current_file =
                static_cast<std::uint32_t>(
                    output.files.size());

            project_configuration_file_proof proof;
            proof.declaring_file =
                declaring_file;
            proof.path_type =
                path_type;
            proof.path =
                locator.lexically_normal();
            proof.content_hash =
                snapshot.content_hash;
            proof.change_token =
                snapshot.change_token;
            proof.change_token_available =
                snapshot.change_token_available;

            if (proof.path.empty()) {
                active.erase(key);
                return server_status::io_error;
            }

            output.files.push_back(
                std::move(proof));

            preprocessors.push_back(
                std::move(preprocessor));

            visited.insert(key);

            const auto dependency_status =
                process_dependencies(
                    absolute_path,
                    current_file,
                    source_file,
                    dependencies,
                    depth);

            if (!succeeded(
                    dependency_status)) {

                active.erase(key);
                return dependency_status;
            }
        }
        catch (...) {
            active.erase(key);
            return server_status::io_error;
        }

        active.erase(key);
        return server_status::success;
    }


    [[nodiscard]] server_status process_dependencies(
        const std::filesystem::path& absolute_path,
        std::uint32_t current_file,
        file_id source_file,
        std::span<const project_configuration_dependency> dependencies,
        std::size_t depth) {

        std::vector<pending_project_dependency>
            pending;

        try {
            pending.reserve(
                dependencies.size());
        }
        catch (...) {
            return server_status::io_error;
        }

        for (const auto& dependency :
             dependencies) {

            const auto child_input =
                dependency.path_type ==
                    project_configuration_path_type::absolute
                ? dependency.path
                : absolute_path.parent_path() /
                    dependency.path;

            std::filesystem::path child;

            const auto child_result =
                resolve_project_path(
                    child_input,
                    child);

            if (child_result !=
                project_path_result::success) {

                diagnostics.emit(
                    diagnostic(
                        diagnostics::project_configuration_read_failed,
                        operation)
                        .file(child_input)
                        .detail(
                            "Cannot resolve referenced Project construction input")
                        .build());

                return server_status::io_error;
            }

            project_path_key child_key;

            const auto key_result =
                make_project_path_key(
                    child,
                    child_key);

            if (key_result !=
                project_path_result::success) {

                return server_status::io_error;
            }

            const auto kind_status =
                register_kind(
                    child_key,
                    dependency.kind,
                    child);

            if (!succeeded(kind_status)) {
                return kind_status;
            }

            if (dependency.kind !=
                file_kind::header) {

                const auto inserted =
                    unique_inputs.insert(
                        child_key);

                if (!inserted.second) {
                    diagnostics.emit(
                        diagnostic(
                            diagnostics::project_duplicate_construction_input,
                            operation)
                            .file(child)
                            .detail(
                                dependency.kind ==
                                        file_kind::source
                                    ? "Source file is declared more than once"
                                    : dependency.kind ==
                                            file_kind::assign
                                        ? "Assign file is declared more than once"
                                        : "Project configuration is referenced more than once")
                            .build());

                    return server_status::
                        project_configuration_invalid;
                }
            }

            file_id child_file;

            if (files != nullptr) {
                const auto resolved =
                    files->resolve(
                        child,
                        dependency.kind,
                        child_file);

                if (!succeeded(resolved)) {
                    return resolved;
                }

                const auto staged =
                    files->add_dependency(
                        source_file,
                        child_file);

                if (!succeeded(staged)) {
                    return staged;
                }
            }

            if (dependency.kind !=
                file_kind::project) {

                continue;
            }

            try {
                pending.push_back({
                    &dependency,
                    std::move(child),
                    child_file,
                });
            }
            catch (...) {
                return server_status::io_error;
            }
        }

        if (files != nullptr) {
            // All child Project file_id resolution is complete before
            // prepare_acquire(). Borrowed path views remain stable while reads
            // run because File Context path storage is not mutated until join.
            for (auto& input : pending) {
                const auto* physical =
                    files->physical(
                        input.file);

                if (physical == nullptr) {
                    return server_status::
                        project_configuration_invalid;
                }

                if (physical->present()) {
                    continue;
                }

                const auto prepared =
                    files->prepare_acquire(
                        input.file,
                        input.job);

                if (!succeeded(prepared)) {
                    return prepared;
                }
            }

            const auto executed =
                execute_parallel_project_acquires(
                    pending);

            if (!succeeded(executed)) {
                return executed;
            }

            for (auto& input : pending) {
                if (!input.job.file) {
                    continue;
                }

                if (input.result.kind !=
                    file_acquire_result_kind::present) {

                    return report_acquisition_failure(
                        input.path,
                        operation,
                        diagnostics,
                        acquisition_result(
                            input.result.kind));
                }

                bool content_changed = false;

                const auto applied =
                    files->apply_acquire(
                        input.result,
                        content_changed);

                if (!succeeded(applied)) {
                    return applied;
                }
            }
        }

        for (auto& input : pending) {
            auto* prefetched =
                files != nullptr &&
                input.job.file
                    ? &input.result.snapshot
                    : nullptr;

            const auto child_status =
                visit(
                    input.path,
                    current_file,
                    input.dependency->path_type,
                    input.dependency->path,
                    input.file,
                    prefetched,
                    depth + 1);

            if (!succeeded(child_status)) {
                return child_status;
            }
        }

        return server_status::success;
    }

    [[nodiscard]] server_status register_kind(
        const project_path_key& key,
        file_kind kind,
        const std::filesystem::path& path) {

        const auto [position, inserted] =
            kinds.emplace(
                key,
                kind);

        if (inserted ||
            position->second == kind) {

            return server_status::success;
        }

        diagnostics.emit(
            diagnostic(
                diagnostics::project_duplicate_construction_input,
                operation)
                .file(path)
                .detail(
                    "Physical Project input is used with conflicting file kinds")
                .build());

        return server_status::
            project_configuration_invalid;
    }

    std::filesystem::path root_project_path;
    std::filesystem::path root_path;
    operation_id operation;
    diagnostic_collection& diagnostics;
    project_configuration_manifest& output;
    file_context* files = nullptr;
    std::vector<project_preprocessor_configuration>& preprocessors;
    std::unordered_set<project_path_key, project_path_key_hash> visited;
    std::unordered_set<project_path_key, project_path_key_hash> active;
    std::unordered_set<project_path_key, project_path_key_hash> unique_inputs;
    std::unordered_map<
        project_path_key,
        file_kind,
        project_path_key_hash> kinds;

};

}

project_configuration_hash calculate_project_configuration_hash(
    std::span<const project_configuration_file_proof> files) {

    return calculate_configuration_hash_impl(
        files);
}

server_status compose_project_configuration_manifest(
    const std::filesystem::path& root_project_path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    project_configuration_manifest& output,
    std::vector<project_preprocessor_configuration>& preprocessors) {

    manifest_composer composer{
        root_project_path,
        operation,
        diagnostics,
        output,
        nullptr,
        preprocessors};

    return composer.compose();
}

server_status compose_project_configuration(
    const std::filesystem::path& root_project_path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    project_configuration_manifest& manifest,
    file_context& files,
    std::vector<project_preprocessor_configuration>& preprocessors) {

    manifest_composer composer{
        root_project_path,
        operation,
        diagnostics,
        manifest,
        &files,
        preprocessors};

    return composer.compose();
}

server_status verify_project_configuration_manifest(
    const std::filesystem::path& root_project_path,
    const project_configuration_manifest& persisted,
    operation_id operation,
    diagnostic_collection& diagnostics,
    project_configuration_manifest_verification& verification) {

    verification =
        project_configuration_manifest_verification::
            unchanged;

    if (persisted.files.empty()) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_manifest_invalid,
                operation)
                .detail(
                    "Persisted Project configuration manifest contains no files")
                .build());

        return server_status::
            project_artifact_invalid;
    }

    std::filesystem::path root;

    const auto root_result =
        resolve_project_path(
            root_project_path,
            root);

    if (root_result !=
        project_path_result::
            success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_configuration_read_failed,
                operation)
                .file(root_project_path)
                .detail(
                    "Cannot resolve resident Project configuration path")
                .build());

        return server_status::io_error;
    }

    const auto& root_file =
        persisted.files.front();

    if (root_file.declaring_file !=
            invalid_configuration_file ||
        root_file.path_type !=
            project_configuration_path_type::relative ||
        root_file.path !=
            root.filename()) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_manifest_invalid,
                operation)
                .detail(
                    "Persisted Project configuration manifest root does not match the resident Project")
                .build());

        return server_status::
            project_artifact_invalid;
    }

    std::vector<std::filesystem::path>
        resolved_paths;

    try {
        resolved_paths.reserve(
            persisted.files.size());
        resolved_paths.push_back(root);
    }
    catch (...) {
        return server_status::io_error;
    }

    for (std::size_t index = 0;
         index < persisted.files.size();
         ++index) {

        const auto& file =
            persisted.files[index];

        std::filesystem::path path;

        if (index == 0) {
            path = root;
        } else {
            if (file.declaring_file >= index) {
                diagnostics.emit(
                    diagnostic(
                        diagnostics::project_manifest_invalid,
                        operation)
                        .detail(
                            "Persisted Project configuration manifest contains an invalid declaring-file edge")
                        .build());

                return server_status::
                    project_artifact_invalid;
            }

            const auto& declaring_path =
                resolved_paths[
                    file.declaring_file];

            const auto path_input =
                file.path_type ==
                    project_configuration_path_type::absolute
                ? file.path
                : declaring_path.parent_path() /
                    file.path;

            const auto path_result =
                resolve_project_path(
                    path_input,
                    path);

            if (path_result !=
                project_path_result::
                    success) {

                diagnostics.emit(
                    diagnostic(
                        diagnostics::project_configuration_read_failed,
                        operation)
                        .file(path_input)
                        .detail(
                            "Cannot resolve Project configuration manifest path")
                        .build());

                return server_status::io_error;
            }

            try {
                resolved_paths.push_back(
                    path);
            }
            catch (...) {
                return server_status::io_error;
            }
        }

        if (file.change_token_available &&
            file.change_token) {

            bool unchanged = false;

            const auto proof =
                prove_file_unchanged(
                    path,
                    file.change_token,
                    unchanged);

            if (proof ==
                file_token_result::missing) {

                verification =
                    project_configuration_manifest_verification::
                        changed;

                return server_status::success;
            }

            if (proof ==
                file_token_result::failed) {

                diagnostics.emit(
                    diagnostic(
                        diagnostics::project_configuration_read_failed,
                        operation)
                        .file(path)
                        .detail(
                            "Cannot verify Project configuration file change token")
                        .build());

                return server_status::io_error;
            }

            if (proof ==
                    file_token_result::available &&
                unchanged) {

                continue;
            }
        }

        file_content_snapshot snapshot;

        const auto acquired =
            acquire_file_content(
                path,
                snapshot);

        if (acquired ==
            file_content_result::missing) {

            verification =
                project_configuration_manifest_verification::
                    changed;

            return server_status::success;
        }

        if (acquired !=
            file_content_result::acquired) {

            return report_acquisition_failure(
                path,
                operation,
                diagnostics,
                acquired);
        }

        if (!(snapshot.content_hash ==
              file.content_hash)) {

            verification =
                project_configuration_manifest_verification::
                    changed;

            return server_status::success;
        }
    }

    return server_status::success;
}

}
