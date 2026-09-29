/*
 * Streaming project.json schema/composition-reference boundary.
 *
 * Construction dependencies remain separate from Project runtime resource
 * references. Root "ic" is never emitted as file_kind/DAG/G input.
 */
#pragma once

#include "project_configuration_manifest.hpp"
#include "preprocessor_configuration.hpp"
#include "file/file_kind.hpp"
#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cw::server {

enum class project_configuration_scope : std::uint8_t {
    root,
    nested,
};

struct project_configuration_dependency final {
    file_kind kind = file_kind::project;
    project_configuration_path_type path_type =
        project_configuration_path_type::relative;
    std::filesystem::path path;
};

struct project_runtime_configuration final {
    std::filesystem::path ic;
};

[[nodiscard]] server_status read_project_configuration(
    std::string& bytes,
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::vector<project_configuration_dependency>& dependencies,
    project_configuration_scope scope,
    preprocessor_configuration* preprocessor,
    project_runtime_configuration* runtime = nullptr);

[[nodiscard]] server_status load_project_runtime_configuration(
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    project_runtime_configuration& output);

}
