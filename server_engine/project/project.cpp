#include "project.hpp"

#include <utility>

namespace cw::server {

project::project(
    std::filesystem::path path,
    read_only_file_mapping&& mapping,
    compiled_project_view compiled,
    fixed_shared_memory&& runtime_memory,
    std::uint64_t runtime_size)
    : project_path(std::move(path)),
      compiled_mapping(std::move(mapping)),
      compiled_view(compiled),
      shared_memory(std::move(runtime_memory)),
      runtime_size_value(runtime_size) {
}

}
