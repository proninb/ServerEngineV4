/*
 * Root Project immutable preprocessing configuration.
 *
 * One instance belongs to one construction operation. Nested project.json files
 * contribute composition inputs only; mutable #define/#undef state belongs to
 * frontend execution.
 */
#pragma once

#include <string>
#include <filesystem>
#include <vector>

namespace cw::server {

struct predefine_configuration final {
    std::string name;

    // Empty means #define NAME; otherwise contains the identifier replacement.
    std::string replacement;
};

struct preprocessor_configuration final {
    std::vector<predefine_configuration> predefines;
    std::vector<std::string> include_directories;
    // Resolution base only; the ordered locators above participate in identity.
    std::filesystem::path root_directory;
};

}
