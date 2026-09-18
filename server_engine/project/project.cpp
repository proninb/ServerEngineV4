#include "project.hpp"

#include <utility>

namespace cw::server {

project::project(std::filesystem::path path)
    : project_path(std::move(path)) {
}

}
