#include "ffi/ViewerBridge.hpp"

#include "control/Base64.hpp"
#include "control/JsonRpc.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <thread>
#include <vector>

namespace gs3d::ffi {
namespace {

#if defined(_WIN32)
bool ensure_winsock() {
    static bool ready = false;
    static bool ok = false;
    if (ready) return ok;
    WSADATA data{};
    ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    ready = true;
    return ok;
}
#endif

std::string json_error(const std::string& message) {
    return nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", nullptr},
        {"error", {{"code", -32000}, {"message", message}}},
    }.dump();
}

bool ends_with_ci(const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        const char a = value[value.size() - suffix.size() + i];
        const char b = suffix[i];
        if (std::tolower(static_cast<unsigned char>(a)) !=
            std::tolower(static_cast<unsigned char>(b))) {
            return false;
        }
    }
    return true;
}

}  // namespace

ViewerBridge& ViewerBridge::instance() {
    static ViewerBridge bridge;
    return bridge;
}

ViewerBridge::~ViewerBridge() {
    stop(true);
}

bool ViewerBridge::connected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

int32_t ViewerBridge::last_capture_width() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_width_;
}

int32_t ViewerBridge::last_capture_height() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_height_;
}

std::string ViewerBridge::startup_log() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return startup_log_;
}

std::string ViewerBridge::last_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

void ViewerBridge::record_startup(const std::string& text) {
    startup_log_ += text;
    if (startup_log_.size() > 8000) {
        startup_log_ = startup_log_.substr(startup_log_.size() - 8000);
    }
}

void ViewerBridge::close_socket() {
#if defined(_WIN32)
    if (socket_ >= 0) {
        closesocket(static_cast<SOCKET>(socket_));
        socket_ = -1;
    }
#else
    if (socket_ >= 0) {
        ::close(static_cast<int>(socket_));
        socket_ = -1;
    }
#endif
}

std::uint16_t ViewerBridge::reserve_port() {
#if defined(_WIN32)
    if (!ensure_winsock()) return 0;
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return 0;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        return 0;
    }
    int len = sizeof(addr);
    if (getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        closesocket(s);
        return 0;
    }
    const auto port = ntohs(addr.sin_port);
    closesocket(s);
    return port;
#else
    return 12735;
#endif
}

bool ViewerBridge::connect_with_timeout(std::uint16_t port, int timeout_ms) {
#if defined(_WIN32)
    if (!ensure_winsock()) return false;
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(s, &writefds);
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    const int rc = select(0, nullptr, &writefds, nullptr, &tv);
    if (rc <= 0) {
        closesocket(s);
        return false;
    }
    int so_error = 0;
    int len = sizeof(so_error);
    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len);
    if (so_error != 0) {
        closesocket(s);
        return false;
    }
    mode = 0;
    ioctlsocket(s, FIONBIO, &mode);
    socket_ = static_cast<std::intptr_t>(s);
    return true;
#else
    (void)port;
    (void)timeout_ms;
    return false;
#endif
}

int ViewerBridge::start(const std::string& executable_path,
                        const std::string& dataset_path) {
#if !defined(_WIN32)
    last_error_ = "ViewerBridge only supported on Windows";
    return -1;
#else
    stop(true);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_.clear();
        startup_log_.clear();
        last_width_ = 0;
        last_height_ = 0;
        recv_buffer_.clear();

        const auto exe = std::filesystem::u8path(executable_path);
        if (!std::filesystem::exists(exe)) {
            last_error_ = "executable not found: " + executable_path;
            return -2;
        }

        const auto data = std::filesystem::u8path(dataset_path);
        if (!std::filesystem::exists(data)) {
            last_error_ = "dataset not found: " + dataset_path;
            return -2;
        }

        port_ = reserve_port();
        if (port_ == 0) {
            last_error_ = "failed to reserve control plane port";
            return -5;
        }

        const bool is_bundle =
            std::filesystem::is_directory(data) ||
            ends_with_ci(dataset_path, ".gs3d.bundle");
        std::ostringstream cmd;
        cmd << '"' << exe.string() << '"';
        cmd << " --no-welcome --headless --control-plane=" << port_;
        if (!is_bundle) {
            cmd << " --no-tiles --no-lod --input \"" << data.string() << '"';
        } else {
            cmd << " --bundle \"" << data.string() << '"';
        }

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        const std::string command = cmd.str();
        std::vector<char> mutable_cmd(command.begin(), command.end());
        mutable_cmd.push_back('\0');

        const BOOL created = CreateProcessA(
            exe.string().c_str(),
            mutable_cmd.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            exe.parent_path().string().c_str(),
            &si,
            &pi);
        if (!created) {
            last_error_ = "CreateProcess failed: " + std::to_string(GetLastError());
            return -5;
        }
        CloseHandle(pi.hThread);
        process_handle_ = reinterpret_cast<std::intptr_t>(pi.hProcess);
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(10000);
    while (std::chrono::steady_clock::now() < deadline) {
        DWORD exit_code = 0;
        HANDLE handle = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handle = reinterpret_cast<HANDLE>(process_handle_);
        }
        if (handle == nullptr) return -8;
        if (GetExitCodeProcess(handle, &exit_code) && exit_code != STILL_ACTIVE) {
            std::lock_guard<std::mutex> lock(mutex_);
            last_error_ = "viewer exited early code=" + std::to_string(exit_code);
            CloseHandle(reinterpret_cast<HANDLE>(process_handle_));
            process_handle_ = 0;
            return -6;
        }
        bool linked = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            linked = connect_with_timeout(port_, 200);
        }
        if (linked) {
            // rpc() 要求 connected_；健康检查前先临时置位，失败再回滚。
            {
                std::lock_guard<std::mutex> lock(mutex_);
                connected_ = true;
            }
            // 健康检查：rpc 自行加锁，此处不可持有 mutex_。
            const auto pong = rpc(R"({"jsonrpc":"2.0","id":1,"method":"ping"})");
            if (pong.find("pong") != std::string::npos) {
                std::lock_guard<std::mutex> lock(mutex_);
                connected_ = true;
                last_error_.clear();
                return 0;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            connected_ = false;
            last_error_ = "control plane ping failed: " + pong;
            close_socket();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = "viewer control plane connect timeout";
        if (process_handle_ != 0) {
            auto handle = reinterpret_cast<HANDLE>(process_handle_);
            process_handle_ = 0;
            // R5：连接失败必须终止子进程，禁止留下孤儿 Viewer。
            DWORD code = 0;
            if (GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE) {
                TerminateProcess(handle, 1);
            }
            CloseHandle(handle);
        }
        close_socket();
        connected_ = false;
    }
    return -7;
#endif
}

void ViewerBridge::stop(bool force) {
#if defined(_WIN32)
    std::string quit_line;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (connected_ && !force) {
            quit_line = R"({"jsonrpc":"2.0","id":999999,"method":"quit"})";
        }
    }
    if (!quit_line.empty()) {
        rpc(quit_line);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    close_socket();
    connected_ = false;
    if (process_handle_ != 0) {
        auto handle = reinterpret_cast<HANDLE>(process_handle_);
        process_handle_ = 0;
        if (!force) {
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
            while (std::chrono::steady_clock::now() < deadline) {
                DWORD code = 0;
                if (GetExitCodeProcess(handle, &code) && code != STILL_ACTIVE) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        DWORD code = 0;
        if (GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE) {
            TerminateProcess(handle, 1);
        }
        CloseHandle(handle);
    }
#else
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
#endif
}

std::string ViewerBridge::rpc(const std::string& request_line) {
#if !defined(_WIN32)
    return json_error("not connected");
#else
    std::lock_guard<std::mutex> lock(mutex_);
    if (socket_ < 0 || !connected_) {
        return json_error("viewer control plane not connected");
    }
    const SOCKET s = static_cast<SOCKET>(socket_);
    std::string payload = request_line;
    if (payload.empty() || payload.back() != '\n') payload.push_back('\n');

    int sent = 0;
    while (sent < static_cast<int>(payload.size())) {
        const int n = send(s, payload.data() + sent,
                           static_cast<int>(payload.size()) - sent, 0);
        if (n <= 0) {
            connected_ = false;
            last_error_ = "send failed";
            return json_error(last_error_);
        }
        sent += n;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto pos = recv_buffer_.find('\n');
        if (pos != std::string::npos) {
            std::string line = recv_buffer_.substr(0, pos);
            recv_buffer_.erase(0, pos + 1);
            // 更新截图尺寸缓存
            try {
                const auto parsed = nlohmann::json::parse(line);
                const auto& result = parsed.contains("result") ? parsed.at("result")
                                                               : parsed;
                if (result.is_object()) {
                    if (result.contains("width") && result["width"].is_number_integer()) {
                        last_width_ = result["width"].get<int32_t>();
                    }
                    if (result.contains("height") && result["height"].is_number_integer()) {
                        last_height_ = result["height"].get<int32_t>();
                    }
                }
            } catch (...) {
            }
            return line;
        }
        char buf[8192];
        const int n = recv(s, buf, sizeof(buf), 0);
        if (n > 0) {
            recv_buffer_.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            connected_ = false;
            last_error_ = "connection closed";
            return json_error(last_error_);
        }
        const int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) {
            // 阻塞 socket：select 等待可读
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(s, &readfds);
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 200 * 1000;
            select(0, &readfds, nullptr, nullptr, &tv);
            continue;
        }
        connected_ = false;
        last_error_ = "recv failed err=" + std::to_string(err);
        return json_error(last_error_);
    }
    last_error_ = "rpc timeout";
    return json_error(last_error_);
#endif
}

std::string ViewerBridge::screenshot_json() {
    const auto line = rpc(R"({"jsonrpc":"2.0","id":1,"method":"screenshot","params":{}})");
    try {
        const auto parsed = nlohmann::json::parse(line);
        if (parsed.contains("result")) {
            const auto& result = parsed.at("result");
            if (result.is_object() && result.contains("data")) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (result.contains("width")) last_width_ = result["width"].get<int32_t>();
                if (result.contains("height")) last_height_ = result["height"].get<int32_t>();
            }
            return result.dump();
        }
        if (parsed.contains("error")) {
            return json_error(parsed.at("error").value("message", "screenshot failed"));
        }
    } catch (...) {
    }
    return line;
}

std::string ViewerBridge::execute(const std::string& component,
                                  const std::string& command,
                                  const std::string& params_json) {
    nlohmann::json params = nlohmann::json::object();
    if (!params_json.empty()) {
        try {
            params = nlohmann::json::parse(params_json);
        } catch (...) {
            params = nlohmann::json::object();
        }
    }
    const auto req = nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "execute"},
        {"params", {{"component", component}, {"command", command}, {"params", params}}},
    };
    return rpc(req.dump());
}

std::string ViewerBridge::get_state(const std::string& component) {
    const auto req = nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "get_state"},
        {"params", {{"component", component}}},
    };
    return rpc(req.dump());
}

}  // namespace gs3d::ffi
