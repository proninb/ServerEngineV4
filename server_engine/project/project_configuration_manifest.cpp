#include "project_configuration_manifest.hpp"

#include "project_configuration_loader.hpp"
#include "file/file_context.hpp"
#include "project_path.hpp"
#include "../filesystem_path.hpp"
#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
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

class manifest_composer final {
public:
    manifest_composer(
        const std::filesystem::path& root_project_path,
        operation_id operation,
        diagnostic_collection& diagnostics,
        project_configuration_manifest& output,
        file_context* files)
        : root_project_path(
              root_project_path),
          operation(operation),
          diagnostics(diagnostics),
          output(output),
          files(files) {

        visited.reserve(32);
        active.reserve(16);
        unique_inputs.reserve(32);
        kinds.reserve(64);
    }

    [[nodiscard]] server_status compose() {
        output = {};

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
                root_path.filename());

        if (!succeeded(status)) {
            output = {};
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
        file_id known_file = {}) {

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

        if (files == nullptr) {
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

                const auto acquisition =
                    result.kind ==
                        file_acquire_result_kind::missing
                    ? file_content_result::missing
                    : result.kind ==
                        file_acquire_result_kind::changed_during_read
                        ? file_content_result::changed_during_read
                        : result.kind ==
                            file_acquire_result_kind::allocation_failed
                            ? file_content_result::allocation_failed
                            : file_content_result::failed;

                return report_acquisition_failure(
                    absolute_path,
                    operation,
                    diagnostics,
                    acquisition);
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

        const auto status =
            read_project_configuration(
                snapshot.bytes,
                absolute_path,
                operation,
                diagnostics,
                dependencies);

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

            visited.insert(key);

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
                    project_path_result::
                        success) {

                    active.erase(key);

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

                    active.erase(key);
                    return server_status::io_error;
                }

                const auto kind_status =
                    register_kind(
                        child_key,
                        dependency.kind,
                        child);

                if (!succeeded(kind_status)) {
                    active.erase(key);
                    return kind_status;
                }

                if (dependency.kind ==
                        file_kind::source ||
                    dependency.kind ==
                        file_kind::assign) {

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
                                        : "Assign file is declared more than once")
                                .build());

                        active.erase(key);
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
                        active.erase(key);
                        return resolved;
                    }

                    const auto staged =
                        files->add_dependency(
                            source_file,
                            child_file);

                    if (!succeeded(staged)) {
                        active.erase(key);
                        return staged;
                    }
                }

                if (dependency.kind ==
                    file_kind::project) {

                    const auto child_status =
                        visit(
                            child,
                            current_file,
                            dependency.path_type,
                            dependency.path,
                            child_file);

                    if (!succeeded(child_status)) {
                        active.erase(key);
                        return child_status;
                    }

                    continue;
                }

                if (files == nullptr) {
                    continue;
                }

                const auto* physical =
                    files->physical(
                        child_file);

                if (physical == nullptr) {
                    active.erase(key);
                    return server_status::
                        project_configuration_invalid;
                }

                if (physical->present()) {
                    continue;
                }

                file_acquire_job job;

                const auto prepared =
                    files->prepare_acquire(
                        child_file,
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

                    const auto acquisition =
                        result.kind ==
                            file_acquire_result_kind::missing
                        ? file_content_result::missing
                        : result.kind ==
                            file_acquire_result_kind::changed_during_read
                            ? file_content_result::changed_during_read
                            : result.kind ==
                                file_acquire_result_kind::allocation_failed
                                ? file_content_result::allocation_failed
                                : file_content_result::failed;

                    return report_acquisition_failure(
                        child,
                        operation,
                        diagnostics,
                        acquisition);
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
            }
        }
        catch (...) {
            active.erase(key);
            return server_status::io_error;
        }

        active.erase(key);
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
    project_configuration_manifest& output) {

    manifest_composer composer{
        root_project_path,
        operation,
        diagnostics,
        output,
        nullptr};

    return composer.compose();
}

server_status compose_project_configuration(
    const std::filesystem::path& root_project_path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    project_configuration_manifest& manifest,
    file_context& files) {

    manifest_composer composer{
        root_project_path,
        operation,
        diagnostics,
        manifest,
        &files};

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
