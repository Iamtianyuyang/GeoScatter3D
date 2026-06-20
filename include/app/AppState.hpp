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
        "数值（振幅）",
        "Z（高程）"
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
        "数值（振幅）",
        "Z（高程）"
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
