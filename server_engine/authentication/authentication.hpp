/*
 * Server-level Authentication subsystem lifecycle.
 *
 * Authentication starts before Communication and is independent from Client
 * Session semantics. Provider-specific credential/token contracts are not part
 * of this slice.
 */
#pragma once

#include "../configuration/server_configuration.hpp"
#include "../server_status.hpp"

namespace cw::server {

// Owns process-lifetime Authentication readiness for one Server instance.
class authentication_service final {
public:
    // Initializes the configured Authentication mode.
    // Modes without an implemented provider fail closed.
    [[nodiscard]] server_status start(
        const authentication_configuration& configuration) noexcept;

    // Releases Authentication provider state.
    void stop() noexcept;

    // True after successful initialization, including explicit mode=none.
    [[nodiscard]] bool ready() const noexcept;

    // Returns the initialized mode. Meaningful only while ready().
    [[nodiscard]] authentication_mode mode() const noexcept;

private:
    authentication_mode active_mode = authentication_mode::none;
    bool initialized = false;
};

}
