#include "control/JsonRpc.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using gs3d::control::JsonRpcRequest;
using gs3d::control::kJsonRpcInvalidParams;
using gs3d::control::kJsonRpcMethodNotFound;
using gs3d::control::kJsonRpcParseError;
using gs3d::control::make_json_rpc_error;
using gs3d::control::make_json_rpc_response;
using gs3d::control::parse_json_rpc_request;

TEST_CASE("Parse a valid JSON-RPC request") {
    const JsonRpcRequest request = parse_json_rpc_request(
        R"({"jsonrpc":"2.0","id":7,"method":"toggle","params":{"component":"panel.dataset"}})"
    );
    REQUIRE(request.valid);
    CHECK(request.method == "toggle");
    CHECK(request.id == 7);
    REQUIRE(request.params.is_object());
    CHECK(request.params["component"] == "panel.dataset");
}

TEST_CASE("Parse a notification without id") {
    const JsonRpcRequest request = parse_json_rpc_request(
        R"({"jsonrpc":"2.0","method":"ping"})"
    );
    REQUIRE(request.valid);
    CHECK(request.method == "ping");
    CHECK(request.id.is_null());
}

TEST_CASE("Parse a string id") {
    const JsonRpcRequest request = parse_json_rpc_request(
        R"({"jsonrpc":"2.0","id":"cmd-1","method":"get_state","params":{"component":"viewport.main"}})"
    );
    REQUIRE(request.valid);
    CHECK(request.id == "cmd-1");
}

TEST_CASE("Reject malformed requests") {
    CHECK_FALSE(parse_json_rpc_request("not json at all").valid);
    CHECK_FALSE(parse_json_rpc_request(R"({"jsonrpc":"1.0","id":1,"method":"ping"})").valid);
    CHECK_FALSE(parse_json_rpc_request(R"({"jsonrpc":"2.0","id":1})").valid);
    CHECK_FALSE(parse_json_rpc_request(R"([1,2,3])").valid);
    CHECK_FALSE(parse_json_rpc_request("").valid);
    CHECK_FALSE(parse_json_rpc_request(R"({"jsonrpc":"2.0","id":1,"method":42})").valid);
}

TEST_CASE("Response echoes the request id") {
    const nlohmann::json response = make_json_rpc_response(
        nlohmann::json(42),
        {{"ok", true}}
    );
    CHECK(response["jsonrpc"] == "2.0");
    CHECK(response["id"] == 42);
    CHECK(response["result"]["ok"] == true);
    CHECK_FALSE(response.contains("error"));
}

TEST_CASE("Error response carries code and message") {
    const nlohmann::json response = make_json_rpc_error(
        nlohmann::json(3),
        kJsonRpcMethodNotFound,
        "method not found: nope"
    );
    CHECK(response["jsonrpc"] == "2.0");
    CHECK(response["id"] == 3);
    CHECK(response["error"]["code"] == kJsonRpcMethodNotFound);
    CHECK(response["error"]["message"] == "method not found: nope");
    CHECK_FALSE(response.contains("result"));
}

TEST_CASE("Parse error uses null id") {
    const nlohmann::json response = make_json_rpc_error(
        nullptr,
        kJsonRpcParseError,
        "invalid JSON-RPC request"
    );
    CHECK(response["error"]["code"] == kJsonRpcParseError);
    CHECK(response["id"].is_null());
}
