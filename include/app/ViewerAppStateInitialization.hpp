#pragma once

#include "app/AppState.hpp"
#include "app/UserPreferences.hpp"
#include "core/DatasetDescriptor.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "data/Gs3dTileReader.hpp"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace gs3d::app {

// Narrow, CPU-only inputs needed to build the UI-facing state for an already
// loaded dataset. This deliberately does not expose the full viewer config.
struct ViewerAppStateInitializationInput {
    const gs3d::core::DatasetDescriptor& dataset;
    const std::vector<AttrDescriptor>& attributes;
    const gs3d::data::Gs3dLodDataset& lod_dataset;
    const std::optional<gs3d::data::Gs3dTileReader>& tile_reader;
    bool tile_preload_enabled = false;
    std::size_t viewport_count = 1;
    int initial_visible_viewport_count = 1;
    bool benchmark_enabled = false;
};

// ui_layout 取配置字符串（"workbench" / "floating-dock" /
// "analysis-rail"，见
// ui_layout_from_string），未知值回退到工作台布局。
// user_render_preferences 可选：启动时把用户偏好中的渲染设置（点大小/
// 形状/着色/色标/值域/高度缩放）应用到默认状态，各视图副本随之继承
// （TIA-90）；缺省为 nullopt 时保持内置默认，保证测试可复现。
[[nodiscard]] AppState make_initial_viewer_app_state(
    const ViewerAppStateInitializationInput& input,
    std::string_view ui_layout = "workbench",
    std::optional<RenderSettingsPreferences> user_render_preferences =
        std::nullopt
);

} // namespace gs3d::app
