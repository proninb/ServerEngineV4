/*
 * Resident Project ownership boundary.
 *
 * Project owns the successfully parsed configuration of exactly one project.json.
 * Nested Project composition, BUILD, Graph, Runtime, and persistence are separate
 * later stages.
 */
#pragma once

#include "project_configuration.hpp"

#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

#include <filesystem>

namespace cw::server {

class project final {
public:
    [[nodiscard]] server_status load(
        const std::filesystem::path& configuration_path,
        operation_id operation,
        diagnostic_collection& diagnostics);

    [[nodiscard]] const std::filesystem::path& configuration_path() const noexcept {
        return path;
    }

    [[nodiscard]] const project_configuration& configuration() const noexcept {
        return configuration_value;
    }

private:
    std::filesystem::path path;
    project_configuration configuration_value;
};

}
