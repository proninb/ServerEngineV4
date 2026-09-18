/*
 * POSIX interruptible console input implementation.
 *
 * The input thread blocks in poll() on:
 * - STDIN_FILENO;
 * - the read end of a private wake pipe.
 *
 * interrupt() writes one byte to the pipe so shutdown wakes immediately.
 */
#include "console_input.hpp"

#if !defined(_WIN32)

#include <cerrno>
#include <poll.h>
#include <unistd.h>

namespace cw::server {

struct console_input::implementation {
    // Read end observed by poll().
    int wake_read = -1;

    // Write end used by interrupt().
    int wake_write = -1;

    // Bytes read from stdin that do not yet form a complete line.
    std::string pending;
};

console_input::console_input()
    : state(std::make_unique<implementation>()) {
}

console_input::~console_input() {
    close();
}

bool console_input::open() {
    // Re-open is intentionally idempotent for the current object lifetime.
    if (state->wake_read != -1) {
        return true;
    }

    int handles[2]{};

    if (::pipe(handles) != 0) {
        return false;
    }

    state->wake_read = handles[0];
    state->wake_write = handles[1];

    return true;
}

bool console_input::read_line(std::string& line) {
    while (true) {
        const auto newline =
            state->pending.find('\n');

        if (newline != std::string::npos) {
            line =
                state->pending.substr(0, newline);

            state->pending.erase(
                0,
                newline + 1);

            if (!line.empty() &&
                line.back() == '\r') {
                line.pop_back();
            }

            return true;
        }

        pollfd handles[2] = {
            {STDIN_FILENO, POLLIN, 0},
            {state->wake_read, POLLIN, 0},
        };

        int result = 0;

        do {
            result =
                ::poll(handles, 2, -1);
        } while (
            result < 0 &&
            errno == EINTR);

        if (result <= 0) {
            return false;
        }

        if ((handles[1].revents & POLLIN) != 0) {
            char ignored = 0;

            static_cast<void>(
                ::read(
                    state->wake_read,
                    &ignored,
                    1));

            return false;
        }

        if ((handles[0].revents &
             (POLLIN | POLLHUP)) != 0) {
            char buffer[256];

            const auto count =
                ::read(
                    STDIN_FILENO,
                    buffer,
                    sizeof(buffer));

            if (count <= 0) {
                return false;
            }

            state->pending.append(
                buffer,
                static_cast<std::size_t>(count));
        }
    }
}

void console_input::interrupt() noexcept {
    if (state->wake_write == -1) {
        return;
    }

    const char signal = 1;

    static_cast<void>(
        ::write(
            state->wake_write,
            &signal,
            1));
}

void console_input::close() noexcept {
    if (state->wake_read != -1) {
        ::close(state->wake_read);
        state->wake_read = -1;
    }

    if (state->wake_write != -1) {
        ::close(state->wake_write);
        state->wake_write = -1;
    }

    state->pending.clear();
}

}

#endif
