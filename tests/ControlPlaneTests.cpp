#include "control/Base64.hpp"
#include "control/ControlPlane.hpp"
#include "control/PanelComponents.hpp"
#include "app/AppState.hpp"
#include "ui/PanelRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using gs3d::app::AppState;
using gs3d::control::CapturedImage;
using gs3d::control::ComponentRegistry;
using gs3d::control::ControlDispatcher;
using gs3d::control::ControlPlane;
using gs3d::control::ControlResponse;
using gs3d::control::make_panel_components;

namespace {

ComponentRegistry make_registry(AppState& state) {
    ComponentRegistry registry;
    for (auto& component : make_panel_components(state)) {
        registry.register_component(std::move(component));
    }
    return registry;
}

nlohmann::json response_result(const nlohmann::json& response) {
    REQUIRE(response.contains("result"));
    return response["result"];
}

} // namespace

TEST_CASE("Dispatcher: ping returns pong", "[control_plane]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);
    ControlDispatcher dispatcher(registry);

    const auto response = dispatcher.handle_request(
        1,
        R"({"jsonrpc":"2.0","id":1,"method":"ping"})"
    );
    REQUIRE(response.has_value());
    CHECK(response_result(nlohmann::json::parse(response->payload)) == "pong");
}

TEST_CASE("Dispatcher: unknown method is a JSON-RPC error", "[control_plane]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);
    ControlDispatcher dispatcher(registry);

    const auto response = dispatcher.handle_request(
        1,
        R"({"jsonrpc":"2.0","id":1,"method":"frobnicate"})"
    );
    REQUIRE(response.has_value());
    const nlohmann::json parsed = nlohmann::json::parse(response->payload);
    CHECK(parsed["error"]["code"] == -32601);
}

TEST_CASE("Dispatcher: get_state on unknown component errors", "[control_plane]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);
    ControlDispatcher dispatcher(registry);

    const auto response = dispatcher.handle_request(
        1,
        R"({"jsonrpc":"2.0","id":2,"method":"get_state","params":{"component":"panel.nope"}})"
    );
    REQUIRE(response.has_value());
    const nlohmann::json parsed = nlohmann::json::parse(response->payload);
    CHECK(parsed["error"]["code"] == -32602);
    CHECK(parsed["error"]["message"].get<std::string>().find("panel.nope") != std::string::npos);
}

TEST_CASE("Dispatcher: toggle and set_value drive panel state", "[control_plane]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);
    ControlDispatcher dispatcher(registry);

    auto response = dispatcher.handle_request(
        1,
        R"({"jsonrpc":"2.0","id":3,"method":"toggle","params":{"component":"panel.performance"}})"
    );
    REQUIRE(response.has_value());
    CHECK(response_result(nlohmann::json::parse(response->payload))["visible"] == true);
    CHECK(state.panels.performance == true);

    response = dispatcher.handle_request(
        1,
        R"({"jsonrpc":"2.0","id":4,"method":"set_value","params":{"component":"panel.performance","value":false}})"
    );
    REQUIRE(response.has_value());
    CHECK(response_result(nlohmann::json::parse(response->payload))["visible"] == false);
    CHECK(state.panels.performance == false);
}

TEST_CASE("Dispatcher: execute with command params", "[control_plane]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);
    ControlDispatcher dispatcher(registry);

    const auto response = dispatcher.handle_request(
        1,
        R"({"jsonrpc":"2.0","id":5,"method":"execute","params":{"component":"panel.dataset","command":"set_visible","params":{"visible":false}}})"
    );
    REQUIRE(response.has_value());
    CHECK(state.panels.dataset == false);
    CHECK(response_result(nlohmann::json::parse(response->payload))["visible"] == false);
}

TEST_CASE("Dispatcher: list_components exposes the registry", "[control_plane]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);
    ControlDispatcher dispatcher(registry);

    const auto response = dispatcher.handle_request(
        1,
        R"({"jsonrpc":"2.0","id":6,"method":"list_components"})"
    );
    REQUIRE(response.has_value());
    const nlohmann::json components =
        response_result(nlohmann::json::parse(response->payload))["components"];
    REQUIRE(components.is_array());
    CHECK(components.size() == static_cast<std::size_t>(gs3d::ui::kPanelCount));
}

TEST_CASE("Dispatcher: notifications produce no response", "[control_plane]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);
    ControlDispatcher dispatcher(registry);

    const auto response = dispatcher.handle_request(
        1,
        R"({"jsonrpc":"2.0","method":"toggle","params":{"component":"panel.dataset"}})"
    );
    CHECK_FALSE(response.has_value());
    CHECK(state.panels.dataset == false);
}

TEST_CASE("Dispatcher: screenshot arms capture and returns PNG via poll_capture",
          "[control_plane][screenshot]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);

    bool armed = false;
    const std::vector<std::uint8_t> fake_png{0x89, 0x50, 0x4E, 0x47, 0x00, 0x01, 0x02};
    ControlDispatcher dispatcher(
        registry,
        {
            .arm_capture = [&armed]() {
                armed = true;
                return true;
            },
            .take_png = [&fake_png]() -> std::optional<CapturedImage> {
                return CapturedImage{
                    .png_bytes = fake_png,
                    .width = 640,
                    .height = 480
                };
            }
        }
    );

    // 截图请求不立即回响应。
    const auto response = dispatcher.handle_request(
        1,
        R"({"jsonrpc":"2.0","id":9,"method":"screenshot"})"
    );
    CHECK_FALSE(response.has_value());
    CHECK(armed);

    // 下一帧 poll_capture 取回 PNG → base64 结果。
    const auto responses = dispatcher.poll_capture();
    REQUIRE(responses.size() == 1);
    const nlohmann::json parsed = nlohmann::json::parse(responses[0].payload);
    REQUIRE(parsed.contains("result"));
    CHECK(parsed["result"]["width"] == 640);
    CHECK(parsed["result"]["height"] == 480);
    CHECK(parsed["result"]["format"] == "png");
    // 解码 base64 后应还原出原始 PNG 字节。
    const std::string encoded = parsed["result"]["data"].get<std::string>();
    const auto decoded = gs3d::control::base64_decode(encoded);
    CHECK(decoded == fake_png);
}

TEST_CASE("Dispatcher: screenshot capture timeout reports error", "[control_plane]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);
    ControlDispatcher dispatcher(
        registry,
        {
            .arm_capture = []() { return true; },
            .take_png = []() -> std::optional<CapturedImage> { return std::nullopt; }
        }
    );
    (void)dispatcher.handle_request(
        1,
        R"({"jsonrpc":"2.0","id":10,"method":"screenshot"})"
    );
    const auto responses = dispatcher.poll_capture();
    REQUIRE(responses.empty());
}

TEST_CASE("Dispatcher: second screenshot while in flight is rejected", "[control_plane]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);
    ControlDispatcher dispatcher(
        registry,
        {
            .arm_capture = []() { return true; },
            .take_png = []() -> std::optional<CapturedImage> { return std::nullopt; }
        }
    );
    (void)dispatcher.handle_request(1, R"({"jsonrpc":"2.0","id":1,"method":"screenshot"})");
    const auto second = dispatcher.handle_request(
        1,
        R"({"jsonrpc":"2.0","id":2,"method":"screenshot"})"
    );
    REQUIRE(second.has_value());
    const nlohmann::json parsed = nlohmann::json::parse(second->payload);
    CHECK(parsed["error"]["code"] == -32000);
}

TEST_CASE("Dispatcher: arm failure reports an immediate error", "[control_plane]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);
    ControlDispatcher dispatcher(
        registry,
        {
            .arm_capture = []() { return false; },
            .take_png = []() -> std::optional<CapturedImage> { return std::nullopt; }
        }
    );
    const auto response = dispatcher.handle_request(
        1,
        R"({"jsonrpc":"2.0","id":11,"method":"screenshot"})"
    );
    REQUIRE(response.has_value());
    CHECK(nlohmann::json::parse(response->payload)["error"]["code"] == -32000);
}

TEST_CASE("Dispatcher: quit sets the quit flag", "[control_plane]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);
    ControlDispatcher dispatcher(registry);
    CHECK_FALSE(dispatcher.quit_requested());

    const auto response = dispatcher.handle_request(
        1,
        R"({"jsonrpc":"2.0","id":12,"method":"quit"})"
    );
    REQUIRE(response.has_value());
    CHECK(response_result(nlohmann::json::parse(response->payload))["ok"] == true);
    CHECK(dispatcher.quit_requested());
}

// ── 真实 TCP 端到端（127.0.0.1 回环 + 临时端口） ─────────────────────

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using TestSocket = SOCKET;
inline constexpr TestSocket kTestInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using TestSocket = int;
inline constexpr TestSocket kTestInvalidSocket = -1;
#endif

namespace {

bool test_socket_send(TestSocket socket, const std::string& data) {
    return send(socket, data.data(), static_cast<int>(data.size()), 0) ==
        static_cast<int>(data.size());
}

std::string test_socket_receive_line(TestSocket socket, int timeout_ms) {
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(socket, FIONBIO, &mode);
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    fcntl(socket, F_SETFL, flags | O_NONBLOCK);
#endif
    std::string buffer;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        char chunk[4096];
        const int received = recv(socket, chunk, sizeof(chunk), 0);
        if (received > 0) {
            buffer.append(chunk, static_cast<std::size_t>(received));
            const std::size_t newline = buffer.find('\n');
            if (newline != std::string::npos) {
                return buffer.substr(0, newline);
            }
        } else if (received < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } else {
            break;
        }
    }
    return {};
}

TestSocket test_socket_connect(std::uint16_t port) {
    const TestSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!socket) {
        return kTestInvalidSocket;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        return kTestInvalidSocket;
    }
    return socket;
}

// 模拟渲染循环：轮询 poll() 直到响应到达或超时。
std::string poll_until_response(
    ControlPlane& plane,
    TestSocket socket,
    const int timeout_ms = 2000
) {
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);
    std::string line;
    while (line.empty() && std::chrono::steady_clock::now() < deadline) {
        plane.poll();
        line = test_socket_receive_line(socket, 100);
    }
    return line;
}

} // namespace

TEST_CASE("TCP round trip: ping over 127.0.0.1", "[control_plane][tcp]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);
    ControlPlane plane(registry);
    std::string error;
    REQUIRE(plane.start(0, error));  // 0 = 系统分配临时端口
    REQUIRE(plane.running());
    REQUIRE(plane.bound_port() != 0);

    const TestSocket socket = test_socket_connect(plane.bound_port());
    REQUIRE(socket != kTestInvalidSocket);

    REQUIRE(test_socket_send(
        socket,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\n"
    ));

    const std::string line = poll_until_response(plane, socket);
    REQUIRE_FALSE(line.empty());
    const nlohmann::json response = nlohmann::json::parse(line);
    CHECK(response["id"] == 1);
    CHECK(response["result"] == "pong");

    plane.stop();
}

TEST_CASE("TCP round trip: toggle panel and read state", "[control_plane][tcp]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);
    ControlPlane plane(registry);
    std::string error;
    REQUIRE(plane.start(0, error));

    const TestSocket socket = test_socket_connect(plane.bound_port());
    REQUIRE(socket != kTestInvalidSocket);

    REQUIRE(test_socket_send(
        socket,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"toggle\","
        "\"params\":{\"component\":\"panel.performance\"}}\n"
    ));
    const std::string line = poll_until_response(plane, socket);
    REQUIRE_FALSE(line.empty());
    const nlohmann::json response = nlohmann::json::parse(line);
    CHECK(response["result"]["visible"] == true);
    CHECK(state.panels.performance == true);

    plane.stop();
}
