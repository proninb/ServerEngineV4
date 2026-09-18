#include "project_path.hpp"

#if defined(_WIN32)
#error project_path_posix.cpp must not be built on Windows
#endif

namespace cw::server {

project_path_key make_project_path_key(
    const std::filesystem::path& path) {

    return {
        path.lexically_normal(),
    };
}

}
