#include "project.hpp"

#include <utility>

namespace cw::server {

project::project(
    std::filesystem::path path,
    read_only_file_mapping&& mapping,
    compiled_project_view compiled)
    : project_path(std::move(path)),
      compiled_mapping(std::move(mapping)),
      compiled_view(compiled) {
}

}
