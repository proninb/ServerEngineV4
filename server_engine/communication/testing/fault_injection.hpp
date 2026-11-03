#pragma once

// Compiled only into the communication regression target; no production knobs.
#ifdef CW_COMMUNICATION_FAULT_INJECTION
#include <atomic>
#include <new>
namespace cw::server::communication_test {
enum class fault { none, registration, login_name, accept_connection,
    accept_buffer, accept_insert, receive_buffer, output, poll,
    accept_error, receive_error, close_post };
inline std::atomic<fault> pending{fault::none};
inline bool consume(fault point) noexcept {
    return pending.compare_exchange_strong(point, fault::none);
}
inline void fail(fault point) {
    if (consume(point)) throw std::bad_alloc{};
}
}
#define CW_COMMUNICATION_FAULT(point) ::cw::server::communication_test::fail(::cw::server::communication_test::fault::point)
#define CW_COMMUNICATION_FAILS(point) ::cw::server::communication_test::consume(::cw::server::communication_test::fault::point)
#else
#define CW_COMMUNICATION_FAULT(point) ((void)0)
#define CW_COMMUNICATION_FAILS(point) false
#endif
