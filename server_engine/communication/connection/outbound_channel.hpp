/*
 * Bounded per-connection outbound channel.
 *
 * Producers enqueue semantic messages. One writer dequeues them, assigns the
 * connection sequence, encodes/writes them, and reports local write completion.
 */
#pragma once

#include "../protocol/protocol_contract.hpp"
#include "../server_response.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <variant>
#include <vector>

namespace cw::server {

struct protocol_response final {
    request_id request;
    server_response result;
    bool release_request_id = true;
};

struct subscription_data final {
    subscription_id subscription;
    subscription_sequence sequence;
    std::vector<std::byte> payload;
};

using outbound_payload =
    std::variant<
        protocol_response,
        server_state_action,
        client_action,
        subscription_data>;

struct outbound_message final {
    std::size_t estimated_bytes = 0;
    outbound_payload payload;
};

struct outbound_write final {
    std::uint64_t connection_sequence = 0;
    outbound_message message;
};

class outbound_channel final {
public:
    explicit outbound_channel(std::size_t byte_limit) noexcept;

    [[nodiscard]] bool enqueue(outbound_message message);

    // Single-writer API. Sequence is assigned here, never by producers.
    [[nodiscard]] bool wait_next(outbound_write& output);

    // Single-writer non-blocking dequeue used by socket event loops.
    [[nodiscard]] bool try_next(outbound_write& output);

    void mark_written(std::uint64_t connection_sequence) noexcept;

    [[nodiscard]] bool wait_until_written(
        std::uint64_t connection_sequence,
        std::chrono::milliseconds timeout);

    // Idempotent open -> closing transition; queued output is discarded.
    [[nodiscard]] bool close() noexcept;

    [[nodiscard]] bool closing() const noexcept;
    [[nodiscard]] std::size_t queued_bytes() const noexcept;

private:
    const std::size_t limit;

    mutable std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable written;

    std::deque<outbound_message> queue;
    std::size_t bytes = 0;
    bool is_closing = false;

    // Single-writer state.
    std::uint64_t next_sequence = 1;
    std::uint64_t last_written = 0;
};

}
