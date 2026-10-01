/*
 * Platform TCP send policy.
 *
 * POSIX writes must never terminate the Server process through SIGPIPE.
 */
#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#else
#include <sys/socket.h>
#endif

#include <cstddef>

namespace cw::server {

#ifdef _WIN32
using tcp_native_socket = SOCKET;
#else
using tcp_native_socket = int;
#endif

[[nodiscard]] inline bool configure_tcp_send_socket(
    tcp_native_socket value) noexcept {

#ifdef _WIN32
    static_cast<void>(value);
    return true;
#else
#if defined(SO_NOSIGPIPE)
    const int enabled = 1;

    return ::setsockopt(
               value,
               SOL_SOCKET,
               SO_NOSIGPIPE,
               &enabled,
               sizeof(enabled)) == 0;
#else
    static_cast<void>(value);
    return true;
#endif
#endif
}

[[nodiscard]] inline int tcp_send_no_signal(
    tcp_native_socket value,
    const char* data,
    int size) noexcept {

#ifdef _WIN32
    return ::send(
        value,
        data,
        size,
        0);
#else
#if defined(MSG_NOSIGNAL)
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif

    return static_cast<int>(
        ::send(
            value,
            data,
            static_cast<std::size_t>(
                size),
            flags));
#endif
}

}
