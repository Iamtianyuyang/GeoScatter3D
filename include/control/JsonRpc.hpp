#pragma once

/*
 * TIA-109：JSON-RPC 2.0 消息构造/解析（纯逻辑，无 socket 依赖）。
 *
 * 传输层是 127.0.0.1 TCP + 换行分隔的 JSON 消息（一请求一行、一响应一行）。
 * 解析与构造在这里完成，可单测；不依赖 nlohmann 之外的任何东西。
 */

#include <nlohmann/json.hpp>

#include <string>

namespace gs3d::control {

// JSON-RPC 2.0 规范保留错误码。
inline constexpr int kJsonRpcParseError     = -32700;
inline constexpr int kJsonRpcInvalidRequest = -32600;
inline constexpr int kJsonRpcMethodNotFound = -32601;
inline constexpr int kJsonRpcInvalidParams  = -32602;
inline constexpr int kJsonRpcInternalError  = -32603;
inline constexpr int kJsonRpcServerError    = -32000;

struct JsonRpcRequest {
    nlohmann::json id = nullptr;   // null（通知）/ number / string
    std::string method;
    nlohmann::json params = nullptr;
    bool valid = false;
};

// 解析一行请求。非法 JSON / 缺 method / method 非字符串 → valid=false。
[[nodiscard]] JsonRpcRequest parse_json_rpc_request(const std::string& line) noexcept;

// 成功响应：{"jsonrpc":"2.0","id":<id>,"result":<result>}
[[nodiscard]] nlohmann::json make_json_rpc_response(
    const nlohmann::json& id,
    nlohmann::json result
);

// 错误响应：{"jsonrpc":"2.0","id":<id>,"error":{"code":<code>,"message":<msg>,"data":<data>}}
[[nodiscard]] nlohmann::json make_json_rpc_error(
    const nlohmann::json& id,
    int code,
    const std::string& message,
    nlohmann::json data = nullptr
);

} // namespace gs3d::control
