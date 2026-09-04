#pragma once

#include "core/TextureHandle.hpp"

#include <array>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "app/MeasurementManager.hpp"

namespace gs3d::app {

/*
 * 顶层 UI 布局模式：
 *   kWorkbench    方案 A：菜单栏 + 左右停靠面板 + 状态栏。
 *   kFloatingDock 视口全沉浸 + 底部悬浮胶囊 Dock + 弹出卡片（方案 B，
 *                 Telegram 风）。所有面板功能收进 Dock 弹出卡片。
 *   kAnalysisRail 方案 C：暗色图标轨 + 互斥抽屉 + 右侧分析卡片。
 * 运行时可通过菜单 / Dock 设置卡片双向切换。
 */
enum class UiLayoutMode : int {
    kWorkbench = 0,
    kFloatingDock = 1,
    kAnalysisRail = 2,
};

[[nodiscard]]
inline UiLayoutMode ui_layout_from_string(
    std::string_view name,
    UiLayoutMode fallback = UiLayoutMode::kWorkbench
) noexcept {
    if (name == "floating-dock") {
        return UiLayoutMode::kFloatingDock;
    }
    if (name == "workbench") {
        return UiLayoutMode::kWorkbench;
    }
    if (name == "analysis-rail" || name == "rail") {
        return UiLayoutMode::kAnalysisRail;
    }
    return fallback;
}

/*
 * 瓦片全量预加载进度（加载门禁 UI 使用）。active 期间 UI 显示全屏
 * 加载页并拦截交互——「加载好了再进程序」；预加载完成或回退按需
 * 流式后 active=false，正常界面出现。每帧由
 * ViewerFrameStateSynchronizer 从真实流式状态镜像而来。
 */
struct TilePreloadProgressState {
    bool active = false;
    // 后台读盘 + 预建显存 buffer 阶段（尚无瓦片可上传）。
    bool reading = true;
    // 读取阶段进度：后台已读完的瓦片数（含显存 buffer 预建）。
    std::uint64_t read_tiles = 0;
    std::uint64_t resident_tiles = 0;
    std::uint64_t total_tiles = 0;
    std::uint64_t resident_bytes = 0;
    std::uint64_t total_bytes = 0;
};

enum class ScreenshotNoticeKind {
    kNone,
    kSelectingPath,
    kSaving,
    kSaved,
    kCancelled,
    kError,
};

struct ScreenshotNoticeState {
    ScreenshotNoticeKind kind = ScreenshotNoticeKind::kNone;
    std::string message;
    // Selecting/saving use 0 for an indefinite notice. Terminal states use
    // a short countdown and then disappear automatically.
    float seconds_left = 0.0f;
};

/*
 * 物理来源：Gs3dPoint 中哪个 float 字段存储属性的原始值。
 * 当前 2 个槽位 (Z / Value)；Gs3dPoint 扩展后追加枚举值。
 */
enum class AttrPhysicalSource : uint32_t {
    Z     = 0,   // Gs3dPoint::z
    Value = 1,   // Gs3dPoint::value
    // --- 扩展预留 ---
    // Attr2 = 2,
    // Attr3 = 3,
};

/*
 * 属性描述：一个命名的数据通道，绑定到 Gs3dPoint 的某个物理字段。
 * 当前 2 项 (fold→Value, elevation→Z)；将来 N 个属性时只加列表条目。
 */
struct AttrDescriptor {
    std::string        name;
    AttrPhysicalSource source;
    float              min_val = 0.0f;
    float              max_val = 0.0f;

    float range() const noexcept { return max_val - min_val; }
};

/*
 * TIA-92: 业务面板默认开启；性能与调试面板默认关闭。
 * 日志面板已下线，不再出现在注册表中；tools 已移至顶栏。
 */
struct PanelVisibilityState {
    bool tools = true;
    bool dataset = true;
    bool render_settings = true;
    bool tile_inspector = false;
    bool lod_view = false;
    bool performance = false;
    bool navigation_map = true;
    bool measurement = true;
    bool region_stats = true;
};

// TIA-92: 快捷键总览 overlay 与 Ctrl+P 面板命令面板的跨帧 UI 状态。
struct UiChromeState {
    bool shortcut_overlay_open = false;
    bool panel_palette_open = false;
    bool sidebar_visible = true;  // TIA-111 方向 B：可折叠侧边栏
    std::array<char, 64> panel_palette_query{};
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
        "value",
        "z"
    };
    // 由 ViewerApp 在载入数据后写入，项目树直接展示真实数据，
    // 而不是只有没有内容的占位节点。
    std::vector<std::string> tile_details;
    std::vector<std::string> lod_details;
    std::array<char, 128> search_text{};
    int selected_tab = 0;
};

struct RenderSettingsState {
    float point_size = 1.5f;
    int   point_shape = 0;   // 0=方形, 1=圆形, 2=菱形, 3=三角形
    float opacity = 1.0f;
    int blend_mode = 0;

    /*
     * 双通道属性选择：高度通道和颜色通道各自独立选择 attr_list 中的属性。
     * 当前 attr_list 有 2 项 (Fold, Elevation)；将来扩展后自动多出选项。
     */
    int height_attr_index = 1;   // 默认：高程 (attr_list[1])
    int color_attr_index  = 0;   // 默认：褶皱 (attr_list[0])
    float height_exaggeration = 1.0f;  // 高度夸张系数 (0.1 ~ 5.0)

    // 从 attr_list 动态填充，UI 下拉直接遍历
    std::vector<std::string> height_by_options{
        "value",
        "z"
    };
    std::vector<std::string> color_by_options{
        "value",
        "z"
    };

    int  colormap_index = 8;   // 色标索引 (默认 Rainbow256)
    bool reverse_colormap = false;
    bool clamp_colormap = true;

    // 数据显示范围裁切 (原始数据值，非归一化)
    bool  value_clip_enabled = false;
    float value_clip_min = 0.0f;  // 原始数据值下限
    float value_clip_max = 1.0f;  // 原始数据值上限

    // 当前颜色属性的数据范围 (dataset value_min/max)，UI 只读显示
    float data_value_min = 0.0f;
    float data_value_max = 1.0f;

    bool use_box_clipping = false;
    bool use_plane_clipping = false;

    bool eye_dome_lighting = false;
    float lighting_strength = 0.35f;
    int background_mode = 0;

    int lod_mode = 0;
    float target_fps = 60.0f;
    float max_points_per_pixel = 2.0f;
    bool high_quality = true;
    bool appearance_section_open = true;
    bool colormap_section_open = true;

    std::string cache_usage = "0 / 256 (soft budget)";
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
    bool force_undock_next_frame = false;
    bool camera_linked = false;
    bool render_requested = false;
    TextureHandle descriptor = kNullTextureHandle;
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

    // Shift+左键拖框选统计状态（测量模式下），跨帧持续到松开鼠标。
    bool stats_select_dragging = false;
    float stats_select_start_x = 0.0f;
    float stats_select_start_y = 0.0f;

    // 悬浮 tooltip：由 ViewerApp 在上一帧算好写入，这一帧 UiRoot 直接读取渲染。
    bool hover_tooltip_visible = false;
    float hover_x = 0.0f;
    float hover_y = 0.0f;
    float hover_fold = 0.0f;
    float hover_elevation = 0.0f;
    std::string hover_primary_value_label = "value";
    std::string hover_z_label = "z";
    // Screen-space position of the hovered point (viewport-local px, origin top-left).
    float hover_screen_x = -1.0f;
    float hover_screen_y = -1.0f;

    // Persistent selected/focus point marker. A double-click updates it and
    // also makes the point the orbit pivot for this viewport.
    bool selected_point_visible = false;
    float selected_screen_x = -1.0f;
    float selected_screen_y = -1.0f;

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

    // 悬停十字准线：贯穿全图的十字线 + 轴交点坐标值。
    // 仅在地图轴开启时生效，关闭时只保留悬停 tooltip。
    bool show_crosshair = true;

    // 十字准线 / 拾取准星自定义颜色。按 sRGB 存储（颜色选择器所见即
    // 所存，同测量线），UiRoot 绘制前线性化。默认亮黄 #F1C21B。
    std::uint32_t crosshair_color = 0xFF1BC2F1;  // IM_COL32(0xF1,0xC2,0x1B,0xFF)
    std::uint32_t reticle_color   = 0xFF1BC2F1;

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
        // 世界轴与「target→相机」方向的点积，[-1, 1]：
        // 正值 = 轴正端朝向观察者。UiRoot 用它做前后排序和背面变暗，
        // 并在轴几乎指向相机（dx/dy 退化为零）时仍能画出轴端圆球。
        float depth = 0.0f;
    };
    GizmoAxisEnd gizmo_x_axis{};
    GizmoAxisEnd gizmo_y_axis{};
    GizmoAxisEnd gizmo_z_axis{};
    bool gizmo_axes_valid = false;

    // Screenshot: canvas rect in ImGui screen coordinates (absolute, not window-relative).
    // Populated by UiRoot::draw_viewport_window each frame.
    float canvas_rect_min_x = 0.0f;
    float canvas_rect_min_y = 0.0f;
    float canvas_rect_max_x = 0.0f;
    float canvas_rect_max_y = 0.0f;

    // Measurement line overlays: pre-projected by ViewerApp each frame,
    // drawn by UiRoot as ImGui overlay lines on the viewport canvas.
    struct MeasurementLineOverlay {
        float a_screen_x = 0.0f;
        float a_screen_y = 0.0f;
        float b_screen_x = 0.0f;
        float b_screen_y = 0.0f;
        bool visible = false;
        std::uint32_t color = 0;
        std::string label;
    };
    std::vector<MeasurementLineOverlay> measurement_overlays;

    // Whether the global measurement mode is active — set by ViewerApp
    // so UiRoot can draw the mode indicator without accessing AppState.
    bool measure_mode_active = false;

    // Pending measurement point (waiting for second point).
    // Projected by ViewerApp each frame; drawn by UiRoot as a
    // highlight marker + preview line to cursor.
    bool pending_point_visible = false;
    float pending_point_screen_x = -1.0f;
    float pending_point_screen_y = -1.0f;

    // "已复制" feedback overlay countdown (C-key copy-to-clipboard).
    int copy_feedback_frames = 0;
};

/*
 * 导航图（概览图）状态。
 *
 * 缩略图是一次性预渲染的（离屏正交俯视），存为 TextureHandle 供 ImGui
 * 显示。坐标映射（bbox → 像素）只写在这里一处，缩略图渲染和视野框绘制
 * 共用同一个映射参数。
 */
struct NavigationMapState {
    bool valid = false;     // 缩略图已渲染，可显示
    bool dirty = true;      // 需要重新渲染（初次加载 / 着色属性变更）

    // ImGui 显示用的纹理句柄
    TextureHandle texture_descriptor = kNullTextureHandle;

    // 缩略图纹理实际尺寸（匹配 bbox 宽高比）
    float tex_w = 256.0f;
    float tex_h = 256.0f;

    // 数据集 XY 包围盒（坐标映射的基准）
    float bbox_min_x = 0.0f;
    float bbox_min_y = 0.0f;
    float bbox_max_x = 0.0f;
    float bbox_max_y = 0.0f;

    // 当前视野框（主视图可见 XY 范围），缩略图像素坐标
    float view_rect_min_x = 0.0f;
    float view_rect_min_y = 0.0f;
    float view_rect_max_x = 0.0f;
    float view_rect_max_y = 0.0f;
    bool  view_rect_valid = false;
};

struct WorkspaceComponentState {
    DatasetSummaryState dataset;
    RenderSettingsState render_settings;
    NavigationMapState navigation_map;
    MeasurementManager measurement;
};

struct WorkspaceWindowState {
    int id = 0;
    bool visible = true;
    bool dock_layout_initialized = false;
    WorkspaceComponentState components;
    std::vector<int> viewport_indices;
};

struct RegionStatsResult {
    bool valid = false;
    bool computing = false;
    std::uint64_t point_count = 0;
    double world_x_min = 0.0;
    double world_x_max = 0.0;
    double world_y_min = 0.0;
    double world_y_max = 0.0;
    std::string primary_label;
    std::string secondary_label;
    float fold_min = 0.0f;
    float fold_max = 0.0f;
    float fold_avg = 0.0f;
    float elev_min = 0.0f;
    float elev_max = 0.0f;
    float elev_avg = 0.0f;
};

struct AppState {
    std::filesystem::path bundle_dir;
    PanelVisibilityState panels;
    DatasetSummaryState dataset;
    RenderSettingsState render_settings;
    DebugLogState debug_log;
    PerformanceState performance;
    StatusBarState status_bar;
    NavigationMapState navigation_map;
    MeasurementManager measurement;
    RegionStatsResult region_stats;
    std::vector<RenderViewState> render_views;
    std::vector<WorkspaceWindowState> workspace_windows;
    std::vector<RenderSettingsState> render_settings_by_view;
    std::vector<NavigationMapState> navigation_maps;
    std::vector<MeasurementManager> measurements;
    std::vector<RegionStatsResult> region_stats_by_view;
    int active_viewport_index = 0;
    TextureHandle logo_texture = kNullTextureHandle;
    UiLayoutMode ui_layout_mode = UiLayoutMode::kWorkbench;
    UiChromeState ui_chrome;
    TilePreloadProgressState tile_preload;
    ScreenshotNoticeState screenshot_notice;

    // TIA-111：控制面命令产生的动作队列。组件在 control_session.poll()
    // 期间写入，ViewerApp 在同帧 poll() 之后、UI 绘制之前读取并处理，
    // 处理后清空。保证「控制面命令 → 动作」在同一帧生效。
    struct ControlActions {
        bool open_requested = false;
        bool open_bundle_requested = false;
        bool show_welcome_requested = false;
        bool screenshot_requested = false;
        bool restore_default_workspace_requested = false;
        bool theme_change_requested = false;
        int theme_id = 0;  // gs3d::ui::ThemeId
        int reset_camera_index = -1;
        int camera_view_axis = -1;
        bool toggle_fullscreen = false;
        std::string open_project_path;
    } control_actions;
};

inline int resolve_viewport_index(
    const AppState& state,
    int viewport_index
) noexcept {
    const int count = static_cast<int>(state.render_views.size());
    if (count <= 0) {
        return 0;
    }
    if (viewport_index >= 0 && viewport_index < count) {
        return viewport_index;
    }
    if (state.active_viewport_index >= 0 &&
        state.active_viewport_index < count) {
        return state.active_viewport_index;
    }
    return 0;
}

inline RenderSettingsState& render_settings_for_view(
    AppState& state,
    int viewport_index
) noexcept {
    const int resolved = resolve_viewport_index(state, viewport_index);
    if (resolved >= 0 &&
        resolved < static_cast<int>(state.render_settings_by_view.size())) {
        return state.render_settings_by_view[static_cast<std::size_t>(resolved)];
    }
    return state.render_settings;
}

inline const RenderSettingsState& render_settings_for_view(
    const AppState& state,
    int viewport_index
) noexcept {
    const int resolved = resolve_viewport_index(state, viewport_index);
    if (resolved >= 0 &&
        resolved < static_cast<int>(state.render_settings_by_view.size())) {
        return state.render_settings_by_view[static_cast<std::size_t>(resolved)];
    }
    return state.render_settings;
}

inline NavigationMapState& navigation_map_for_view(
    AppState& state,
    int viewport_index
) noexcept {
    const int resolved = resolve_viewport_index(state, viewport_index);
    if (resolved >= 0 &&
        resolved < static_cast<int>(state.navigation_maps.size())) {
        return state.navigation_maps[static_cast<std::size_t>(resolved)];
    }
    return state.navigation_map;
}

inline const NavigationMapState& navigation_map_for_view(
    const AppState& state,
    int viewport_index
) noexcept {
    const int resolved = resolve_viewport_index(state, viewport_index);
    if (resolved >= 0 &&
        resolved < static_cast<int>(state.navigation_maps.size())) {
        return state.navigation_maps[static_cast<std::size_t>(resolved)];
    }
    return state.navigation_map;
}

inline MeasurementManager& measurement_for_view(
    AppState& state,
    int viewport_index
) noexcept {
    const int resolved = resolve_viewport_index(state, viewport_index);
    if (resolved >= 0 &&
        resolved < static_cast<int>(state.measurements.size())) {
        return state.measurements[static_cast<std::size_t>(resolved)];
    }
    return state.measurement;
}

inline const MeasurementManager& measurement_for_view(
    const AppState& state,
    int viewport_index
) noexcept {
    const int resolved = resolve_viewport_index(state, viewport_index);
    if (resolved >= 0 &&
        resolved < static_cast<int>(state.measurements.size())) {
        return state.measurements[static_cast<std::size_t>(resolved)];
    }
    return state.measurement;
}

inline RegionStatsResult& region_stats_for_view(
    AppState& state,
    int viewport_index
) noexcept {
    const int resolved = resolve_viewport_index(state, viewport_index);
    if (resolved >= 0 &&
        resolved < static_cast<int>(state.region_stats_by_view.size())) {
        return state.region_stats_by_view[static_cast<std::size_t>(resolved)];
    }
    return state.region_stats;
}

inline const RegionStatsResult& region_stats_for_view(
    const AppState& state,
    int viewport_index
) noexcept {
    const int resolved = resolve_viewport_index(state, viewport_index);
    if (resolved >= 0 &&
        resolved < static_cast<int>(state.region_stats_by_view.size())) {
        return state.region_stats_by_view[static_cast<std::size_t>(resolved)];
    }
    return state.region_stats;
}

} // namespace gs3d::app
