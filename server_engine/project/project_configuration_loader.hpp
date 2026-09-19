/*
 * Streaming project.json schema/composition-reference boundary.
 *
 * One file is validated without materializing a project_configuration tree.
 * Direct child Project references preserve locator semantics and declaration
 * order so recursive composition can be owned by the Project composition layer.
 */
#pragma once

#include "project_configuration_manifest.hpp"
#include "file/file_kind.hpp"
#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace cw::server {

struct project_configuration_dependency final {
    file_kind kind = file_kind::project;
    project_configuration_path_type path_type =
        project_configuration_path_type::relative;
    std::filesystem::path path;
};

[[nodiscard]] server_status read_project_configuration(
    std::string& bytes,
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::vector<project_configuration_dependency>& dependencies);

}
