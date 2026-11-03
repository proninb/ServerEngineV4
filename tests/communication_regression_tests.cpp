#include "communication/communication.hpp"
#include "communication/protocol/json_protocol.hpp"
#include "communication/tcp/tcp_endpoint.hpp"
#include "communication/tcp/tcp_send.hpp"
#include "communication/testing/fault_injection.hpp"
#include <iostream>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace cw::server;

namespace {

#ifdef _WIN32

using test_socket = SOCKET;

inline constexpr test_socket invalid_test_socket =
    INVALID_SOCKET;

class network_runtime final {
public:
    network_runtime() noexcept {
        WSADATA data{};

        started =
            WSAStartup(
                MAKEWORD(2, 2),
                &data) == 0;
    }

    ~network_runtime() {
        if (started) {
            WSACleanup();
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        return started;
    }

private:
    bool started = false;
};

void close_test_socket(
    test_socket value) noexcept {

    if (value != invalid_test_socket) {
        closesocket(value);
    }
}

[[nodiscard]] int test_socket_error() noexcept {
    return WSAGetLastError();
}

[[nodiscard]] bool test_would_block(
    int error) noexcept {

    return error == WSAEWOULDBLOCK;
}

[[nodiscard]] bool set_test_nonblocking(
    test_socket value) noexcept {

    u_long enabled = 1;

    return ioctlsocket(
               value,
               FIONBIO,
               &enabled) == 0;
}

#else

using test_socket = int;

inline constexpr test_socket invalid_test_socket =
    -1;

class network_runtime final {
public:
    [[nodiscard]] bool valid() const noexcept {
        return true;
    }
};

void close_test_socket(
    test_socket value) noexcept {

    if (value != invalid_test_socket) {
        ::close(value);
    }
}

[[nodiscard]] int test_socket_error() noexcept {
    return errno;
}

[[nodiscard]] bool test_would_block(
    int error) noexcept {

    return error == EAGAIN ||
        error == EWOULDBLOCK;
}

[[nodiscard]] bool set_test_nonblocking(
    test_socket value) noexcept {

    const auto flags =
        fcntl(
            value,
            F_GETFL,
            0);

    return flags >= 0 &&
        fcntl(
            value,
            F_SETFL,
            flags | O_NONBLOCK) == 0;
}

#endif

class test_socket_owner final {
public:
    test_socket_owner() noexcept = default;

    explicit test_socket_owner(
        test_socket socket) noexcept
        : value(socket) {
    }

    test_socket_owner(
        test_socket_owner&& other) noexcept
        : value(
              std::exchange(
                  other.value,
                  invalid_test_socket)) {
    }

    test_socket_owner& operator=(
        test_socket_owner&& other) noexcept {

        if (this != &other) {
            reset();

            value =
                std::exchange(
                    other.value,
                    invalid_test_socket);
        }

        return *this;
    }

    ~test_socket_owner() {
        reset();
    }

    test_socket_owner(
        const test_socket_owner&) = delete;

    test_socket_owner& operator=(
        const test_socket_owner&) = delete;

    void reset(
        test_socket socket =
            invalid_test_socket) noexcept {

        close_test_socket(value);
        value = socket;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return value != invalid_test_socket;
    }

    test_socket value =
        invalid_test_socket;
};

[[nodiscard]] std::uint16_t reserve_loopback_port() noexcept {
    test_socket_owner socket_value{
        ::socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP),
    };

    if (!socket_value) {
        return 0;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr =
        htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (::bind(
            socket_value.value,
            reinterpret_cast<sockaddr*>(
                &address),
            sizeof(address)) != 0) {

        return 0;
    }

#ifdef _WIN32
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif

    if (getsockname(
            socket_value.value,
            reinterpret_cast<sockaddr*>(
                &address),
            &length) != 0) {

        return 0;
    }

    return ntohs(
        address.sin_port);
}

[[nodiscard]] bool start_loopback_endpoint(
    tcp_endpoint& endpoint,
    request_queue& requests,
    tcp_connection_gate& gate,
    std::atomic_uint64_t& next_session,
    std::uint16_t& port) {

    for (std::size_t attempt = 0;
         attempt < 16;
         ++attempt) {

        port =
            reserve_loopback_port();

        if (port == 0) {
            continue;
        }

        communication_endpoint_configuration
            configuration;

        configuration.name = "regression";
        configuration.transport =
            transport_kind::tcp;
        configuration.protocol = "json";
        configuration.address = "127.0.0.1";
        configuration.port = port;

        if (endpoint.start(
                configuration,
                requests,
                gate,
                next_session,
                std::chrono::steady_clock::
                    duration::zero())) {

            return true;
        }
    }

    return false;
}

[[nodiscard]] test_socket_owner connect_loopback(
    std::uint16_t port) noexcept {

    test_socket_owner socket_value{
        ::socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP),
    };

    if (!socket_value) {
        return {};
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port =
        htons(port);

    if (inet_pton(
            AF_INET,
            "127.0.0.1",
            &address.sin_addr) != 1 ||
        ::connect(
            socket_value.value,
            reinterpret_cast<sockaddr*>(
                &address),
            sizeof(address)) != 0) {

        return {};
    }

    return socket_value;
}

[[nodiscard]] bool send_all(
    test_socket socket_value,
    std::string_view bytes) noexcept {

    std::size_t offset = 0;

    while (offset < bytes.size()) {
        const auto remaining =
            bytes.size() - offset;

#ifdef _WIN32
        const auto sent =
            ::send(
                socket_value,
                bytes.data() + offset,
                static_cast<int>(
                    remaining),
                0);
#else
#if defined(MSG_NOSIGNAL)
        constexpr int flags =
            MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif

        const auto sent =
            ::send(
                socket_value,
                bytes.data() + offset,
                remaining,
                flags);
#endif

        if (sent <= 0) {
            return false;
        }

        offset +=
            static_cast<std::size_t>(
                sent);
    }

    return true;
}

[[nodiscard]] bool wait_for_sessions(
    const std::atomic_uint64_t& next_session,
    std::uint64_t expected,
    std::chrono::milliseconds timeout) {

    const auto deadline =
        std::chrono::steady_clock::now() +
        timeout;

    while (std::chrono::steady_clock::now() <
           deadline) {

        if (next_session.load(
                std::memory_order_acquire) >=
            expected) {

            return true;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds{5});
    }

    return next_session.load(
               std::memory_order_acquire) >=
        expected;
}

void close_with_reset(
    test_socket_owner& socket_value) noexcept {

    if (!socket_value) {
        return;
    }

    linger value{};
    value.l_onoff = 1;
    value.l_linger = 0;

    static_cast<void>(
        setsockopt(
            socket_value.value,
            SOL_SOCKET,
            SO_LINGER,
            reinterpret_cast<const char*>(
                &value),
            sizeof(value)));

    socket_value.reset();
}

[[nodiscard]] bool socket_is_open(
    test_socket socket_value) noexcept {

    if (!set_test_nonblocking(
            socket_value)) {

        return false;
    }

    char byte = 0;

    const auto received =
        recv(
            socket_value,
            &byte,
            1,
            0);

    if (received > 0) {
        return true;
    }

    if (received == 0) {
        return false;
    }

    return test_would_block(
        test_socket_error());
}

[[nodiscard]] bool wait_for_closed(
    test_socket socket_value,
    std::chrono::milliseconds timeout) {

    const auto deadline =
        std::chrono::steady_clock::now() +
        timeout;

    while (std::chrono::steady_clock::now() <
           deadline) {

        if (!socket_is_open(
                socket_value)) {

            return true;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds{10});
    }

    return !socket_is_open(
        socket_value);
}

void unregister_subscription(
    void* context) noexcept {

    static_cast<std::atomic_uint32_t*>(
        context)->fetch_add(
            1,
            std::memory_order_relaxed);
}

[[nodiscard]] bool test_registry_shutdown_lifetime() {
    request_queue requests;
    communication communications;

    std::atomic_uint32_t
        unregister_count = 0;

    auto owner =
        communication_connection::create(
            client_session_id{101},
            4096);

    if (!owner ||
        owner->commit_login("Studio") !=
            client_session_login_result::
                success ||
        !owner->add_subscription(
            subscription_id{7},
            subscription_registration{
                &unregister_count,
                &unregister_subscription,
            }) ||
        !communications.register_connection(
            *owner)) {

        return false;
    }

    connection_close_control_message close;
    close.connection =
        owner.get();
    close.lifetime =
        owner->hold();

    if (!requests.push(
            communication_control_message{
                std::move(close)})) {

        return false;
    }

    owner =
        communication_connection_owner{};

    requests.stop_accepting_and_discard();
    communications.stop();

    return unregister_count.load(
               std::memory_order_relaxed) == 1;
}

[[nodiscard]] bool test_poll_snapshot_stability() {
    request_queue requests;
    tcp_connection_gate gate;
    gate.reset(8);

    std::atomic_uint64_t
        next_session = 1;

    tcp_endpoint endpoint;
    std::uint16_t port = 0;

    if (!start_loopback_endpoint(
            endpoint,
            requests,
            gate,
            next_session,
            port)) {

        return false;
    }

    auto first =
        connect_loopback(port);
    auto second =
        connect_loopback(port);
    auto third =
        connect_loopback(port);

    if (!first ||
        !second ||
        !third) {

        endpoint.stop();
        return false;
    }

    const char partial = 0;
    const std::string_view
        one_byte{
            &partial,
            1,
        };

    if (!send_all(
            first.value,
            one_byte) ||
        !send_all(
            second.value,
            one_byte) ||
        !send_all(
            third.value,
            one_byte) ||
        !wait_for_sessions(
            next_session,
            4,
            std::chrono::seconds{2})) {

        endpoint.stop();
        return false;
    }

    close_with_reset(first);

    std::this_thread::sleep_for(
        std::chrono::milliseconds{250});

    const auto second_alive =
        socket_is_open(
            second.value);

    const auto third_alive =
        socket_is_open(
            third.value);

    close_with_reset(second);
    close_with_reset(third);

    endpoint.stop();

    return second_alive &&
        third_alive;
}

[[nodiscard]] bool test_tcp_overload_closes_peer() {
    request_queue requests{
        request_queue_limits{
            1,
            1024u * 1024u,
        }};

    tcp_connection_gate gate;
    gate.reset(4);

    std::atomic_uint64_t
        next_session = 1;

    tcp_endpoint endpoint;
    std::uint16_t port = 0;

    if (!start_loopback_endpoint(
            endpoint,
            requests,
            gate,
            next_session,
            port)) {

        return false;
    }

    auto client =
        connect_loopback(port);

    if (!client ||
        !wait_for_sessions(
            next_session,
            2,
            std::chrono::seconds{2})) {

        endpoint.stop();
        return false;
    }

    std::string first;
    std::string second;

    encode_tcp_frame(
        R"({"request_id":1,"command":"LOGIN","arguments":{"name":"A"}})",
        first);

    encode_tcp_frame(
        R"({"request_id":2,"command":"LOGIN","arguments":{"name":"B"}})",
        second);

    std::string combined;
    combined.reserve(
        first.size() +
        second.size());

    combined += first;
    combined += second;

    if (!send_all(
            client.value,
            combined)) {

        endpoint.stop();
        return false;
    }

    const auto closed =
        wait_for_closed(
            client.value,
            std::chrono::seconds{2});

    client.reset();
    endpoint.stop();

    return closed;
}

// These tests exercise real socket cleanup and the production login transaction.
[[nodiscard]] bool wait_fault_consumed() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < deadline) {
        if (communication_test::pending.load() == communication_test::fault::none) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return false;
}

[[nodiscard]] bool pop_login(request_queue& requests, communication& registry,
    test_socket socket, std::uint64_t request = 1) {
    std::string frame;
    encode_tcp_frame("{\"request_id\":" + std::to_string(request) +
        ",\"command\":\"LOGIN\",\"arguments\":{\"name\":\"TrustedLAN\"}}", frame);
    if (!send_all(socket, frame)) return false;
    communication_control_message message;
    const auto deadline = std::chrono::system_clock::now() + std::chrono::seconds{2};
    while (requests.wait_pop_until(deadline, message)) {
        if (auto* login = std::get_if<login_control_message>(&message)) {
            if (!registry.login_connection(*login->connection, login->name)) return false;
            server_response response;
            response.status = server_status::success;
            return login->connection->enqueue_response(login->request, response);
        }
        if (auto* close = std::get_if<connection_close_control_message>(&message))
            registry.unregister_connection(close->connection->id());
        else return false;
    }
    return false;
}

[[nodiscard]] bool test_login_transaction_failures() {
    for (auto point : {communication_test::fault::registration,
                       communication_test::fault::login_name}) {
        communication registry;
        auto connection = communication_connection::create(client_session_id{100}, 4096);
        communication_test::pending.store(point);
        if (registry.login_connection(*connection, "Client") ||
            connection->logged_in() || !connection->closing() ||
            connection->reserve_request(request_id{2})) return false;
        request_identity identity;
        if (connection->make_request_identity(identity)) return false;
        registry.stop();
    }
    communication registry;
    auto first = communication_connection::create(client_session_id{100}, 4096);
    auto duplicate = communication_connection::create(client_session_id{100}, 4096);
    if (!registry.login_connection(*first, "A") ||
        registry.login_connection(*duplicate, "B") || !duplicate->closing() ||
        duplicate->logged_in() || !first->logged_in()) return false;
    auto invalid = communication_connection::create(client_session_id{101}, 4096);
    return !registry.login_connection(*invalid, "") && invalid->closing() &&
        !invalid->logged_in();
}

[[nodiscard]] bool test_tcp_failure_recovery(communication_test::fault point) {
    request_queue requests;
    tcp_connection_gate gate;
    gate.reset(1);
    std::atomic_uint64_t next_session = 1;
    tcp_endpoint endpoint;
    communication registry;
    std::uint16_t port = 0;
    if (!start_loopback_endpoint(endpoint, requests, gate, next_session, port)) return false;
    const bool accept_fault = point == communication_test::fault::accept_connection ||
        point == communication_test::fault::accept_buffer ||
        point == communication_test::fault::accept_insert ||
        point == communication_test::fault::accept_error;
    if (accept_fault) communication_test::pending.store(point);
    auto client = connect_loopback(port);
    if (!client) return false;
    if (!accept_fault && !wait_for_sessions(next_session, 2, std::chrono::seconds{2})) return false;
    if (!accept_fault) communication_test::pending.store(point);
    if (point == communication_test::fault::output) {
        if (!pop_login(requests, registry, client.value)) return false;
    } else if (point == communication_test::fault::close_post) {
        close_with_reset(client);
    } else if (point == communication_test::fault::accept_error) {
        if (!wait_fault_consumed() || !pop_login(requests, registry, client.value)) return false;
        endpoint.stop();
        return !endpoint.failed();
    } else if (!accept_fault) {
        if (!send_all(client.value, "x")) return false;
    }
    if (!wait_fault_consumed()) return false;
    if (client && !wait_for_closed(client.value, std::chrono::seconds{2})) return false;
    client.reset();
    if (endpoint.failed()) return false;
    // A cap of one makes a leaked gate permit observable on the next connection.
    auto replacement = connect_loopback(port);
    if (!replacement || !pop_login(requests, registry, replacement.value)) return false;
    endpoint.stop();
    return !endpoint.failed();
}

[[nodiscard]] bool test_tcp_failed_login_is_terminal() {
    for (auto point : {communication_test::fault::registration,
                       communication_test::fault::login_name}) {
        request_queue requests;
        tcp_connection_gate gate;
        gate.reset(1);
        std::atomic_uint64_t next_session = 1;
        tcp_endpoint endpoint;
        communication registry;
        std::uint16_t port = 0;
        if (!start_loopback_endpoint(endpoint, requests, gate, next_session, port)) return false;
        auto client = connect_loopback(port);
        if (!client) return false;
        communication_test::pending.store(point);
        if (pop_login(requests, registry, client.value)) return false;
        std::string next;
        encode_tcp_frame(R"({"request_id":2,"command":"SHUTDOWN"})", next);
        (void)send_all(client.value, next);
        if (!wait_for_closed(client.value, std::chrono::seconds{2})) return false;
        communication_control_message message;
        const auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds{100};
        while (requests.wait_pop_until(deadline, message)) {
            if (!std::holds_alternative<connection_close_control_message>(message)) return false;
        }
        auto replacement = connect_loopback(port);
        if (!replacement || !pop_login(requests, registry, replacement.value)) return false;
        endpoint.stop();
    }
    return true;
}

[[nodiscard]] bool test_receive_failure_keeps_other_peer() {
    request_queue requests;
    tcp_connection_gate gate;
    gate.reset(2);
    std::atomic_uint64_t next_session = 1;
    tcp_endpoint endpoint;
    communication registry;
    std::uint16_t port = 0;
    if (!start_loopback_endpoint(endpoint, requests, gate, next_session, port)) return false;
    auto bad = connect_loopback(port);
    auto healthy = connect_loopback(port);
    if (!bad || !healthy || !wait_for_sessions(next_session, 3, std::chrono::seconds{2})) return false;
    communication_test::pending.store(communication_test::fault::receive_buffer);
    if (!send_all(bad.value, "x") || !wait_for_closed(bad.value, std::chrono::seconds{2})) return false;
    if (!pop_login(requests, registry, healthy.value)) return false;
    endpoint.stop();
    return !endpoint.failed();
}

[[nodiscard]] bool test_registry_reaps_missing_close_notification() {
    communication registry;
    std::atomic_uint32_t unregistered = 0;
    auto connection = communication_connection::create(client_session_id{200}, 4096);
    if (!registry.login_connection(*connection, "Old") ||
        !connection->add_subscription(subscription_id{7},
            subscription_registration{&unregistered, &unregister_subscription})) return false;
    (void)connection->begin_close();
    // No close message: model a queue allocation failure.
    auto next = communication_connection::create(client_session_id{201}, 4096);
    if (!registry.login_connection(*next, "Next")) return false;
    return unregistered.load() == 1;
}

[[nodiscard]] bool test_worker_exception_boundary() {
    request_queue requests;
    tcp_connection_gate gate;
    gate.reset(1);
    std::atomic_uint64_t next_session = 1;
    tcp_endpoint endpoint;
    std::uint16_t port = 0;
    if (!start_loopback_endpoint(endpoint, requests, gate, next_session, port)) return false;
    auto client = connect_loopback(port);
    if (!client || !wait_for_sessions(next_session, 2, std::chrono::seconds{2})) return false;
    communication_test::pending.store(communication_test::fault::poll);
    if (!send_all(client.value, "x") || !wait_fault_consumed()) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!endpoint.failed() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    if (!endpoint.failed() || !wait_for_closed(client.value, std::chrono::seconds{2})) return false;
    endpoint.stop();
    if (!gate.acquire()) return false;
    gate.release();
    if (!start_loopback_endpoint(endpoint, requests, gate, next_session, port)) return false;
    endpoint.stop();
    return !endpoint.failed();
}

[[nodiscard]] bool test_requests_before_login_denied() {
    request_queue requests;
    tcp_connection_gate gate;
    gate.reset(1);
    std::atomic_uint64_t next_session = 1;
    tcp_endpoint endpoint;
    std::uint16_t port = 0;
    if (!start_loopback_endpoint(endpoint, requests, gate, next_session, port)) return false;
    auto client = connect_loopback(port);
    if (!client) return false;
    std::string frame;
    encode_tcp_frame(R"({"request_id":1,"command":"SHUTDOWN"})", frame);
    if (!send_all(client.value, frame)) return false;
    communication_control_message message;
    const bool queued = requests.wait_pop_until(
        std::chrono::system_clock::now() + std::chrono::milliseconds{250}, message);
    communication registry;
    if (queued || !pop_login(requests, registry, client.value, 2)) return false;
    endpoint.stop();
    return true;
}

[[nodiscard]] bool test_posix_sigpipe_boundary() {
#ifdef _WIN32
    return true;
#else
    int pair[2]{
        -1,
        -1,
    };

    if (socketpair(
            AF_UNIX,
            SOCK_STREAM,
            0,
            pair) != 0) {

        return false;
    }

    if (!configure_tcp_send_socket(
            pair[0])) {

        ::close(pair[0]);
        ::close(pair[1]);
        return false;
    }

    ::close(pair[1]);

    const char byte = 1;

    const auto sent =
        tcp_send_no_signal(
            pair[0],
            &byte,
            1);

    ::close(pair[0]);

    return sent < 0;
#endif
}

}

int main() {
    network_runtime network;

    if (!network.valid()) {
        return 1;
    }

    if (!test_registry_shutdown_lifetime()) {
        return 2;
    }

    if (!test_poll_snapshot_stability()) {
        return 3;
    }

    if (!test_tcp_overload_closes_peer()) {
        return 4;
    }

    if (!test_login_transaction_failures()) return 10;
    if (!test_requests_before_login_denied()) return 11;
    if (!test_worker_exception_boundary()) return 12;
    if (!test_tcp_failed_login_is_terminal()) return 13;
    if (!test_receive_failure_keeps_other_peer()) return 14;
    if (!test_registry_reaps_missing_close_notification()) return 15;
    for (auto point : {communication_test::fault::accept_connection,
                       communication_test::fault::accept_buffer,
                       communication_test::fault::accept_insert,
                       communication_test::fault::accept_error,
                       communication_test::fault::receive_buffer,
                       communication_test::fault::receive_error,
                       communication_test::fault::output,
                       communication_test::fault::close_post}) {
        if (!test_tcp_failure_recovery(point)) {
            std::cerr << "TCP fault recovery failed: " << static_cast<int>(point) << "\n";
            return 20 + static_cast<int>(point);
        }
    }

    if (!test_posix_sigpipe_boundary()) {
        return 5;
    }

    return 0;
}
