#include "app/ViewerFrameStateSynchronizer.hpp"

#include <algorithm>
#include <string>

namespace gs3d::app {

ViewerFrameStateSynchronizer::ViewerFrameStateSynchronizer(
    double dataset_origin_z,
    std::uint32_t gpu_cache_max_tiles,
    bool lod_enabled
) noexcept
    : dataset_origin_z_(dataset_origin_z),
      gpu_cache_max_tiles_(gpu_cache_max_tiles),
      lod_enabled_(lod_enabled)
{
}

void ViewerFrameStateSynchronizer::synchronize(
    AppState& state,
    const std::vector<gs3d::render::PointPushConstants>& viewport_pushes,
    const std::vector<gs3d::scene::SceneState>& viewport_scenes,
    const std::vector<float>& viewport_height_exaggerations,
    const ViewerFrameTileCacheMetrics& cache,
    const ViewerFrameStateMetrics& metrics
) const {
    state.dataset.point_count = metrics.dataset_point_count;
    state.dataset.loaded_points = metrics.gpu_resident_points;

    const auto synchronize_render_settings = [&](RenderSettingsState& settings,
                                                  std::size_t index) {
        if (index >= viewport_pushes.size() ||
            index >= viewport_scenes.size() ||
            index >= viewport_height_exaggerations.size()) {
            return;
        }
        const auto& push = viewport_pushes[index];
        const auto& scene = viewport_scenes[index];
        settings.point_size = push.point_size;
        settings.color_attr_index = scene.active_attribute_index;
        settings.height_attr_index = scene.active_height_index;
        settings.height_exaggeration = viewport_height_exaggerations[index];
        settings.colormap_index = static_cast<int>(
            (push.flags & gs3d::render::PointFlags::kColormapMask) >> 1
        );
        settings.point_shape = static_cast<int>(
            (push.flags & gs3d::render::PointFlags::kPointShapeMask) >>
                gs3d::render::PointFlags::kPointShapeShift
        );
        settings.value_clip_enabled =
            (push.flags & gs3d::render::PointFlags::kValueClip) != 0;
        float display_min = push.color_min;
        if (push.color_source ==
            static_cast<std::uint32_t>(AttrPhysicalSource::Z)) {
            display_min += static_cast<float>(dataset_origin_z_);
        }
        settings.data_value_min = display_min;
        settings.data_value_max = display_min + push.color_range;
        settings.loaded_tiles = metrics.loaded_tiles;
        settings.pending_tiles = metrics.pending_tiles;
        settings.cache_usage = std::to_string(metrics.loaded_tiles) + " / " +
            std::to_string(gpu_cache_max_tiles_);
        settings.cpu_cache_usage = std::to_string(
            cache.resident_bytes / (1024ull * 1024ull)
        ) + " / " + std::to_string(
            cache.max_bytes / (1024ull * 1024ull)
        ) + " MB";
        const auto cache_requests = cache.hits + cache.misses;
        settings.cache_hit_rate = cache_requests > 0
            ? 100.0f * static_cast<float>(cache.hits) /
                static_cast<float>(cache_requests)
            : -1.0f;
    };

    for (std::size_t i = 0; i < state.render_settings_by_view.size(); ++i) {
        synchronize_render_settings(state.render_settings_by_view[i], i);
    }
    state.render_settings = render_settings_for_view(
        state,
        state.active_viewport_index
    );
    for (auto& workspace : state.workspace_windows) {
        if (workspace.viewport_indices.empty()) {
            continue;
        }
        const bool active_in_workspace = std::find(
            workspace.viewport_indices.begin(),
            workspace.viewport_indices.end(),
            state.active_viewport_index
        ) != workspace.viewport_indices.end();
        workspace.components.render_settings = render_settings_for_view(
            state,
            active_in_workspace
                ? state.active_viewport_index
                : workspace.viewport_indices.front()
        );
    }

    state.performance.fps = metrics.fps;
    state.performance.frame_time_ms = metrics.frame_time_ms;
    state.performance.visible_points = metrics.visible_points;
    state.performance.total_points = metrics.dataset_point_count;
    state.performance.loaded_tiles = metrics.loaded_tiles;
    state.performance.pending_tiles = metrics.pending_tiles;
    state.performance.gpu_memory_bytes = metrics.gpu_buffer_bytes;
    state.performance.lod_mode = lod_enabled_ ? "已启用细节层级" : "全分辨率";

    state.status_bar.fps = metrics.fps;
    state.status_bar.visible_points = metrics.visible_points;
    state.status_bar.loaded_tiles = metrics.loaded_tiles;
    state.status_bar.pending_tiles = metrics.pending_tiles;
    state.status_bar.gpu_memory_bytes = metrics.gpu_buffer_bytes;
    state.status_bar.camera_position = metrics.camera_position;
    state.status_bar.crs = "本地坐标 / 未知";
    state.status_bar.ready_state = "就绪";
}

} // namespace gs3d::app
