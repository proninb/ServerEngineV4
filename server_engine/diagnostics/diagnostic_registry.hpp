/*
 * Compile-time diagnostic catalog and read-only lookup.
 */
#pragma once

#include "diagnostic_descriptor.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace cw::server {

// Read-only view over the immutable diagnostic descriptor catalog.
class diagnostic_registry_view final {
public:
    // Binds this view to an immutable descriptor range.
    constexpr explicit diagnostic_registry_view(
        std::span<const diagnostic_descriptor> values) noexcept
        : descriptors(values) {
    }

    // Returns the descriptor matching id, or nullptr when the id is unknown.
    [[nodiscard]] constexpr const diagnostic_descriptor* find(
        diagnostic_id id) const noexcept {

        for (const auto& descriptor : descriptors) {
            if (descriptor.id == id) {
                return &descriptor;
            }
        }

        return nullptr;
    }

private:
    // Non-owning view over static descriptor storage.
    std::span<const diagnostic_descriptor> descriptors;
};

// Complete V4 diagnostic catalog.
inline constexpr std::array diagnostic_descriptors{
    diagnostics::server_initialization_failed,
    diagnostics::configuration_read_failed,
    diagnostics::configuration_invalid_json,
    diagnostics::configuration_invalid,
    diagnostics::configuration_unsupported_version,
    diagnostics::communication_start_failed,
    diagnostics::communication_unsupported_transport,
    diagnostics::project_already_loaded,
    diagnostics::project_not_loaded,
    diagnostics::project_load_failed,
    diagnostics::project_build_incomplete,
    diagnostics::project_rebuild_incomplete,
    diagnostics::project_identity_invalid,
    diagnostics::project_identity_io_failed,
    diagnostics::project_startup_unsupported,
    diagnostics::project_invalid_json,
    diagnostics::project_invalid_configuration,
};

// Process-wide immutable registry view.
inline constexpr diagnostic_registry_view diagnostic_registry{
    diagnostic_descriptors,
};

// Compile-time gate: numeric diagnostic IDs must never collide.
consteval bool diagnostic_ids_unique() {
    for (std::size_t left = 0;
         left < diagnostic_descriptors.size();
         ++left) {

        for (std::size_t right = left + 1;
             right < diagnostic_descriptors.size();
             ++right) {

            if (diagnostic_descriptors[left].id ==
                diagnostic_descriptors[right].id) {
                return false;
            }
        }
    }

    return true;
}

// Compile-time gate: symbolic diagnostic names must never collide.
consteval bool diagnostic_names_unique() {
    for (std::size_t left = 0;
         left < diagnostic_descriptors.size();
         ++left) {

        for (std::size_t right = left + 1;
             right < diagnostic_descriptors.size();
             ++right) {

            if (diagnostic_descriptors[left].name ==
                diagnostic_descriptors[right].name) {
                return false;
            }
        }
    }

    return true;
}

static_assert(diagnostic_ids_unique());
static_assert(diagnostic_names_unique());

}
