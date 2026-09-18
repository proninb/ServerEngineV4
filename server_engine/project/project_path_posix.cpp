#include "project_path.hpp"

#if defined(_WIN32)
#error project_path_posix.cpp must not be built on Windows
#endif

namespace cw::server {

project_path_result make_project_path_key(
    const std::filesystem::path& path,
    project_path_key& output) noexcept {

    output = {};

    try {
        output.value =
            path.lexically_normal();

        return project_path_result::
            success;
    }
    catch (...) {
        output = {};

        return project_path_result::
            failed;
    }
}

}
