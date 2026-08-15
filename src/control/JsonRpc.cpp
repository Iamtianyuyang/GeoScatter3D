#include "control/JsonRpc.hpp"

#include <utility>

namespace gs3d::control {

JsonRpcRequest parse_json_rpc_request(const std::string& line) noexcept {
    JsonRpcRequest request;
    try {
        const nlohmann::json message = nlohmann::json::parse(line);
        if (!message.is_object()) {
            return request;
        }
        if (!message.contains("jsonrpc") ||
            message["jsonrpc"] != "2.0") {
            return request;
        }
        if (!message.contains("method") ||
            !message["method"].is_string()) {
            return request;
        }
        request.method = message["method"].get<std::string>();
        if (message.contains("id")) {
            request.id = message["id"];
        }
        if (message.contains("params")) {
            request.params = message["params"];
        }
        request.valid = true;
    } catch (const nlohmann::json::exception&) {
        return request;
    }
    return request;
}

nlohmann::json make_json_rpc_response(
    const nlohmann::json& id,
    nlohmann::json result
) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", std::move(result)}
    };
}

nlohmann::json make_json_rpc_error(
    const nlohmann::json& id,
    const int code,
    const std::string& message,
    nlohmann::json data
) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", code},
            {"message", message},
            {"data", std::move(data)}
        }}
    };
}

} // namespace gs3d::control
