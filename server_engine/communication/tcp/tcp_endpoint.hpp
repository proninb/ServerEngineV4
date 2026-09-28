/*
 * TCP/JSON endpoint.
 *
 * One endpoint owns one scalable non-blocking socket event loop. The event loop
 * is the single outbound writer for every connection it owns; Server work never
 * executes on the I/O thread.
 */
#pragma once

#include "../../configuration/server_configuration.hpp"
#include "../request_queue.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

namespace cw::server {

class tcp_connection_gate final {
public:
    void reset(std::uint32_t limit) noexcept;
    [[nodiscard]] bool acquire() noexcept;
    void release() noexcept;

private:
    std::atomic_uint32_t active = 0;
    std::uint32_t limit = 0;
};

class tcp_endpoint final {
public:
    tcp_endpoint();
    ~tcp_endpoint();

    tcp_endpoint(const tcp_endpoint&) = delete;
    tcp_endpoint& operator=(const tcp_endpoint&) = delete;

    [[nodiscard]] bool start(
        const communication_endpoint_configuration& configuration,
        request_queue& requests,
        tcp_connection_gate& gate,
        std::atomic_uint64_t& next_session_id,
        std::chrono::steady_clock::duration max_lifetime);

    void stop() noexcept;

private:
    class implementation;
    std::unique_ptr<implementation> impl;
};

}
