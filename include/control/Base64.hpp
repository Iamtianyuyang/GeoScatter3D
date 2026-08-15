#pragma once

/*
 * TIA-109：Base64 编码（截图 PNG 以 Base64 走 JSON-RPC 结果回传）。
 * 无第三方依赖，纯逻辑可单测。
 */

#include <cstdint>
#include <string>
#include <vector>

namespace gs3d::control {

[[nodiscard]] std::string base64_encode(const std::vector<std::uint8_t>& bytes);

// 解码标准 Base64（测试/脚本回读用）；非法输入返回空串。
[[nodiscard]] std::vector<std::uint8_t> base64_decode(const std::string& text);

} // namespace gs3d::control
