#include "project_path.hpp"

#include <system_error>

namespace cw::server {

project_path_result resolve_project_path(
    const std::filesystem::path& path,
    std::filesystem::path& output) noexcept {

    output.clear();

    try {
        std::error_code error;

        const auto absolute =
            std::filesystem::absolute(
                path,
                error);

        if (error) {
            return project_path_result::
                failed;
        }

        output =
            absolute.lexically_normal();

        return project_path_result::
            success;
    }
    catch (...) {
        output.clear();

        return project_path_result::
            failed;
    }
}

}
