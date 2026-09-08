#pragma once

#include <nlohmann/json.hpp>

namespace gs3d::app { struct AppState; }
namespace gs3d::control {
// 纯数据快照，不向 Flutter 暴露任何图形对象。
[[nodiscard]] nlohmann::json interaction_snapshot(const app::AppState& state);
}
