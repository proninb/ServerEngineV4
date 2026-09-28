/*
 * Move-only intrusive lifetime holders for one Communication connection.
 *
 * connection_lifetime_token is an opaque RAII holder. It deliberately does not
 * depend on communication_connection implementation symbols, so generic
 * protocol/request code can own a token without linking the full connection core.
 *
 * The last release may happen on any thread. Connection destruction therefore
 * must never perform thread-affine transport I/O.
 */
#pragma once

#include <utility>

namespace cw::server {

class communication_connection;

class connection_lifetime_token final {
public:
    connection_lifetime_token() noexcept = default;

    connection_lifetime_token(
        connection_lifetime_token&& other) noexcept
        : context(std::exchange(other.context, nullptr)),
          release_callback(
              std::exchange(
                  other.release_callback,
                  nullptr)) {
    }

    connection_lifetime_token& operator=(
        connection_lifetime_token&& other) noexcept {

        if (this != &other) {
            reset();

            context =
                std::exchange(
                    other.context,
                    nullptr);

            release_callback =
                std::exchange(
                    other.release_callback,
                    nullptr);
        }

        return *this;
    }

    ~connection_lifetime_token() {
        reset();
    }

    connection_lifetime_token(const connection_lifetime_token&) = delete;
    connection_lifetime_token& operator=(const connection_lifetime_token&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept {
        return release_callback != nullptr;
    }

private:
    using release_function =
        void (*)(void* context) noexcept;

    friend class communication_connection;

    connection_lifetime_token(
        void* value_context,
        release_function value_release) noexcept
        : context(value_context),
          release_callback(value_release) {
    }

    void reset() noexcept {
        if (release_callback != nullptr) {
            auto callback = release_callback;
            auto* value = context;

            context = nullptr;
            release_callback = nullptr;

            callback(value);
        }
    }

    void* context = nullptr;
    release_function release_callback = nullptr;
};

class communication_connection_owner final {
public:
    communication_connection_owner() noexcept = default;
    communication_connection_owner(communication_connection_owner&& other) noexcept;
    communication_connection_owner& operator=(communication_connection_owner&& other) noexcept;
    ~communication_connection_owner();

    communication_connection_owner(const communication_connection_owner&) = delete;
    communication_connection_owner& operator=(const communication_connection_owner&) = delete;

    [[nodiscard]] communication_connection* get() const noexcept;
    [[nodiscard]] communication_connection& operator*() const noexcept;
    [[nodiscard]] communication_connection* operator->() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;

private:
    friend class communication_connection;

    explicit communication_connection_owner(
        communication_connection* connection) noexcept;

    void reset() noexcept;

    communication_connection* connection = nullptr;
};

}
