#include "app/ViewerAppStateInitialization.hpp"

#include "app/UserPreferences.hpp"

#include <algorithm>
#include <string>

namespace gs3d::app {

namespace {

[[nodiscard]] std::string format_bounds_label(
    const gs3d::core::Bounds3f& bounds
) {
    return
        "[" + std::to_string(bounds.min_x) + ", " +
        std::to_string(bounds.min_y) + ", " +
        std::to_string(bounds.min_z) + "] -> [" +
        std::to_string(bounds.max_x) + ", " +
        std::to_string(bounds.max_y) + ", " +
        std::to_string(bounds.max_z) + "]";
}

} // namespace

AppState make_initial_viewer_app_state(
    const ViewerAppStateInitializationInput& input,
    std::string_view ui_layout,
    std::optional<RenderSettingsPreferences> user_render_preferences
) {
    AppState state;
    state.ui_layout_mode = ui_layout_from_string(ui_layout);
    state.dataset.active_dataset = input.dataset.display_name;
    state.dataset.path = input.dataset.path;
    state.dataset.format = input.dataset.format;
    state.dataset.point_count = input.dataset.point_count;
    state.dataset.loaded_points = input.dataset.point_count;
    state.dataset.file_size = input.dataset.file_size;
    state.dataset.bounding_box = format_bounds_label(input.dataset.bounds);
    state.dataset.dataset_tree = input.dataset.dataset_tree;
    state.dataset.attributes.clear();

    if (input.tile_reader.has_value()) {
        const auto tile_stats = input.tile_reader->stats();
        state.dataset.tile_details = {
            "状态：已启用",
            "瓦片数：" + std::to_string(tile_stats.tile_count),
            "瓦片内点数：" + std::to_string(tile_stats.total_point_count),
            "瓦片数据：" +
                std::to_string(tile_stats.total_point_bytes / 1024 / 1024) +
                " MB",
            input.tile_preload_enabled
                ? "加载方式：全量预加载"
                : "加载方式：按需流式"
        };
    } else {
        state.dataset.tile_details = {"状态：未启用"};
    }

    if (!input.lod_dataset.empty()) {
        state.dataset.lod_details.push_back(
            "状态：已启用（" +
            std::to_string(input.lod_dataset.level_count()) + " 层）"
        );
        for (std::size_t i = 0; i < input.lod_dataset.level_count(); ++i) {
            const auto& level = input.lod_dataset.level(i);
            state.dataset.lod_details.push_back(
                level.name + "：" +
                std::to_string(level.target_point_count) + " 点"
            );
        }
    } else {
        state.dataset.lod_details = {"状态：未启用"};
    }

    state.render_settings.height_by_options.clear();
    state.render_settings.color_by_options.clear();
    for (const auto& attr : input.attributes) {
        state.dataset.attributes.push_back(attr.name);
        state.render_settings.height_by_options.push_back(attr.name);
        state.render_settings.color_by_options.push_back(attr.name);
    }

    // 用户偏好覆盖默认渲染设置；随后按此播种各视图副本（TIA-90）。
    if (user_render_preferences.has_value()) {
        apply_render_settings_preferences(
            state, *user_render_preferences
        );
    }

    state.render_settings_by_view.assign(
        input.viewport_count,
        state.render_settings
    );
    state.navigation_maps.resize(input.viewport_count);
    state.measurements.resize(input.viewport_count);
    state.region_stats_by_view.resize(input.viewport_count);
    state.render_views.resize(input.viewport_count);
    const int visible_count = std::clamp(
        input.initial_visible_viewport_count,
        1,
        static_cast<int>(input.viewport_count)
    );
    for (std::size_t i = 0; i < input.viewport_count; ++i) {
        auto& view = state.render_views[i];
        view.viewport_index = static_cast<int>(i);
        view.visible = static_cast<int>(i) < visible_count;
        view.camera_linked = false;
    }

    if (input.benchmark_enabled) {
        state.panels.dataset = false;
        state.panels.render_settings = false;
        state.panels.tile_inspector = false;
        state.panels.lod_view = false;
        state.panels.performance = false;
    }
    return state;
}

} // namespace gs3d::app
