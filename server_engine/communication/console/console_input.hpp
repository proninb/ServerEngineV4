/*
 * Platform-neutral interruptible console input abstraction.
 *
 * read_line() may block while waiting for terminal input, but interrupt()
 * must always wake that wait immediately so shutdown never requires Enter.
 */
#pragma once

#include <memory>
#include <string>

namespace cw::server {

// Owns the platform-specific resources required for interruptible line input.
class console_input final {
public:
    // Creates an unopened console input object.
    console_input();

    // Releases platform resources if still open.
    ~console_input();

    // Opens stdin/console resources and the platform wake mechanism.
    [[nodiscard]] bool open();

    // Waits for one complete input line or returns false after interrupt/end-of-input/error.
    [[nodiscard]] bool read_line(std::string& line);

    // Wakes a blocked read_line() from another thread.
    void interrupt() noexcept;

    // Releases console and wake resources. Safe to call repeatedly.
    void close() noexcept;

private:
    // Hides native platform resources from platform-neutral callers.
    struct implementation;

    // Sole owner of the platform implementation state.
    std::unique_ptr<implementation> state;
};

}
