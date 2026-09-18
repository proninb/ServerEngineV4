#include "project_configuration_manifest.hpp"

#include "project_configuration_loader.hpp"
#include "project_path.hpp"
#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

#include <cstdint>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cw::server {
namespace {

[[nodiscard]] std::filesystem::path absolute_normalized(
    const std::filesystem::path& path) {

    std::error_code error;
    const auto absolute =
        std::filesystem::absolute(
            path,
            error);

    return (error ? path : absolute).lexically_normal();
}

[[nodiscard]] std::filesystem::path manifest_path(
    const std::filesystem::path& root_directory,
    const std::filesystem::path& absolute_path) {

    const auto relative =
        absolute_path.lexically_relative(
            root_directory);

    if (relative.empty() ||
        relative.is_absolute()) {

        return {};
    }

    return relative.lexically_normal();
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
    canonical.append("CWCFG001", 8);

    append_u64(
        canonical,
        static_cast<std::uint64_t>(
            files.size()));

    for (const auto& file : files) {
        const auto path =
            file.path.generic_string();

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
        hash_project_content(
            canonical);

    project_configuration_hash output;
    output.bytes = digest.bytes;
    return output;
}

[[nodiscard]] server_status report_acquisition_failure(
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    project_snapshot_result result) {

    diagnostics.emit(
        diagnostic(
            diagnostics::project_configuration_read_failed,
            operation)
            .file(path)
            .detail(
                result == project_snapshot_result::missing
                    ? "Project configuration file does not exist"
                    : result == project_snapshot_result::changed_during_read
                        ? "Project configuration changed during stable acquisition"
                        : result == project_snapshot_result::allocation_failed
                            ? "Cannot allocate Project configuration snapshot"
                            : "Cannot acquire Project configuration snapshot")
            .build());

    return result == project_snapshot_result::allocation_failed
        ? server_status::io_error
        : server_status::project_configuration_invalid;
}

class manifest_composer final {
public:
    manifest_composer(
        const std::filesystem::path& root_project_path,
        operation_id operation,
        diagnostic_collection& diagnostics,
        project_configuration_manifest& output)
        : root_path(
              absolute_normalized(
                  root_project_path)),
          root_directory(
              root_path.parent_path()),
          operation(operation),
          diagnostics(diagnostics),
          output(output) {

        visited.reserve(32);
        active.reserve(16);
    }

    [[nodiscard]] server_status compose() {
        output = {};

        const auto status =
            visit(root_path);

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
        const std::filesystem::path& absolute_path) {

        project_path_key key;

        const auto key_result =
            make_project_path_key(
                absolute_path,
                key);

        if (key_result !=
            project_path_key_result::
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
            return server_status::success;
        }

        active.insert(key);

        project_content_snapshot snapshot;

        const auto acquired =
            acquire_project_content(
                absolute_path,
                snapshot);

        if (acquired !=
            project_snapshot_result::acquired) {

            active.erase(key);

            return report_acquisition_failure(
                absolute_path,
                operation,
                diagnostics,
                acquired);
        }

        std::vector<std::filesystem::path>
            project_references;

        const auto status =
            read_project_configuration(
                snapshot.bytes,
                absolute_path,
                operation,
                diagnostics,
                project_references);

        if (!succeeded(status)) {
            active.erase(key);
            return status;
        }

        try {
            project_configuration_file_proof proof;
            proof.path =
                manifest_path(
                    root_directory,
                    absolute_path);

            if (proof.path.empty()) {
                active.erase(key);

                diagnostics.emit(
                    diagnostic(
                        diagnostics::project_invalid_configuration,
                        operation)
                        .file(absolute_path)
                        .detail(
                            "Project configuration path cannot be represented relative to the root Project directory")
                        .build());

                return server_status::
                    project_configuration_invalid;
            }

            proof.content_hash =
                snapshot.content_hash;
            proof.change_token =
                snapshot.change_token;
            proof.change_token_available =
                snapshot.change_token_available;

            output.files.push_back(
                std::move(proof));

            visited.insert(key);

            for (const auto& reference :
                 project_references) {

                const auto child =
                    absolute_normalized(
                        absolute_path.parent_path() /
                        reference);

                const auto child_status =
                    visit(child);

                if (!succeeded(child_status)) {
                    active.erase(key);
                    return child_status;
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

    std::filesystem::path root_path;
    std::filesystem::path root_directory;
    operation_id operation;
    diagnostic_collection& diagnostics;
    project_configuration_manifest& output;
    std::unordered_set<project_path_key, project_path_key_hash> visited;
    std::unordered_set<project_path_key, project_path_key_hash> active;
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
        output};

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

    const auto root =
        absolute_normalized(
            root_project_path);

    const auto root_directory =
        root.parent_path();

    if (persisted.files.front().path !=
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

    for (const auto& file :
         persisted.files) {

        const auto path =
            absolute_normalized(
                root_directory /
                file.path);

        if (file.change_token_available &&
            file.change_token) {

            bool unchanged = false;

            const auto proof =
                prove_file_unchanged(
                    path,
                    file.change_token,
                    unchanged);

            if (proof ==
                project_token_result::missing) {

                verification =
                    project_configuration_manifest_verification::
                        changed;

                return server_status::success;
            }

            if (proof ==
                project_token_result::failed) {

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
                    project_token_result::available &&
                unchanged) {

                continue;
            }
        }

        project_content_snapshot snapshot;

        const auto acquired =
            acquire_project_content(
                path,
                snapshot);

        if (acquired ==
            project_snapshot_result::missing) {

            verification =
                project_configuration_manifest_verification::
                    changed;

            return server_status::success;
        }

        if (acquired !=
            project_snapshot_result::acquired) {

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
