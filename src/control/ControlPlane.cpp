#include "control/ControlPlane.hpp"

#include "control/Base64.hpp"
#include "util/Log.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
inline constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
inline bool socket_valid(const SocketHandle socket) { return socket != kInvalidSocket; }
inline void socket_close(const SocketHandle socket) { closesocket(socket); }
inline int socket_last_error() { return WSAGetLastError(); }
inline constexpr int kSocketWouldBlock = WSAEWOULDBLOCK;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;
inline bool socket_valid(const SocketHandle socket) { return socket >= 0; }
inline void socket_close(const SocketHandle socket) { close(socket); }
inline int socket_last_error() { return errno; }
inline constexpr int kSocketWouldBlock = EWOULDBLOCK;
#endif

#include <algorithm>
#include <cstring>
#include <string_view>
#include <utility>

namespace gs3d::control {

namespace {

// 单条消息上限：截图 PNG(Base64) 可达数 MB。
constexpr std::size_t kMaxLineBytes = 64ull * 1024ull * 1024ull;

// select() 一次最多监听的 socket 数（fd_set 上限）。
constexpr int kMaxSelectSockets = 60;

bool set_nonblocking(const SocketHandle socket) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool set_nodelay(const SocketHandle socket) {
    int enabled = 1;
    return setsockopt(
        socket,
        IPPROTO_TCP,
        TCP_NODELAY,
        reinterpret_cast<const char*>(&enabled),
        sizeof(enabled)
    ) == 0;
}

// 等待一组 socket 可读（timeout_ms 超时）。Windows 用 select()（WSAPoll
// 有已知缺陷：超时可能不生效、对已关闭 socket 可能永久阻塞；select 的
// timeval 超时可靠），POSIX 用 poll()。返回 false 表示出错。
bool wait_readable(
    const SocketHandle* sockets,
    const std::size_t count,
    const int timeout_ms,
    char* readable_out
) {
#ifdef _WIN32
    fd_set read_fds;
    FD_ZERO(&read_fds);
    for (std::size_t i = 0; i < count; ++i) {
        FD_SET(sockets[i], &read_fds);
    }
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    const int result = select(0, &read_fds, nullptr, nullptr, &tv);
    if (result == SOCKET_ERROR) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        readable_out[i] = FD_ISSET(sockets[i], &read_fds) != 0 ? 1 : 0;
    }
    return true;
#else
    struct pollfd fds[64];
    const std::size_t n = std::min<std::size_t>(count, 64);
    for (std::size_t i = 0; i < n; ++i) {
        fds[i].fd = sockets[i];
        fds[i].events = POLLIN;
        fds[i].revents = 0;
    }
    const int result = poll(fds, n, timeout_ms);
    if (result < 0) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        readable_out[i] =
            (fds[i].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0 ? 1 : 0;
    }
    return true;
#endif
}

// 一次请求中取字符串参数；缺失/类型错误抛 -32602。
std::string require_string_param(
    const nlohmann::json& params,
    const char* key
) {
    if (!params.is_object() || !params.contains(key) ||
        !params[key].is_string()) {
        throw ComponentError(
            kJsonRpcInvalidParams,
            std::string("missing or invalid param \"") + key + "\""
        );
    }
    return params[key].get<std::string>();
}

} // namespace

// ── ControlDispatcher ──────────────────────────────────────────────────

ControlDispatcher::ControlDispatcher(
    ComponentRegistry& registry,
    CaptureCallbacks capture
)
    : registry_(registry), capture_(std::move(capture)) {}

std::optional<ControlResponse> ControlDispatcher::handle_request(
    const std::uint64_t connection_id,
    const std::string& request_line
) {
    const JsonRpcRequest request = parse_json_rpc_request(request_line);
    if (!request.valid) {
        return ControlResponse{
            connection_id,
            make_json_rpc_error(
                nullptr,
                kJsonRpcParseError,
                "invalid JSON-RPC request"
            ).dump()
        };
    }
    const bool is_notification = request.id.is_null();

    // 截图：不立即回；武装本帧截图，由 poll_capture() 在读回完成后回送。
    if (request.method == "screenshot") {
        if (!capture_.arm_capture) {
            return ControlResponse{
                connection_id,
                make_json_rpc_error(
                    request.id,
                    kJsonRpcServerError,
                    "screenshot unavailable (no capture backend)"
                ).dump()
            };
        }
        if (capture_armed_) {
            return ControlResponse{
                connection_id,
                make_json_rpc_error(
                    request.id,
                    kJsonRpcServerError,
                    "screenshot already in progress"
                ).dump()
            };
        }
        if (!capture_.arm_capture()) {
            return ControlResponse{
                connection_id,
                make_json_rpc_error(
                    request.id,
                    kJsonRpcServerError,
                    "cannot capture now (viewport region invalid or capture busy)"
                ).dump()
            };
        }
        capture_armed_ = true;
        capture_connection_ = connection_id;
        capture_id_ = request.id;
        capture_armed_at_ = std::chrono::steady_clock::now();
        return std::nullopt;  // 响应由 poll_capture() 产出
    }

    nlohmann::json result;
    try {
        result = dispatch_method(request);
    } catch (const ComponentError& error) {
        return ControlResponse{
            connection_id,
            make_json_rpc_error(request.id, error.code(), error.what()).dump()
        };
    } catch (const std::exception& error) {
        return ControlResponse{
            connection_id,
            make_json_rpc_error(
                request.id,
                kJsonRpcInternalError,
                error.what()
            ).dump()
        };
    }
    if (is_notification) {
        return std::nullopt;
    }
    return ControlResponse{
        connection_id,
        make_json_rpc_response(request.id, std::move(result)).dump()
    };
}

std::vector<ControlResponse> ControlDispatcher::poll_capture() {
    std::vector<ControlResponse> responses;
    if (!capture_armed_) {
        return responses;
    }
    const auto now = std::chrono::steady_clock::now();
    if (capture_.take_png) {
        if (const auto image = capture_.take_png(); image.has_value()) {
            responses.push_back(ControlResponse{
                *capture_connection_,
                make_json_rpc_response(
                    capture_id_,
                    nlohmann::json{
                        {"width", image->width},
                        {"height", image->height},
                        {"format", "png"},
                        {"data", base64_encode(image->png_bytes)}
                    }
                ).dump()
            });
            capture_armed_ = false;
            capture_connection_.reset();
            return responses;
        }
    }
    if (now - capture_armed_at_ > kCaptureTimeout) {
        responses.push_back(ControlResponse{
            *capture_connection_,
            make_json_rpc_error(
                capture_id_,
                kJsonRpcServerError,
                "screenshot capture timed out"
            ).dump()
        });
        capture_armed_ = false;
        capture_connection_.reset();
    }
    return responses;
}

nlohmann::json ControlDispatcher::dispatch_method(const JsonRpcRequest& request) {
    if (request.method == "ping") {
        return "pong";
    }
    if (request.method == "quit") {
        quit_requested_ = true;
        return {{"ok", true}};
    }
    if (request.method == "list_components") {
        return {{"components", registry_.list_json()}};
    }
    if (request.method == "get_state") {
        const std::string id = require_string_param(request.params, "component");
        Component* component = registry_.find(id);
        if (component == nullptr) {
            throw ComponentError(
                kJsonRpcInvalidParams,
                "unknown component: " + id
            );
        }
        return component->get_state();
    }
    if (request.method == "execute") {
        const std::string id = require_string_param(request.params, "component");
        const std::string command = require_string_param(request.params, "command");
        const nlohmann::json params =
            request.params.contains("params")
                ? request.params["params"]
                : nlohmann::json::object();
        return registry_.execute(id, command, params);
    }
    if (request.method == "toggle" || request.method == "click") {
        const std::string id = require_string_param(request.params, "component");
        return registry_.execute(id, request.method, nlohmann::json::object());
    }
    if (request.method == "set_value") {
        const std::string id = require_string_param(request.params, "component");
        if (!request.params.contains("value")) {
            throw ComponentError(
                kJsonRpcInvalidParams,
                "set_value requires param \"value\""
            );
        }
        return registry_.execute(
            id,
            "set_value",
            nlohmann::json{{"value", request.params["value"]}}
        );
    }
    throw ComponentError(
        kJsonRpcMethodNotFound,
        "method not found: " + request.method
    );
}

// ── ControlPlane（TCP 服务） ───────────────────────────────────────────

ControlPlane::ControlPlane(
    ComponentRegistry& registry,
    CaptureCallbacks capture
)
    : registry_(registry),
      capture_(std::move(capture)),
      dispatcher_(registry_, capture_) {}

ControlPlane::~ControlPlane() {
    stop();
}

bool ControlPlane::start(const std::uint16_t port, std::string& error) {
    if (running_.load()) {
        return true;
    }

#ifdef _WIN32
    static std::once_flag winsock_once;
    static bool winsock_ok = false;
    std::call_once(winsock_once, [] {
        WSADATA data{};
        winsock_ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    });
    if (!winsock_ok) {
        error = "WSAStartup failed";
        return false;
    }
#endif

    SocketHandle listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!socket_valid(listen_sock)) {
        error = "socket creation failed";
        return false;
    }
    {
        int reuse = 1;
        (void)setsockopt(
            listen_sock,
            SOL_SOCKET,
            SO_REUSEADDR,
            reinterpret_cast<const char*>(&reuse),
            sizeof(reuse)
        );
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (bind(
            listen_sock,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)
        ) != 0) {
        error = "cannot bind 127.0.0.1:" + std::to_string(port) +
                " (port in use?)";
        socket_close(listen_sock);
        return false;
    }
    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    if (getsockname(
            listen_sock,
            reinterpret_cast<sockaddr*>(&bound),
            &bound_len
        ) == 0) {
        bound_port_ = ntohs(bound.sin_port);
    } else {
        bound_port_ = port;
    }
    if (listen(listen_sock, 8) != 0) {
        error = "listen failed";
        socket_close(listen_sock);
        return false;
    }
    // 非阻塞：accept 循环靠 EWOULDBLOCK 判断没有更多待处理连接。
    if (!set_nonblocking(listen_sock)) {
        error = "cannot set listen socket non-blocking";
        socket_close(listen_sock);
        return false;
    }

    // 唤醒 socket 对：一对互联的 UDP 回环 socket（Windows 无 AF_UNIX
    // socketpair；UDP 互联对是等价且可移植的做法）。
    SocketHandle wake_a = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    SocketHandle wake_b = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (!socket_valid(wake_a) || !socket_valid(wake_b)) {
        error = "wake socket creation failed";
        if (socket_valid(wake_a)) socket_close(wake_a);
        if (socket_valid(wake_b)) socket_close(wake_b);
        socket_close(listen_sock);
        return false;
    }
    {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (bind(wake_a, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0) {
            error = "wake socket bind failed";
            socket_close(wake_a);
            socket_close(wake_b);
            socket_close(listen_sock);
            return false;
        }
        sockaddr_in a_bound{};
        socklen_t a_len = sizeof(a_bound);
        getsockname(wake_a, reinterpret_cast<sockaddr*>(&a_bound), &a_len);
        a_bound.sin_family = AF_INET;
        a_bound.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        // b 只向 a 发送；a 只接收 b（互联）。
        if (connect(wake_b, reinterpret_cast<const sockaddr*>(&a_bound), sizeof(a_bound)) != 0) {
            error = "wake socket connect failed";
            socket_close(wake_a);
            socket_close(wake_b);
            socket_close(listen_sock);
            return false;
        }
        sockaddr_in b_bound{};
        socklen_t b_len = sizeof(b_bound);
        getsockname(wake_b, reinterpret_cast<sockaddr*>(&b_bound), &b_len);
        b_bound.sin_family = AF_INET;
        b_bound.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(wake_a, reinterpret_cast<const sockaddr*>(&b_bound), sizeof(b_bound)) != 0) {
            error = "wake socket connect failed";
            socket_close(wake_a);
            socket_close(wake_b);
            socket_close(listen_sock);
            return false;
        }
    }
    if (!set_nonblocking(wake_b)) {
        error = "cannot set wake socket non-blocking";
        socket_close(wake_a);
        socket_close(wake_b);
        socket_close(listen_sock);
        return false;
    }

    listen_sock_ = static_cast<std::intptr_t>(listen_sock);
    wake_sock_[0] = static_cast<std::intptr_t>(wake_a);
    wake_sock_[1] = static_cast<std::intptr_t>(wake_b);
    stop_requested_.store(false);
    running_.store(true);
    server_thread_ = std::thread(&ControlPlane::server_loop, this);
    return true;
}

void ControlPlane::stop() {
    if (!running_.exchange(false) && !stop_requested_.load()) {
        return;
    }
    // 服务器线程独占全部 socket：置停止标志 + 唤醒字节后 join，随后才
    // 关闭 socket —— 不存在跨线程关闭正在 select 的 socket 的路径。
    stop_requested_.store(true);
    wake_server();
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    if (socket_valid(static_cast<SocketHandle>(listen_sock_))) {
        socket_close(static_cast<SocketHandle>(listen_sock_));
        listen_sock_ = -1;
    }
    if (socket_valid(static_cast<SocketHandle>(wake_sock_[0]))) {
        socket_close(static_cast<SocketHandle>(wake_sock_[0]));
    }
    if (socket_valid(static_cast<SocketHandle>(wake_sock_[1]))) {
        socket_close(static_cast<SocketHandle>(wake_sock_[1]));
    }
    wake_sock_[0] = -1;
    wake_sock_[1] = -1;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.clear();
    }
}

void ControlPlane::wake_server() {
    const SocketHandle wake = static_cast<SocketHandle>(wake_sock_[1]);
    if (!socket_valid(wake)) {
        return;
    }
    const char byte = 'w';
    (void)send(wake, &byte, 1, 0);  // 非阻塞 UDP，忽略失败
}

ControlPlane::Connection* ControlPlane::find_connection(const std::uint64_t id) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    const auto it = connections_.find(id);
    return it == connections_.end() ? nullptr : it->second.get();
}

/*
 * 服务器线程：单线程独占全部 socket I/O。
 *   每轮 select({listen, wake, conns...}, 50ms)：
 *     - listen 可读 → 尽可能多地 accept（直到 EWOULDBLOCK）；
 *     - wake 可读 → 消费唤醒字节；
 *     - conn 可读 → recv → 按行推入 inbound 队列；recv<=0 → 关闭连接；
 *     - 每个连接：冲刷出站队列（非阻塞 send，部分发送续传）。
 *   stop() 置标志后本线程在 ≤50ms 内退出，并自行关闭全部 socket。
 */
void ControlPlane::server_loop() {
    std::vector<SocketHandle> sockets;
    std::vector<char> readable;
    std::vector<char> read_buffer(64 * 1024);

    while (!stop_requested_.load()) {
        sockets.clear();
        const SocketHandle listen = static_cast<SocketHandle>(listen_sock_);
        const SocketHandle wake = static_cast<SocketHandle>(wake_sock_[0]);
        if (socket_valid(listen)) sockets.push_back(listen);
        if (socket_valid(wake)) sockets.push_back(wake);
        std::vector<std::uint64_t> active_ids;
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            for (const auto& [id, connection] : connections_) {
                if (static_cast<std::size_t>(sockets.size()) >=
                    static_cast<std::size_t>(kMaxSelectSockets)) {
                    break;
                }
                sockets.push_back(static_cast<SocketHandle>(connection->sock));
                active_ids.push_back(id);
            }
        }
        if (sockets.size() > 64) {
            // 保险：fd_set 上限（POSIX poll 数组同限）。
            sockets.resize(64);
        }

        readable.assign(sockets.size(), 0);
        if (!wait_readable(sockets.data(), sockets.size(), 50, readable.data())) {
            if (stop_requested_.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::size_t index = 0;
        if (socket_valid(listen) && index < readable.size()) {
            if (readable[index]) {
                // 接受所有待处理连接（非阻塞，直到 EWOULDBLOCK）。
                for (;;) {
                    sockaddr_storage peer{};
                    socklen_t peer_len = sizeof(peer);
                    const SocketHandle client = accept(
                        listen,
                        reinterpret_cast<sockaddr*>(&peer),
                        &peer_len
                    );
                    if (!socket_valid(client)) {
                        break;
                    }
                    if (!set_nonblocking(client)) {
                        socket_close(client);
                        continue;
                    }
                    (void)set_nodelay(client);
                    auto connection = std::make_unique<Connection>();
                    connection->id = next_connection_id_++;
                    connection->sock = static_cast<std::intptr_t>(client);
                    const std::uint64_t new_id = connection->id;
                    {
                        std::lock_guard<std::mutex> lock(connections_mutex_);
                        connections_[new_id] = std::move(connection);
                    }
                    gs3d::util::log::info()
                        << "[CONTROL] client connected (id=" << new_id
                        << ")\n";
                }
            }
            ++index;
        }
        if (socket_valid(wake) && index < readable.size()) {
            if (readable[index]) {
                char byte = 0;
                (void)recv(wake, &byte, 1, 0);
            }
            ++index;
        }

        // 连接 socket：先收后发。
        for (const std::uint64_t id : active_ids) {
            Connection* connection = find_connection(id);
            if (connection == nullptr) {
                continue;
            }
            const std::size_t conn_index = index++;
            if (conn_index >= readable.size() || !readable[conn_index]) {
                // 本轮不可读：仍尝试冲刷出站（可能刚被唤醒）。
                flush_outbound(*connection);
                continue;
            }
            const int received = recv(
                static_cast<SocketHandle>(connection->sock),
                read_buffer.data(),
                static_cast<int>(read_buffer.size()),
                0
            );
            if (received <= 0) {
                // 对端关闭或错误。先取句柄再释放对象，避免 use-after-free。
                gs3d::util::log::info()
                    << "[CONTROL] client disconnected (id=" << id << ")\n";
                const SocketHandle dead_sock =
                    static_cast<SocketHandle>(connection->sock);
                {
                    std::lock_guard<std::mutex> lock(connections_mutex_);
                    connections_.erase(id);
                }
                socket_close(dead_sock);
                continue;
            }
            connection->line_buffer.append(
                read_buffer.data(),
                static_cast<std::size_t>(received)
            );
            bool keep_connection = true;
            std::size_t newline = 0;
            while ((newline = connection->line_buffer.find('\n')) !=
                   std::string::npos) {
                std::string line = connection->line_buffer.substr(0, newline);
                connection->line_buffer.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty()) {
                    continue;
                }
                if (line.size() > kMaxLineBytes) {
                    gs3d::util::log::error()
                        << "[CONTROL] request line too large, dropping connection\n";
                    keep_connection = false;
                    break;
                }
                {
                    std::lock_guard<std::mutex> lock(inbound_mutex_);
                    inbound_.emplace_back(id, std::move(line));
                }
            }
            if (!keep_connection) {
                const SocketHandle dead_sock =
                    static_cast<SocketHandle>(connection->sock);
                {
                    std::lock_guard<std::mutex> lock(connections_mutex_);
                    connections_.erase(id);
                }
                socket_close(dead_sock);
                continue;
            }
            flush_outbound(*connection);
        }
    }

    // 清理：关闭全部 socket（此时无其它线程触碰它们）。
    if (socket_valid(static_cast<SocketHandle>(listen_sock_))) {
        socket_close(static_cast<SocketHandle>(listen_sock_));
    }
    if (socket_valid(static_cast<SocketHandle>(wake_sock_[0]))) {
        socket_close(static_cast<SocketHandle>(wake_sock_[0]));
    }
    std::lock_guard<std::mutex> lock(connections_mutex_);
    for (auto& [id, connection] : connections_) {
        static_cast<void>(id);
        if (socket_valid(static_cast<SocketHandle>(connection->sock))) {
            socket_close(static_cast<SocketHandle>(connection->sock));
        }
    }
}

bool ControlPlane::flush_outbound(Connection& connection) {
    std::deque<std::string> pending;
    {
        std::lock_guard<std::mutex> lock(connection.out_mutex);
        if (connection.outbound.empty()) {
            return true;
        }
        pending.swap(connection.outbound);
    }
    while (!pending.empty()) {
        std::string& message = pending.front();
        std::size_t offset = 0;
        while (offset < message.size()) {
            const int sent = send(
                static_cast<SocketHandle>(connection.sock),
                message.data() + offset,
                static_cast<int>(message.size() - offset),
                0
            );
            if (sent > 0) {
                offset += static_cast<std::size_t>(sent);
                continue;
            }
            if (sent < 0 && socket_last_error() == kSocketWouldBlock) {
                message.erase(0, offset);
                std::lock_guard<std::mutex> lock(connection.out_mutex);
                // 逆序 push_front 以保持原发送顺序。
                for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
                    connection.outbound.push_front(std::move(*it));
                }
                return true;
            }
            return false;  // 致命发送错误
        }
        pending.pop_front();
    }
    return true;
}

void ControlPlane::submit_response(
    const std::uint64_t connection_id,
    std::string response_json
) {
    response_json.push_back('\n');
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        const auto it = connections_.find(connection_id);
        if (it != connections_.end()) {
            std::lock_guard<std::mutex> out_lock(it->second->out_mutex);
            it->second->outbound.push_back(std::move(response_json));
            found = true;
        }
    }
    if (found) {
        wake_server();
    }
}

void ControlPlane::poll() {
    if (!running_.load()) {
        return;
    }
    std::deque<std::pair<std::uint64_t, std::string>> pending;
    {
        std::lock_guard<std::mutex> lock(inbound_mutex_);
        pending.swap(inbound_);
    }
    for (auto& [connection_id, line] : pending) {
        const auto response = dispatcher_.handle_request(connection_id, line);
        if (response.has_value()) {
            submit_response(
                response->connection_id,
                std::move(response->payload)
            );
        }
    }
    for (auto& response : dispatcher_.poll_capture()) {
        submit_response(response.connection_id, std::move(response.payload));
    }
}

} // namespace gs3d::control
