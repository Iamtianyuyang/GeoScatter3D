#include "ui/AuxiliaryPanels.hpp"
#include "ui/MeasurementPanel.hpp"
#include "ui/RegionStatsPanel.hpp"
#include "ui/UiPalette.hpp"
#include "ui/UiRoot.hpp"
#include "ui/Widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace gs3d::ui {

namespace {
constexpr const char* kPerformanceWindowName = "性能###Performance";
constexpr const char* kTileInspectorWindowName = "瓦片###TileInspector";
constexpr const char* kLodViewWindowName = "细节层级###LodView";

// [标签 | 值] 行：标签用弱文字，值跟随正文色。
void stat_row(const char* label, const char* value)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value);
}

void stat_row_fmt(const char* label, const char* fmt, ...)
{
    char buf[64];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    stat_row(label, buf);
}

// "used / total" 读数下画细进度条，直观看出容量水位。
void usage_meter(const std::string& usage)
{
    float used = 0.0f;
    float total = 0.0f;
    if (std::sscanf(usage.c_str(), "%f / %f", &used, &total) == 2 &&
        total > 0.0f) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        widgets::Meter(used / total);
    }
}

bool begin_stat_table(const char* id)
{
    if (!ImGui::BeginTable(
            id,
            2,
            ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_NoPadOuterX |
                ImGuiTableFlags_NoPadInnerX
        )) {
        return false;
    }
    const float avail = ImGui::GetContentRegionAvail().x;
    const float label_w = std::clamp(avail * 0.40f, 70.0f, 140.0f);
    ImGui::TableSetupColumn(
        "label", ImGuiTableColumnFlags_WidthFixed, label_w);
    ImGui::TableSetupColumn(
        "value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    return true;
}

} // namespace

void draw_auxiliary_panels(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions
)
{
    const auto& active_render_settings =
        gs3d::app::render_settings_for_view(
            state,
            state.active_viewport_index
        );

    if (state.panels.performance) {
        ImGui::SetNextWindowSize(
            ImVec2(280.0f, 0.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(
                kPerformanceWindowName,
                &state.panels.performance
            )) {
            draw_panel_section_label("渲染");
            if (begin_stat_table("##PerfRenderTable")) {
                stat_row_fmt("帧率", "%.1f", state.performance.fps);
                stat_row_fmt(
                    "帧耗时", "%.2f ms",
                    state.performance.frame_time_ms);
                stat_row_fmt(
                    "可见点数", "%llu",
                    static_cast<unsigned long long>(
                        state.performance.visible_points));
                stat_row_fmt(
                    "GPU 显存", "%.1f MB",
                    bytes_to_mb(state.performance.gpu_memory_bytes));
                ImGui::EndTable();
            }

            ImGui::Spacing();
            draw_panel_section_label("流式加载");
            if (begin_stat_table("##PerfStreamingTable")) {
                stat_row_fmt(
                    "瓦片", "%u 已加载 / %u 等待",
                    state.performance.loaded_tiles,
                    state.performance.pending_tiles);

                stat_row(
                    "GPU 瓦片",
                    active_render_settings.cache_usage.c_str());
                ImGui::TableSetColumnIndex(1);
                usage_meter(active_render_settings.cache_usage);

                stat_row(
                    "CPU 缓存",
                    active_render_settings.cpu_cache_usage.c_str());
                ImGui::TableSetColumnIndex(1);
                usage_meter(active_render_settings.cpu_cache_usage);

                // < 0 ⇒ 尚无缓存请求（预加载快路径下不走流式）。
                if (active_render_settings.cache_hit_rate < 0.0f) {
                    stat_row("缓存命中", "—");
                } else {
                    stat_row_fmt(
                        "缓存命中", "%.1f%%",
                        active_render_settings.cache_hit_rate);
                }
                ImGui::EndTable();
            }
            if (widgets::Button(
                    "清空缓存", widgets::ButtonVariant::kDanger)) {
                actions.clear_cache_requested = true;
            }
        }
        ImGui::End();
    }

    if (state.panels.tile_inspector) {
        if (ImGui::Begin(
                kTileInspectorWindowName,
                &state.panels.tile_inspector
            )) {
            ImGui::Text(
                "已加载瓦片  %u",
                active_render_settings.loaded_tiles
            );
            ImGui::Text(
                "等待瓦片    %u",
                active_render_settings.pending_tiles
            );
            ImGui::Text(
                "GPU 驻留    %s",
                active_render_settings.cache_usage.c_str()
            );
            ImGui::Text(
                "CPU 缓存    %s",
                active_render_settings.cpu_cache_usage.c_str()
            );
        }
        ImGui::End();
    }

    if (state.panels.lod_view) {
        if (ImGui::Begin(kLodViewWindowName, &state.panels.lod_view)) {
            ImGui::Text(
                "模式        %s",
                state.performance.lod_mode.c_str()
            );
            ImGui::Text(
                "目标帧率    %.0f",
                active_render_settings.target_fps
            );
        }
        ImGui::End();
    }

    if (state.panels.measurement) {
        draw_measurement_panel(state);
    }

    if (state.panels.region_stats) {
        draw_region_stats_panel(state);
    }
}

// ── TIA-159 方向 B：分模式绘制 ─────────────────────────────────────

void draw_performance_content(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions
)
{
    const auto& rs = gs3d::app::render_settings_for_view(
        state, state.active_viewport_index);

    draw_panel_section_label("渲染");
    if (begin_stat_table("##PerfRenderTable")) {
        stat_row_fmt("帧率", "%.1f", state.performance.fps);
        stat_row_fmt("帧耗时", "%.2f ms", state.performance.frame_time_ms);
        stat_row_fmt("可见点数", "%llu",
            static_cast<unsigned long long>(state.performance.visible_points));
        stat_row_fmt("GPU 显存", "%.1f MB",
            bytes_to_mb(state.performance.gpu_memory_bytes));
        ImGui::EndTable();
    }

    ImGui::Spacing();
    draw_panel_section_label("流式加载");
    if (begin_stat_table("##PerfStreamingTable")) {
        stat_row_fmt("瓦片", "%u 已加载 / %u 等待",
            state.performance.loaded_tiles, state.performance.pending_tiles);
        stat_row("GPU 瓦片", rs.cache_usage.c_str());
        ImGui::TableSetColumnIndex(1);
        usage_meter(rs.cache_usage);
        stat_row("CPU 缓存", rs.cpu_cache_usage.c_str());
        ImGui::TableSetColumnIndex(1);
        usage_meter(rs.cpu_cache_usage);
        if (rs.cache_hit_rate < 0.0f) {
            stat_row("缓存命中", "—");
        } else {
            stat_row_fmt("缓存命中", "%.1f%%", rs.cache_hit_rate);
        }
        ImGui::EndTable();
    }
    if (widgets::Button("清空缓存", widgets::ButtonVariant::kDanger)) {
        actions.clear_cache_requested = true;
    }
}

void draw_tile_detail_collapsible(gs3d::app::AppState& state)
{
    const auto& rs = gs3d::app::render_settings_for_view(
        state, state.active_viewport_index);

    if (ImGui::CollapsingHeader("瓦片详情", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("已加载瓦片  %u", rs.loaded_tiles);
        ImGui::Text("等待瓦片    %u", rs.pending_tiles);
        ImGui::Text("GPU 驻留    %s", rs.cache_usage.c_str());
        ImGui::Text("CPU 缓存    %s", rs.cpu_cache_usage.c_str());
    }
}

void draw_lod_settings_collapsible(gs3d::app::AppState& state)
{
    const auto& rs = gs3d::app::render_settings_for_view(
        state, state.active_viewport_index);

    if (ImGui::CollapsingHeader("LOD 设置", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("模式  %s", state.performance.lod_mode.c_str());
        ImGui::Spacing();
        float target_fps = rs.target_fps;
        if (ImGui::SliderFloat("目标帧率", &target_fps, 1.0f, 144.0f, "%.0f FPS")) {
            // 展示用，实际写回需要 render settings 命令
        }
        ImGui::Text("当前  %.0f FPS", rs.target_fps);
    }
}

} // namespace gs3d::ui
