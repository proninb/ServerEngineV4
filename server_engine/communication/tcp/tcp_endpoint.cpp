#include "tcp_endpoint.hpp"

#include "../connection/communication_connection.hpp"
#include "../protocol/json_protocol.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace cw::server {
namespace {

#ifdef _WIN32

using native_socket = SOCKET;
using poll_descriptor = WSAPOLLFD;

inline constexpr native_socket invalid_socket_value =
    INVALID_SOCKET;

void close_socket(native_socket value) noexcept {
    if (value != invalid_socket_value) {
        closesocket(value);
    }
}

[[nodiscard]] int socket_error() noexcept {
    return WSAGetLastError();
}

[[nodiscard]] bool would_block(
    int error) noexcept {

    return error == WSAEWOULDBLOCK;
}

[[nodiscard]] bool set_nonblocking(
    native_socket value) noexcept {

    u_long enabled = 1;

    return ioctlsocket(
               value,
               FIONBIO,
               &enabled) == 0;
}

[[nodiscard]] int poll_sockets(
    poll_descriptor* descriptors,
    unsigned long count,
    int timeout_ms) noexcept {

    return WSAPoll(
        descriptors,
        count,
        timeout_ms);
}

#else

using native_socket = int;
using poll_descriptor = pollfd;

inline constexpr native_socket invalid_socket_value =
    -1;

void close_socket(native_socket value) noexcept {
    if (value != invalid_socket_value) {
        ::close(value);
    }
}

[[nodiscard]] int socket_error() noexcept {
    return errno;
}

[[nodiscard]] bool would_block(
    int error) noexcept {

    return error == EAGAIN ||
        error == EWOULDBLOCK;
}

[[nodiscard]] bool set_nonblocking(
    native_socket value) noexcept {

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

[[nodiscard]] int poll_sockets(
    poll_descriptor* descriptors,
    std::size_t count,
    int timeout_ms) noexcept {

    return ::poll(
        descriptors,
        static_cast<nfds_t>(count),
        timeout_ms);
}

#endif

struct socket_owner final {
    native_socket value =
        invalid_socket_value;

    socket_owner() noexcept = default;

    explicit socket_owner(
        native_socket socket) noexcept
        : value(socket) {
    }

    socket_owner(
        socket_owner&& other) noexcept
        : value(
              std::exchange(
                  other.value,
                  invalid_socket_value)) {
    }

    socket_owner& operator=(
        socket_owner&& other) noexcept {

        if (this != &other) {
            reset();

            value =
                std::exchange(
                    other.value,
                    invalid_socket_value);
        }

        return *this;
    }

    ~socket_owner() {
        reset();
    }

    socket_owner(const socket_owner&) = delete;
    socket_owner& operator=(const socket_owner&) = delete;

    void reset(
        native_socket socket =
            invalid_socket_value) noexcept {

        close_socket(value);
        value = socket;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return value != invalid_socket_value;
    }
};

[[nodiscard]] bool create_listener(
    const communication_endpoint_configuration& configuration,
    socket_owner& output) {

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* addresses = nullptr;

    const auto port =
        std::to_string(
            configuration.port);

    const char* host =
        configuration.address.empty()
            ? nullptr
            : configuration.address.c_str();

    if (getaddrinfo(
            host,
            port.c_str(),
            &hints,
            &addresses) != 0) {

        return false;
    }

    bool opened = false;

    for (auto* address = addresses;
         address != nullptr;
         address = address->ai_next) {

        socket_owner candidate{
            socket(
                address->ai_family,
                address->ai_socktype,
                address->ai_protocol),
        };

        if (!candidate) {
            continue;
        }

        int reuse = 1;

        static_cast<void>(
            setsockopt(
                candidate.value,
                SOL_SOCKET,
                SO_REUSEADDR,
                reinterpret_cast<const char*>(
                    &reuse),
                sizeof(reuse)));

        if (bind(
                candidate.value,
                address->ai_addr,
                static_cast<int>(
                    address->ai_addrlen)) != 0) {

            continue;
        }

        if (listen(
                candidate.value,
                SOMAXCONN) != 0 ||
            !set_nonblocking(
                candidate.value)) {

            continue;
        }

        output =
            std::move(candidate);

        opened = true;
        break;
    }

    freeaddrinfo(addresses);
    return opened;
}

[[nodiscard]] bool create_wakeup_pair(
    socket_owner& receiver,
    socket_owner& sender) {

    socket_owner receive_socket{
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP),
    };

    socket_owner send_socket{
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP),
    };

    if (!receive_socket ||
        !send_socket) {

        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr =
        htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (bind(
            receive_socket.value,
            reinterpret_cast<sockaddr*>(
                &address),
            sizeof(address)) != 0) {

        return false;
    }

#ifdef _WIN32
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif

    if (getsockname(
            receive_socket.value,
            reinterpret_cast<sockaddr*>(
                &address),
            &length) != 0) {

        return false;
    }

    if (connect(
            send_socket.value,
            reinterpret_cast<sockaddr*>(
                &address),
            sizeof(address)) != 0) {

        return false;
    }

    if (!set_nonblocking(
            receive_socket.value) ||
        !set_nonblocking(
            send_socket.value)) {

        return false;
    }

    receiver =
        std::move(receive_socket);

    sender =
        std::move(send_socket);

    return true;
}

void drain_wakeup(
    native_socket socket_value) noexcept {

    std::array<char, 128> buffer{};

    while (true) {
        const auto received =
            recv(
                socket_value,
                buffer.data(),
                static_cast<int>(
                    buffer.size()),
                0);

        if (received > 0) {
            continue;
        }

        if (received < 0 &&
            would_block(
                socket_error())) {

            return;
        }

        return;
    }
}

}

void tcp_connection_gate::reset(
    std::uint32_t value) noexcept {

    limit = value;
    active.store(
        0,
        std::memory_order_release);
}

bool tcp_connection_gate::acquire() noexcept {
    auto current =
        active.load(
            std::memory_order_acquire);

    while (current < limit) {
        if (active.compare_exchange_weak(
                current,
                current + 1,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {

            return true;
        }
    }

    return false;
}

void tcp_connection_gate::release() noexcept {
    const auto previous =
        active.fetch_sub(
            1,
            std::memory_order_acq_rel);

    if (previous == 0) {
        active.store(
            0,
            std::memory_order_release);
    }
}

class tcp_endpoint::implementation final {
public:
    struct peer final {
        socket_owner socket;
        communication_connection_owner connection;

        std::vector<std::byte> receive_buffer;
        std::size_t receive_offset = 0;

        std::string send_buffer;
        std::size_t send_offset = 0;
        std::optional<outbound_write> sending;

        std::chrono::steady_clock::time_point accepted_at;
        bool gate_acquired = false;
        bool close_posted = false;
    };

    [[nodiscard]] bool start(
        const communication_endpoint_configuration& configuration,
        request_queue& request_queue_value,
        tcp_connection_gate& gate_value,
        std::atomic_uint64_t& next_session_value,
        std::chrono::steady_clock::duration max_lifetime_value) {

        stop();

#ifdef _WIN32
        WSADATA data{};

        if (WSAStartup(
                MAKEWORD(2, 2),
                &data) != 0) {

            return false;
        }

        winsock_started = true;
#endif

        requests =
            &request_queue_value;

        gate =
            &gate_value;

        next_session =
            &next_session_value;

        max_lifetime =
            max_lifetime_value;

        if (!create_listener(
                configuration,
                listener) ||
            !create_wakeup_pair(
                wake_receiver,
                wake_sender)) {

            stop();
            return false;
        }

        stopping.store(
            false,
            std::memory_order_release);

        worker =
            std::jthread(
                [this] {
                    run();
                });

        return true;
    }

    void stop() noexcept {
        if (worker.joinable()) {
            stopping.store(
                true,
                std::memory_order_release);

            wake();
            worker.join();
        }

        for (auto& value : peers) {
            close_peer(
                value,
                false);
        }

        peers.clear();
        listener.reset();
        wake_receiver.reset();
        wake_sender.reset();

        requests = nullptr;
        gate = nullptr;
        next_session = nullptr;

#ifdef _WIN32
        if (winsock_started) {
            WSACleanup();
            winsock_started = false;
        }
#endif
    }

    static void notify(
        void* context) noexcept {

        static_cast<implementation*>(
            context)->wake();
    }

private:
    void wake() noexcept {
        if (!wake_sender) {
            return;
        }

        const char value = 1;

        static_cast<void>(
            send(
                wake_sender.value,
                &value,
                1,
                0));
    }

    void run() {
        while (!stopping.load(
                   std::memory_order_acquire)) {

            expire_peers();
            prepare_output();

            std::vector<poll_descriptor>
                descriptors;

            descriptors.reserve(
                2 + peers.size());

            poll_descriptor listener_poll{};
            listener_poll.fd =
                listener.value;
            listener_poll.events =
                POLLIN;
            descriptors.push_back(
                listener_poll);

            poll_descriptor wake_poll{};
            wake_poll.fd =
                wake_receiver.value;
            wake_poll.events =
                POLLIN;
            descriptors.push_back(
                wake_poll);

            for (const auto& value : peers) {
                poll_descriptor descriptor{};
                descriptor.fd =
                    value.socket.value;

                descriptor.events =
                    POLLIN;

                if (!value.send_buffer.empty()) {
                    descriptor.events |=
                        POLLOUT;
                }

                descriptors.push_back(
                    descriptor);
            }

            const auto result =
                poll_sockets(
                    descriptors.data(),
#ifdef _WIN32
                    static_cast<unsigned long>(
                        descriptors.size()),
#else
                    descriptors.size(),
#endif
                    1000);

            if (result < 0) {
                break;
            }

            if (result == 0) {
                continue;
            }

            if ((descriptors[1].revents &
                 POLLIN) != 0) {

                drain_wakeup(
                    wake_receiver.value);
            }

            if ((descriptors[0].revents &
                 POLLIN) != 0) {

                accept_peers();
            }

            const auto peer_count =
                peers.size();

            for (std::size_t index = 0;
                 index < peer_count &&
                 index < peers.size();) {

                const auto descriptor_index =
                    index + 2;

                if (descriptor_index >=
                    descriptors.size()) {

                    break;
                }

                const auto events =
                    descriptors[
                        descriptor_index].revents;

                bool alive = true;

                if ((events &
                     (POLLERR |
                      POLLHUP |
                      POLLNVAL)) != 0) {

                    alive = false;
                }

                if (alive &&
                    (events & POLLIN) != 0) {

                    alive =
                        receive(
                            peers[index]);
                }

                if (alive &&
                    (events & POLLOUT) != 0) {

                    alive =
                        send_pending(
                            peers[index]);
                }

                if (alive &&
                    peers[index]
                        .connection
                        ->closing()) {

                    alive = false;
                }

                if (!alive) {
                    close_peer(
                        peers[index],
                        true);

                    peers.erase(
                        peers.begin() +
                        static_cast<
                            std::ptrdiff_t>(
                            index));

                    continue;
                }

                ++index;
            }
        }
    }

    void accept_peers() {
        while (true) {
            sockaddr_storage address{};

#ifdef _WIN32
            int length = sizeof(address);
#else
            socklen_t length = sizeof(address);
#endif

            socket_owner accepted{
                accept(
                    listener.value,
                    reinterpret_cast<sockaddr*>(
                        &address),
                    &length),
            };

            if (!accepted) {
                if (would_block(
                        socket_error())) {

                    return;
                }

                return;
            }

            if (!set_nonblocking(
                    accepted.value) ||
                gate == nullptr ||
                !gate->acquire()) {

                continue;
            }

            const auto id_value =
                next_session->fetch_add(
                    1,
                    std::memory_order_relaxed);

            const auto id =
                client_session_id{
                    id_value == 0
                        ? next_session->fetch_add(
                              1,
                              std::memory_order_relaxed)
                        : id_value,
                };

            auto connection =
                communication_connection::create(
                    id,
                    tcp_json_outbound_budget_bytes,
                    this,
                    &implementation::notify);

            peer value;
            value.socket =
                std::move(accepted);
            value.connection =
                std::move(connection);
            value.accepted_at =
                std::chrono::steady_clock::now();
            value.gate_acquired = true;

            value.receive_buffer.reserve(
                8192);

            peers.push_back(
                std::move(value));
        }
    }

    void expire_peers() {
        if (max_lifetime ==
            std::chrono::steady_clock::duration::zero()) {

            return;
        }

        const auto now =
            std::chrono::steady_clock::now();

        for (auto& value : peers) {
            if (now - value.accepted_at >=
                max_lifetime) {

                (void)value.connection
                    ->begin_close();
            }
        }
    }

    void prepare_output() {
        for (auto& value : peers) {
            if (!value.send_buffer.empty() ||
                value.connection->closing()) {

                continue;
            }

            outbound_write message;

            if (!value.connection
                    ->try_next(message)) {

                continue;
            }

            std::string json;

            if (!encode_json_outbound(
                    message,
                    json)) {

                (void)value.connection
                    ->begin_close();
                continue;
            }

            encode_tcp_frame(
                json,
                value.send_buffer);

            value.send_offset = 0;
            value.sending =
                std::move(message);
        }
    }

    [[nodiscard]] bool receive(
        peer& value) {

        std::array<std::byte, 8192> buffer{};

        while (true) {
            const auto received =
                recv(
                    value.socket.value,
                    reinterpret_cast<char*>(
                        buffer.data()),
                    static_cast<int>(
                        buffer.size()),
                    0);

            if (received > 0) {
                value.receive_buffer.insert(
                    value.receive_buffer.end(),
                    buffer.begin(),
                    buffer.begin() +
                        received);

                if (!consume_frames(value)) {
                    return false;
                }

                continue;
            }

            if (received == 0) {
                return false;
            }

            if (would_block(
                    socket_error())) {

                return true;
            }

            return false;
        }
    }

    [[nodiscard]] bool consume_frames(
        peer& value) {

        while (true) {
            const auto available =
                value.receive_buffer.size() -
                value.receive_offset;

            if (available < 4) {
                compact_receive(value);
                return true;
            }

            const auto* begin =
                value.receive_buffer.data() +
                value.receive_offset;

            const auto length =
                decode_tcp_frame_length(
                    begin);

            if (length == 0 ||
                length >
                    tcp_json_max_frame_bytes) {

                return false;
            }

            if (available <
                4u +
                    static_cast<std::size_t>(
                        length)) {

                compact_receive(value);
                return true;
            }

            const auto* payload =
                reinterpret_cast<const char*>(
                    begin + 4);

            const std::string_view text{
                payload,
                length,
            };

            if (!dispatch(
                    value,
                    text)) {

                return false;
            }

            value.receive_offset +=
                4u +
                static_cast<std::size_t>(
                    length);
        }
    }

    void compact_receive(
        peer& value) {

        if (value.receive_offset == 0) {
            return;
        }

        if (value.receive_offset ==
            value.receive_buffer.size()) {

            value.receive_buffer.clear();
            value.receive_offset = 0;
            return;
        }

        if (value.receive_offset < 8192) {
            return;
        }

        value.receive_buffer.erase(
            value.receive_buffer.begin(),
            value.receive_buffer.begin() +
                static_cast<std::ptrdiff_t>(
                    value.receive_offset));

        value.receive_offset = 0;
    }

    [[nodiscard]] bool dispatch(
        peer& value,
        std::string_view text) {

        json_request decoded;

        const auto parsed =
            decode_json_request(
                text,
                decoded);

        if (!parsed.ok() ||
            !decoded.request.valid()) {

            return false;
        }

        if (!value.connection
                ->reserve_request(
                    decoded.request)) {

            server_response duplicate;
            duplicate.status =
                server_status::
                    communication_invalid_request;

            return value.connection
                ->enqueue_response(
                    decoded.request,
                    duplicate,
                    false);
        }

        if (decoded.kind ==
            json_request_kind::login) {

            const auto& login =
                std::get<json_login_request>(
                    decoded.payload);

            login_control_message message;
            message.connection =
                value.connection.get();

            message.lifetime =
                value.connection->hold();

            message.request =
                decoded.request;

            message.name =
                login.name;

            if (!requests->push(
                    communication_control_message{
                        std::move(message)})) {

                value.connection
                    ->release_request(
                        decoded.request);

                return false;
            }

            return true;
        }

        if (!value.connection
                ->logged_in()) {

            server_response rejected;
            rejected.status =
                server_status::
                    communication_invalid_request;

            return value.connection
                ->enqueue_response(
                    decoded.request,
                    rejected);
        }

        if (decoded.kind ==
            json_request_kind::client) {

            auto client =
                std::get<json_client_request>(
                    std::move(
                        decoded.payload));

            client_control_message message;
            message.connection =
                value.connection.get();

            message.lifetime =
                value.connection->hold();

            message.request =
                decoded.request;

            message.message =
                std::move(
                    client.message);

            if (!requests->push(
                    communication_control_message{
                        std::move(message)})) {

                value.connection
                    ->release_request(
                        decoded.request);

                return false;
            }

            return true;
        }

        request_identity identity;

        if (!value.connection
                ->make_request_identity(
                    identity)) {

            server_response rejected;
            rejected.status =
                server_status::
                    communication_invalid_request;

            return value.connection
                ->enqueue_response(
                    decoded.request,
                    rejected);
        }

        server_request_message message;
        message.request =
            std::get<server_request>(
                std::move(
                    decoded.payload));

        message.identity =
            std::move(identity);

        message.origin =
            server_request_origin{
                value.connection.get(),
                &communication_connection::
                    present_response,
                decoded.request,
                value.connection->hold(),
                &communication_connection::
                    arm_response_write_wait,
                &communication_connection::
                    wait_response_written,
            };

        if (!requests->push(
                communication_control_message{
                    std::move(message)})) {

            value.connection
                ->release_request(
                    decoded.request);

            return false;
        }

        return true;
    }

    [[nodiscard]] bool send_pending(
        peer& value) {

        while (value.send_offset <
               value.send_buffer.size()) {

            const auto remaining =
                value.send_buffer.size() -
                value.send_offset;

            const auto sent =
                send(
                    value.socket.value,
                    value.send_buffer.data() +
                        value.send_offset,
                    static_cast<int>(
                        std::min<std::size_t>(
                            remaining,
                            static_cast<std::size_t>(
                                std::numeric_limits<int>::max()))),
                    0);

            if (sent > 0) {
                value.send_offset +=
                    static_cast<std::size_t>(
                        sent);
                continue;
            }

            if (sent < 0 &&
                would_block(
                    socket_error())) {

                return true;
            }

            return false;
        }

        if (value.sending) {
            value.connection->mark_written(
                *value.sending);
        }

        value.sending.reset();
        value.send_buffer.clear();
        value.send_offset = 0;

        prepare_one_output(value);
        return true;
    }

    void prepare_one_output(
        peer& value) {

        if (!value.send_buffer.empty() ||
            value.connection->closing()) {

            return;
        }

        outbound_write message;

        if (!value.connection
                ->try_next(message)) {

            return;
        }

        std::string json;

        if (!encode_json_outbound(
                message,
                json)) {

            (void)value.connection
                ->begin_close();
            return;
        }

        encode_tcp_frame(
            json,
            value.send_buffer);

        value.sending =
            std::move(message);
    }

    void close_peer(
        peer& value,
        bool post_close) noexcept {

        (void)value.connection
            ->begin_close();

        value.socket.reset();

        if (post_close &&
            !value.close_posted &&
            requests != nullptr) {

            connection_close_control_message
                close_message;

            close_message.connection =
                value.connection.get();

            close_message.lifetime =
                value.connection->hold();

            static_cast<void>(
                requests->push(
                    communication_control_message{
                        std::move(
                            close_message)}));

            value.close_posted = true;
        }

        if (value.gate_acquired &&
            gate != nullptr) {

            gate->release();
            value.gate_acquired = false;
        }
    }

    std::atomic_bool stopping = false;
    std::jthread worker;

    socket_owner listener;
    socket_owner wake_receiver;
    socket_owner wake_sender;

    request_queue* requests = nullptr;
    tcp_connection_gate* gate = nullptr;
    std::atomic_uint64_t* next_session =
        nullptr;

    std::chrono::steady_clock::duration
        max_lifetime{};

    std::vector<peer> peers;

#ifdef _WIN32
    bool winsock_started = false;
#endif
};

tcp_endpoint::tcp_endpoint()
    : impl(
          std::make_unique<
              implementation>()) {
}

tcp_endpoint::~tcp_endpoint() {
    stop();
}

bool tcp_endpoint::start(
    const communication_endpoint_configuration& configuration,
    request_queue& requests,
    tcp_connection_gate& gate,
    std::atomic_uint64_t& next_session_id,
    std::chrono::steady_clock::duration max_lifetime) {

    return impl->start(
        configuration,
        requests,
        gate,
        next_session_id,
        max_lifetime);
}

void tcp_endpoint::stop() noexcept {
    if (impl) {
        impl->stop();
    }
}

}
