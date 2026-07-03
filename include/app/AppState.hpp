#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gs3d::app {

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

struct PanelVisibilityState {
    bool dataset = true;
    bool render_settings = true;
    bool debug_log = false;
    bool tile_inspector = false;
    bool lod_view = false;
    bool performance = false;
    bool navigation_map = true;
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

    int  colormap_index = 0;   // 色标索引 (0=Geo, 1=Viridis, 2=Jet, ...)
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

/*
 * 导航图（概览图）状态。
 *
 * 缩略图是一次性预渲染的（离屏正交俯视），存为 VkDescriptorSet 供 ImGui
 * 显示。坐标映射（bbox → 像素）只写在这里一处，缩略图渲染和视野框绘制
 * 共用同一个映射参数。
 */
struct NavigationMapState {
    bool valid = false;     // 缩略图已渲染，可显示
    bool dirty = true;      // 需要重新渲染（初次加载 / 着色属性变更）

    // ImGui 显示用的纹理 descriptor
    VkDescriptorSet texture_descriptor = VK_NULL_HANDLE;

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

struct AppState {
    PanelVisibilityState panels;
    DatasetSummaryState dataset;
    RenderSettingsState render_settings;
    DebugLogState debug_log;
    PerformanceState performance;
    StatusBarState status_bar;
    NavigationMapState navigation_map;
    std::vector<RenderViewState> render_views;
};

} // namespace gs3d::app
