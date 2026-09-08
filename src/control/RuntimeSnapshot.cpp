#include "control/RuntimeSnapshot.hpp"
#include "app/AppState.hpp"

namespace gs3d::control {
nlohmann::json interaction_snapshot(const app::AppState& state) {
    const int active = app::resolve_viewport_index(state, state.active_viewport_index);
    if (active < 0 || active >= static_cast<int>(state.render_views.size())) {
        return {{"id", "runtime.pick"}, {"ready", false}};
    }
    const auto& view = state.render_views[static_cast<std::size_t>(active)];
    const auto& measurement = app::measurement_for_view(state, active);
    const auto& stats = app::region_stats_for_view(state, active);
    const auto point_json = [&view](const data::Gs3dPoint& point) {
        return nlohmann::json::array({point.x + view.data_origin[0], point.y + view.data_origin[1],
                                     point.z + view.data_origin[2], point.value});
    };
    auto lines = nlohmann::json::array();
    for (std::size_t i = 0; i < measurement.lines().size(); ++i) {
        const auto& line = measurement.lines()[i];
        lines.push_back({{"id", std::to_string(i)}, {"start", point_json(line.point_a)},
                         {"end", point_json(line.point_b)}, {"fixed", line.fixed}, {"color", line.color}});
    }
    auto overlays = nlohmann::json::array();
    for (const auto& line : view.measurement_overlays) {
        if (line.visible) overlays.push_back({{"a", {line.a_screen_x, line.a_screen_y}},
            {"b", {line.b_screen_x, line.b_screen_y}}, {"color", line.color}, {"label", line.label}});
    }
    auto axes = nlohmann::json::array();
    for (const auto& line : view.axis_lines) axes.push_back({line.x0, line.y0, line.x1, line.y1});
    auto ticks = nlohmann::json::array();
    for (const auto& tick : view.axis_tick_labels) ticks.push_back({{"x", tick.x}, {"y", tick.y}, {"text", tick.text}});
    const auto& nav = app::navigation_map_for_view(state, active);
    return {
        {"id", "runtime.pick"}, {"ready", true}, {"active_viewport_index", active},
        {"width", view.image_width}, {"height", view.image_height},
        {"canvas_left", view.canvas_rect_min_x}, {"canvas_top", view.canvas_rect_min_y},
        {"canvas_right", view.canvas_rect_max_x}, {"canvas_bottom", view.canvas_rect_max_y},
        {"hover", view.hover_tooltip_visible ? nlohmann::json::array({view.hover_x, view.hover_y, view.hover_elevation, view.hover_fold}) : nlohmann::json(nullptr)},
        {"hover_screen", {view.hover_screen_x, view.hover_screen_y}},
        {"selected", view.selected_point_visible ? nlohmann::json(view.selected_point) : nlohmann::json(nullptr)},
        {"selected_screen", {view.selected_screen_x, view.selected_screen_y}},
        {"measurement_mode", measurement.measure_mode_active()},
        {"measurement_display_mode", static_cast<int>(measurement.display_mode())},
        {"measurement_lines", lines},
        {"pending_point", measurement.pending_point() ? point_json(*measurement.pending_point()) : nlohmann::json(nullptr)},
        {"pending_screen", view.pending_point_visible ? nlohmann::json::array({view.pending_point_screen_x, view.pending_point_screen_y}) : nlohmann::json(nullptr)},
        {"measurement_overlays", overlays}, {"axis_lines", axes}, {"axis_ticks", ticks},
        {"region_computing", stats.computing}, {"region_valid", stats.valid}, {"region_point_count", stats.point_count},
        {"region_error", stats.error},
        {"region_stats", {{"count", stats.point_count}, {"mean", stats.fold_avg}, {"min", stats.fold_min},
            {"max", stats.fold_max}, {"std_dev", stats.fold_std_dev}, {"histogram", stats.histogram},
            {"z_min", stats.elev_min}, {"z_max", stats.elev_max}, {"z_mean", stats.elev_avg},
            {"computing", stats.computing}}},
        {"navigation", {{"valid", nav.view_rect_valid}, {"width", nav.tex_w}, {"height", nav.tex_h},
            {"view_rect", {nav.view_rect_min_x, nav.view_rect_min_y, nav.view_rect_max_x, nav.view_rect_max_y}}}},
        {"dataset", {{"point_count", state.dataset.point_count}, {"tile_details", state.dataset.tile_details},
            {"lod_details", state.dataset.lod_details}, {"attributes", state.dataset.attributes}}}
    };
}
}
