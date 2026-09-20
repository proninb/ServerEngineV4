/*
 * Project-local preprocessing configuration.
 *
 * One instance belongs to one project.json construction scope. It contains only
 * immutable initial preprocessing inputs; mutable preprocessing execution state
 * belongs to the frontend.
 */
#pragma once

#include <string>
#include <vector>

namespace cw::server {

struct project_predefine final {
    std::string name;

    // Empty means #define NAME; otherwise contains the identifier replacement.
    std::string replacement;
};

struct project_preprocessor_configuration final {
    std::vector<project_predefine> predefines;
};

}
