#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gs3d::app {

struct PanelVisibilityState {
    bool dataset = true;
    bool render_settings = true;
    bool debug_log = false;
    bool tile_inspector = false;
    bool lod_view = false;
    bool performance = false;
};

struct DatasetSummaryState {
    std::string active_dataset = "test.gs3d";
    std::string path;
    std::string format = "GS3D";
    std::uint64_t point_count = 0;
    std::string file_size = "--";
    std::uint64_t loaded_points = 0;
    std::string bounding_box;
    std::vector<std::string> dataset_tree{
        "根数据集",
        "瓦片",
        "属性"
    };
    std::vector<std::string> attributes{
        "Fold（褶皱）",
        "Elevation（高程）"
    };
    std::array<char, 128> search_text{};
    int selected_tab = 0;
};

struct RenderSettingsState {
    float point_size = 1.0f;
    float opacity = 1.0f;
    int blend_mode = 0;
    int color_by_index = 0;
    std::vector<std::string> color_by_options{
        "Fold（褶皱）",
        "Elevation（高程）"
    };

    bool reverse_colormap = false;
    bool clamp_colormap = true;

    bool use_box_clipping = false;
    bool use_plane_clipping = false;

    bool eye_dome_lighting = false;
    float lighting_strength = 0.35f;
    int background_mode = 0;

    int lod_mode = 0;
    float target_fps = 60.0f;
    float max_points_per_pixel = 2.0f;
    bool high_quality = true;

    std::string cache_usage = "0 / 256";
    std::string cpu_cache_usage = "0 / 0 MB";
    std::uint32_t loaded_tiles = 0;
    std::uint32_t pending_tiles = 0;
    float cache_hit_rate = 0.0f;
};

struct DebugLogState {
    std::vector<std::string> lines{
        "[信息] 界面已初始化。",
        "[信息] 已启用停靠布局和多视图。",
        "[信息] 每个三维视图都可拖出到其他显示器。"
    };
};

struct PerformanceState {
    float fps = 0.0f;
    float frame_time_ms = 0.0f;
    std::uint64_t visible_points = 0;
    std::uint64_t total_points = 0;
    std::uint32_t loaded_tiles = 0;
    std::uint32_t pending_tiles = 0;
    std::uint64_t gpu_memory_bytes = 0;
    std::string lod_mode = "运行时细节层级";
};

struct StatusBarState {
    float fps = 0.0f;
    std::uint64_t visible_points = 0;
    std::uint32_t loaded_tiles = 0;
    std::uint32_t pending_tiles = 0;
    std::uint64_t gpu_memory_bytes = 0;
    std::string camera_position = "0, 0, 0";
    std::string crs = "本地坐标 / 未知";
    std::string ready_state = "就绪";
};

struct RenderViewState {
    int viewport_index = 0;
    bool visible = true;
    bool detached = false;
    bool camera_linked = false;
    bool render_requested = false;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    bool show_live_image = false;
    std::uint32_t image_width = 0;
    std::uint32_t image_height = 0;
    std::uint64_t points_visible = 0;
    std::uint64_t points_total = 0;
    float frame_time_ms = 0.0f;
    std::string camera_mode = "轨道";
    std::string position = "0, 0, 0";
    float fov = 45.0f;
    std::string scale = "500 米";
    std::string legend_title = "高程";

    // Ctrl+左键拖框选状态，跨帧持续到松开鼠标（见 UiRoot.cpp draw_viewport）。
    bool box_select_dragging = false;
    float box_select_start_x = 0.0f;
    float box_select_start_y = 0.0f;

    // 悬浮 tooltip：由 ViewerApp 在上一帧算好写入，这一帧 UiRoot 直接读取渲染。
    bool hover_tooltip_visible = false;
    float hover_x = 0.0f;
    float hover_y = 0.0f;
    float hover_fold = 0.0f;
    float hover_elevation = 0.0f;
    // Debug-only breadcrumbs for tracing the post-pick display chain.
    bool hover_debug_has_hit = false;
    bool hover_debug_lookup_ok = false;
    std::uint32_t hover_debug_point_id = 0;
    float hover_debug_capture_x = -1.0f;
    float hover_debug_capture_y = -1.0f;
    float hover_debug_capture_radius = 0.0f;
    // Screen-space position of the hovered point or raw pick hit
    // (viewport-local px, origin top-left).
    float hover_screen_x = -1.0f;
    float hover_screen_y = -1.0f;

    /*
     * 三维世界坐标轴（QGIS 包围盒 + 角柱），由 compute_axis_overlay() 填充
     * 几何数据，UiRoot 用 ImGui DrawList 绘制。
     */
    bool show_world_axis = false;

    struct AxisLineSegment {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
    };
    std::vector<AxisLineSegment> axis_lines;

    struct AxisTickLabel {
        float x = 0.0f;
        float y = 0.0f;
        std::string text;
    };
    std::vector<AxisTickLabel> axis_tick_labels;

    /*
     * 地图式坐标轴（固定在屏幕边缘，不随相机旋转）。
     * 开启时，点云只在 plot_rect 内显示，左侧/底部留出边距画轴。
     * 和 show_world_axis 互斥：打开地图轴时默认关闭世界轴。
     */
    bool show_map_axis = true;

    // 当前视口中可见的 X/Y 坐标范围（世界坐标，已去除 origin 偏移的内
    // 部值，ViewerApp 填入）。用于地图坐标轴的刻度计算。
    float map_axis_x_min = 0.0f;
    float map_axis_y_min = 0.0f;
    float map_axis_x_max = 0.0f;
    float map_axis_y_max = 0.0f;
    // 原点偏移量（世界坐标 = 内部坐标 + origin），ViewerApp 填入后
    // 地图轴刻度标签自动加回原值，显示为原始 CSV 坐标。
    double map_axis_origin_x = 0.0;
    double map_axis_origin_y = 0.0;

    /*
     * Orientation gizmo 三轴屏幕方向：ViewerApp 每帧从相机 view matrix
     * 算出世界坐标轴 X/Y/Z 在视口内的投射方向，存为 gizmo 中心的偏移量。
     * UiRoot 只管以 gizmo 中心为原点画这三条线段，不碰相机数学。
     */
    struct GizmoAxisEnd {
        float dx = 0.0f;  // 屏幕空间 X 偏移（右正）
        float dy = 0.0f;  // 屏幕空间 Y 偏移（下正）
    };
    GizmoAxisEnd gizmo_x_axis{};
    GizmoAxisEnd gizmo_y_axis{};
    GizmoAxisEnd gizmo_z_axis{};
    bool gizmo_axes_valid = false;
};

struct AppState {
    PanelVisibilityState panels;
    DatasetSummaryState dataset;
    RenderSettingsState render_settings;
    DebugLogState debug_log;
    PerformanceState performance;
    StatusBarState status_bar;
    std::vector<RenderViewState> render_views;
};

} // namespace gs3d::app
