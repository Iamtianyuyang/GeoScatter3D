#include "ui/AuxiliaryPanels.hpp"
#include "ui/MeasurementPanel.hpp"
#include "ui/RegionStatsPanel.hpp"
#include "ui/UiRoot.hpp"

#include "imgui.h"

namespace gs3d::ui {

namespace {
constexpr const char* kPerformanceWindowName = "性能###Performance";
constexpr const char* kDebugLogWindowName = "日志###DebugLog";
constexpr const char* kTileInspectorWindowName = "瓦片###TileInspector";
constexpr const char* kLodViewWindowName = "细节层级###LodView";
} // namespace

void draw_auxiliary_panels(gs3d::app::AppState& state)
{
    if (state.panels.performance) {
        if (ImGui::Begin(
                kPerformanceWindowName,
                &state.panels.performance
            )) {
            ImGui::Text("帧率        %.1f", state.performance.fps);
            ImGui::Text(
                "帧耗时      %.2f ms",
                state.performance.frame_time_ms
            );
            ImGui::Text(
                "可见点数    %llu",
                static_cast<unsigned long long>(
                    state.performance.visible_points
                )
            );
            ImGui::Text(
                "GPU 显存    %.1f MB",
                bytes_to_mb(state.performance.gpu_memory_bytes)
            );
            ImGui::Text(
                "瓦片        %u 已加载 / %u 等待",
                state.performance.loaded_tiles,
                state.performance.pending_tiles
            );
        }
        ImGui::End();
    }

    if (state.panels.debug_log) {
        if (ImGui::Begin(kDebugLogWindowName, &state.panels.debug_log)) {
            for (const auto& line : state.debug_log.lines) {
                ImGui::TextUnformatted(line.c_str());
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
                state.render_settings.loaded_tiles
            );
            ImGui::Text(
                "等待瓦片    %u",
                state.render_settings.pending_tiles
            );
            ImGui::Text(
                "GPU 驻留    %s",
                state.render_settings.cache_usage.c_str()
            );
            ImGui::Text(
                "CPU 缓存    %s",
                state.render_settings.cpu_cache_usage.c_str()
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
                state.render_settings.target_fps
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

} // namespace gs3d::ui
