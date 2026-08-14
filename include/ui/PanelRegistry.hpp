#pragma once

#include "app/AppState.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <string_view>

namespace gs3d::ui {

/*
 * 面板注册表（TIA-92 重设计 · 方向 A）。
 *
 * 所有可开关面板的唯一事实来源：菜单「窗口」、Ctrl+P 命令面板、
 * 默认布局构建都从这里取面板清单，不再各自维护一份 checkbox 列表。
 *
 * 本头文件不依赖 ImGui，纯逻辑可直接单测（UiPanelRegistryTests）。
 */

enum class PanelId : int {
    kDataset = 0,
    kRenderSettings = 1,
    kPerformance = 2,
    kNavigationMap = 3,
    kMeasurement = 4,
    kRegionStats = 5,
    kTileInspector = 6,
    kLodView = 7,
    kCount,
};

inline constexpr int kPanelCount = static_cast<int>(PanelId::kCount);

struct PanelDescriptor {
    PanelId id;
    const char* name;
    const char* description;
    bool debug;
};

inline constexpr std::array<PanelDescriptor, kPanelCount> kPanelRegistry{{
    {PanelId::kDataset,        "项目",     "数据集信息与属性列表",     false},
    {PanelId::kRenderSettings, "属性",     "点云外观与渲染设置",       false},
    {PanelId::kPerformance,    "性能",     "帧率与流式加载诊断",       false},
    {PanelId::kNavigationMap,  "导航图",   "俯视缩略图与视野框",       false},
    {PanelId::kMeasurement,    "测量",     "距离测量工具与列表",       false},
    {PanelId::kRegionStats,    "区域统计", "框选区域统计结果",         false},
    {PanelId::kTileInspector,  "瓦片",     "瓦片加载状态（调试）",     true},
    {PanelId::kLodView,        "细节层级", "LOD 模式与目标帧率（调试）", true},
}};

[[nodiscard]]
inline std::string_view panel_name(const PanelId id) noexcept
{
    const int index = static_cast<int>(id);
    if (index < 0 || index >= kPanelCount) return {};
    return kPanelRegistry[static_cast<std::size_t>(index)].name;
}

[[nodiscard]]
inline bool is_debug_panel(const PanelId id) noexcept
{
    const int index = static_cast<int>(id);
    if (index < 0 || index >= kPanelCount) return false;
    return kPanelRegistry[static_cast<std::size_t>(index)].debug;
}

[[nodiscard]]
inline bool* panel_visibility(
    gs3d::app::PanelVisibilityState& panels, PanelId id) noexcept
{
    switch (id) {
    case PanelId::kDataset:        return &panels.dataset;
    case PanelId::kRenderSettings: return &panels.render_settings;
    case PanelId::kPerformance:    return &panels.performance;
    case PanelId::kNavigationMap:  return &panels.navigation_map;
    case PanelId::kMeasurement:    return &panels.measurement;
    case PanelId::kRegionStats:    return &panels.region_stats;
    case PanelId::kTileInspector:  return &panels.tile_inspector;
    case PanelId::kLodView:        return &panels.lod_view;
    case PanelId::kCount: break;
    }
    return nullptr;
}

inline void toggle_panel(gs3d::app::PanelVisibilityState& panels,
                         PanelId id) noexcept
{
    if (bool* v = panel_visibility(panels, id)) *v = !*v;
}

[[nodiscard]]
inline bool panel_matches_query(PanelId id, std::string_view query) noexcept
{
    const int index = static_cast<int>(id);
    if (index < 0 || index >= kPanelCount) return false;
    const auto& d = kPanelRegistry[static_cast<std::size_t>(index)];
    if (query.empty()) return true;
    auto ci = [query](const char* t) {
        std::size_t ti = 0, qi = 0;
        while (t[ti] != '\0') {
            if (std::tolower((unsigned char)t[ti]) ==
                std::tolower((unsigned char)query[qi])) {
                if (++qi == query.size()) return true;
            } else qi = 0;
            ++ti;
        }
        return false;
    };
    return ci(d.name) || ci(d.description);
}

} // namespace gs3d::ui
