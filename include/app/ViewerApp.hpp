#pragma once

#include "app/AppState.hpp"
#include "app/ViewerAppRunState.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace gs3d::app { struct ViewerAppTileStreamState; }
namespace gs3d::app { struct ViewerAppTileStreamFrameContext; }
namespace gs3d::data { class Gs3dDataset; }
namespace gs3d::render { class OffscreenFramebuffer; }
namespace gs3d::render { class ViewportManager; }
namespace gs3d::render { class VulkanContext; }
namespace gs3d::render { class VulkanSwapchain; }
namespace gs3d::app { struct UiActions; }
namespace gs3d::app { struct RenderSettingsCommand; }
namespace gs3d::app { class ViewportResizeScheduler; }
namespace gs3d::platform { class Window; }

namespace gs3d::app {

/*
 * 交互(缩放/平移/旋转)期间前台的显示策略。
 *
 *   KeepStableHighQuality (默认):交互时继续用当前相机渲染交互开始前已经
 *     稳定显示的高质量数据(常驻的高细节 LOD + 已驻留的全分辨率 tile),
 *     绝不在交互途中切到更粗的 LOD,也不卸载已就绪的 tile。相机实时跟随,
 *     只是点云数据暂时不降质。新数据只在交互结束后、且质量不低于当前显示
 *     时才替换(本架构下质量单调不降,天然满足)。
 *
 *   AllowCoarseLOD:旧行为——交互时降到最低/自适应 LOD 并关闭 tile 叠加,
 *     换取最低端硬件上的交互帧率。会出现稀疏/层状的粗 LOD 显示。
 *
 *   FreezeLastFrameTexture:极端兜底(冻结上一帧贴图)。当前未单独实现贴图
 *     冻结管线,按 KeepStableHighQuality 处理——继续渲染已有高质量 buffer
 *     比贴图冻结更好(缩放/平移/旋转仍实时)。
 */
enum class InteractiveDisplayMode {
    KeepStableHighQuality,
    AllowCoarseLOD,
    FreezeLastFrameTexture
};

enum class ViewerOpenRequestKind {
    Project,
    RawData,
    Welcome
};

struct ViewerOpenRequest {
    ViewerOpenRequestKind kind = ViewerOpenRequestKind::Project;
    std::filesystem::path path;
};

struct ViewerAppConfig {
    std::filesystem::path gs3d_path =
        "/home/tianyy/project/GeoScatter3D/data/test.gs3d";

    std::filesystem::path vertex_shader_path =
        "/home/tianyy/project/GeoScatter3D/assets/shaders/point.vert.spv";

    std::filesystem::path fragment_shader_path =
        "/home/tianyy/project/GeoScatter3D/assets/shaders/point.frag.spv";

    unsigned int window_width = 1280;
    unsigned int window_height = 720;

    std::string window_title =
        "GeoScatter3D 三维散点查看器";

    bool window_resizable = true;

    /*
     * ImGui docking 布局持久化文件路径；空路径 = 不持久化，每次启动都用
     * 默认布局（之前的行为，io.IniFilename 被硬编码为 nullptr）。
     */
    std::filesystem::path ui_layout_ini_path = "config/imgui_layout.ini";

    /*
     * User comfort multiplier applied on top of the PPI-derived ui_scale:
     *   final_ui_scale = clamp(ppi_scale * ui_scale_multiplier, 1.0, 2.5)
     * PPI alone gives an objectively-correct baseline; this multiplier lets
     * the user nudge the overall UI size without touching code. Default 1.15
     * gives a modest comfort bump.
     */
    float ui_scale_multiplier = 1.15f;

    /*
     * 启动 UI 主题（见 ui/Theme.hpp）：
     *   "carbon-blue"（默认）| "deep-graphite" | "instrument-amber"
     * 未知值回落到 carbon-blue。运行期可经 视图→主题 菜单切换（不回写）。
     */
    std::string theme = "carbon-blue";

    // Enable Dear ImGui multi-viewports: docked windows may be detached into
    // native OS windows. The backend may disable this at runtime on platforms
    // without viewport support, such as Wayland.
    bool enable_multi_viewports = true;

    bool enable_validation_layers = true;

    // "auto" or "uuid:<32-char-hex>".
    // When auto, the best suitable GPU is chosen automatically.
    // The UUID refers to VkPhysicalDeviceIDProperties::deviceUUID.
    std::string preferred_gpu = "auto";

    std::array<float, 4> clear_color{
        0.118f,
        0.133f,
        0.165f,
        1.0f
    };

    float initial_point_size = 1.5f;

    // Source-field labels for the physical Value and Z channels.
    std::string primary_value_field_name = "fold";
    std::string z_field_name = "elevation";

    std::string camera_mode = "fit";

    std::array<float, 3> camera_position{
        0.0f,
        -14000.0f,
        6000.0f
    };

    std::array<float, 3> camera_target{
        0.0f,
        0.0f,
        0.0f
    };

    std::array<float, 3> camera_up{
        0.0f,
        0.0f,
        1.0f
    };

    float camera_fov_y = 45.0f;
    float camera_near = 1.0f;
    float camera_far = 100000.0f;

    float controller_rotate_speed = 1.0f;
    float controller_pan_speed = 1.0f;
    float controller_zoom_speed = 1.0f;

    bool controller_invert_rotate_x = false;
    bool controller_invert_rotate_y = false;
    bool controller_invert_pan_x = false;
    bool controller_invert_pan_y = true;

    bool lod_enabled = false;
    bool lod_keep_full_buffer = false;

    std::filesystem::path lod_sidecar_path =
    "data/test.gs3dlod";

    bool lod_auto_load_sidecar = true;
    bool lod_auto_save_sidecar = true;

    /*
     * Potree 式自动分层参数（替代旧的 target_point_counts / target_point_ratios）。
     *
     *   finest_target_points: 最精细层目标点数，反推锚定 voxel_size_0。
     *   growth_factor: voxel_size 倍增系数。XY 数据默认 √2 (1.414)。
     *   min_points_per_level: 最粗层点数下限，低于此值停止分层。
     *
     * 对 33M XY 数据，默认值预期自动分 ~5 层，最精 ~2M，最粗 ~125K。
     */
    std::uint64_t lod_finest_target_points = 2'000'000ull;
    float lod_growth_factor = 1.414f;
    std::uint64_t lod_min_points_per_level = 100'000ull;

    std::string lod_voxel_mode = "XY";
    float lod_voxel_scale = 1.0f;

    double lod_medium_delay_seconds = 0.20;
    double lod_high_delay_seconds = 0.80;

    bool lod_use_lowest_while_interacting = true;

    /*
     * 在 use_lowest_while_interacting 之上做帧时间自适应：交互时不固定用
     * 最低档，而是按最近测得的帧时间在档位间爬升/回退（AIMD 风格——超预算
     * 立刻退一档，达标若干帧才升一档），核显/独显都能自动找到合适档位。
     */
    bool lod_adaptive_interacting_level = false;
    double lod_frame_time_budget_ms = 14.0;

    /*
     * 交互期间前台显示策略。默认保持稳定高质量(不在缩放/平移/旋转途中
     * 切到稀疏粗 LOD)。见 InteractiveDisplayMode。
     */
    InteractiveDisplayMode interactive_display_mode =
        InteractiveDisplayMode::KeepStableHighQuality;

    bool lod_verbose = true;

        bool tile_enabled = false;

    std::filesystem::path tile_index_path =
        "data/test.gs3dtiles.index";

    std::filesystem::path tile_data_path =
        "data/test.gs3dtiles";

    /*
     * 触发全精度 tile 加载的屏幕空间阈值（像素）。
     * 当 tile 投影宽度 ≥ 该值时，加载该 tile 的全量原始点。
     * 对应 Potree 的 projected_area > threshold 判断。
     */
    float tile_min_pixel_size = 50.0f;
    std::uint32_t tile_max_visible_tiles = 0;

    bool tile_use_full_z_range = true;
    bool tile_verbose = true;
    std::uint32_t tile_gpu_cache_max_tiles = 256;
    std::uint64_t tile_gpu_upload_budget_bytes =
        8ull * 1024ull * 1024ull;
    std::uint64_t tile_cpu_cache_max_bytes =
        512ull * 1024ull * 1024ull;

    /*
     * 全量预加载:启动后一次性把所有瓦片读入并常驻 GPU(数据集能装进显存
     * 时),之后缩放/平移/旋转零加载延迟——不再按需流式、不再受逐帧上传
     * 节流限制。瓦片选择(决定画哪些可见子集)改为每帧运行(纯 CPU,无 I/O),
     * 交互期间也实时跟随相机。
     *
     * 仅当所有瓦片字节数 <= tile_preload_max_bytes 时启用,否则自动回退到
     * 按需流式(数据集大于显存预算时)。默认 1.5 GiB,覆盖竞赛级数据集且远
     * 小于常见独显显存。
     */
    bool tile_preload_all = true;
    std::uint64_t tile_preload_max_bytes =
        1536ull * 1024ull * 1024ull;
    // 预加载阶段每帧上传预算(比交互流式的 8 MiB 大得多,几帧内传完全量)。
    std::uint64_t tile_preload_upload_budget_bytes =
        64ull * 1024ull * 1024ull;

    // Number of 3D views visible at startup. More can be opened up to 4.
    int viewport_count = 1;

    /*
     * Benchmark mode (GeoScatter3DBenchmark): drives the camera with a
     * synthetic orbit instead of real input, exits after a fixed frame
     * count instead of waiting for the window to close, and prints
     * frame-time / reload-latency percentiles to stdout on exit.
     */
    bool benchmark_mode = false;
    std::uint32_t benchmark_frame_count = 600;
    std::string benchmark_present_mode = "auto";
    std::filesystem::path benchmark_pick_script_path{};
    std::filesystem::path benchmark_pick_result_path{};

    // Bundle directory for per-project analysis data (e.g. analysis.toml).
    // Empty when not in bundle mode (raw .gs3d / .csv / .dat input).
    std::filesystem::path bundle_dir;

    // Diagnostic-only dump of the current frame's visible color image
    // and the matching R32_UINT pick-id attachment. Used to check
    // whether a point that is visibly rendered also writes a non-zero
    // pixel into the pick image.
    bool pick_debug_dump_enabled = false;
    std::filesystem::path pick_debug_dump_dir = "bench/.pick_debug";
    bool pick_debug_dump_once_on_hover = true;

};

class ViewerApp {
public:
    ViewerApp() = default;

    explicit ViewerApp(ViewerAppConfig config);

    [[nodiscard]]
    int run();

    [[nodiscard]]
    const std::optional<ViewerOpenRequest>& open_request() const noexcept {
        return open_request_;
    }

private:
    void prepare_gpu_pick_requests(
        ViewerAppPickState& pick,
        const gs3d::render::ViewportManager& viewport_manager,
        gs3d::app::AppState& app_state,
        const gs3d::app::UiActions& gui_cmds,
        const std::vector<float>& viewport_point_sizes,
        std::size_t& benchmark_pick_issue_index,
        const std::vector<BenchmarkPickScriptQuery>& benchmark_pick_queries
    );

    void consume_ready_pick_frame_slot(
        std::uint32_t frame_slot,
        ViewerAppPickState& pick,
        PickDebugFrameDumper& pick_debug_frame_dumper,
        GpuPickReadback& gpu_pick_readback,
        const ViewerAppPickLookupContext& pick_lookup,
        ViewerAppPickCameraContext& pick_camera,
        ViewerAppBenchmarkPickContext& pick_benchmark,
        const VisibleTilePickResolver& resolve_hover_point_from_visible_tiles
    );

    void fill_render_views(
        gs3d::app::AppState& app_state,
        const ViewerAppRenderViewContext& ctx,
        const ViewerAppPickState& pick,
        const std::vector<std::optional<gs3d::camera::Vec3>>& selected_focus_points
    );

    void update_navigation_map_view_rect(
        NavigationMapState& nav,
        const std::vector<RenderViewState>& render_views,
        int streaming_viewport_index
    );

    void build_visible_viewports(
        std::vector<int>& visible_viewports,
        const std::vector<RenderViewState>& render_views
    );

    void apply_render_setting_commands(
        const RenderSettingsCommand& command,
        ViewerAppRenderSettingsContext& ctx
    );

    void apply_reset_camera_command(
        const UiActions& gui_cmds,
        ViewerAppCameraCommandContext& ctx
    );

    void sync_camera_link_groups(
        const AppState& app_state,
        gs3d::camera::CameraHub& camera_hub
    );

    void apply_project_open_commands(
        const UiActions& gui_cmds,
        gs3d::platform::Window& window
    );

    void apply_screenshot_command(
        const UiActions& gui_cmds,
        ViewerAppScreenshotContext& ctx
    );

    void observe_viewport_resize_requests(
        const UiActions& gui_cmds,
        ViewportResizeScheduler& scheduler,
        double now_seconds
    );

    void handle_region_stats_commands(
        const UiActions& gui_cmds,
        AppState& app_state,
        const RegionStatsCommandContext& ctx
    );

    void print_benchmark_report(
        const ViewerAppBenchmarkFrameSamples& samples,
        VkPresentModeKHR present_mode
    ) const;

    void record_screenshot_copy(
        VkCommandBuffer cmd,
        std::uint32_t image_index,
        gs3d::render::VulkanContext& context,
        const gs3d::render::VulkanSwapchain& swapchain,
        ViewerAppScreenshotCaptureState& capture
    );

    void write_pending_screenshot(
        gs3d::render::VulkanContext& context,
        const gs3d::render::VulkanSwapchain& swapchain,
        ViewerAppScreenshotCaptureState& capture
    );

    void init_navigation_map(
        gs3d::render::VulkanContext& context,
        VkCommandPool command_pool,
        VkFormat color_format,
        const gs3d::data::Gs3dDataset& dataset,
        gs3d::render::OffscreenFramebuffer& nav_fb,
        NavigationMapState& nav,
        const ViewerAppNavThumbnailContext& ctx
    );

    void record_navigation_thumbnail(
        VkCommandBuffer cmd,
        gs3d::render::OffscreenFramebuffer& nav_fb,
        NavigationMapState& nav,
        const ViewerAppNavThumbnailContext& ctx
    );

    void clear_tile_cpu_cache(ViewerAppTileStreamState& tiles);

    void update_tile_streaming(
        ViewerAppTileStreamState& tiles,
        const ViewerAppTileStreamFrameContext& ctx
    );

    void record_viewport_passes(
        VkCommandBuffer cmd,
        const ViewerAppViewportDrawContext& ctx
    );

    ViewerAppConfig config_;
    std::optional<ViewerOpenRequest> open_request_;
    std::future<RegionStatsResult> region_stats_future_;
    std::atomic<std::uint64_t>     region_stats_gen_{0};
    int                            region_stats_view_index_ = 0;
};

} // namespace gs3d::app
