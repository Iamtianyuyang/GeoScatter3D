#pragma once

#include "app/AppState.hpp"
#include "app/BenchmarkSession.hpp"
#include "app/ViewerAppControlPlane.hpp"
#include "app/ViewerAppRunState.hpp"
#include "data/Gs3dExporter.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace gs3d::app { class ViewerBenchmarkController; }
namespace gs3d::render { class ViewportManager; }
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

struct ViewerInputConfig {
    // The configured input path is resolved by AppConfigLoader. Leaving this
    // empty keeps a default-constructed config portable instead of smuggling
    // one developer machine's filesystem into every executable.
    std::filesystem::path gs3d_path;

    // Source-field labels for the physical Value and Z channels.
    std::string primary_value_field_name = "fold";
    std::string z_field_name = "elevation";

    // Empty when not in bundle mode (raw .gs3d / .csv / .dat input).
    std::filesystem::path bundle_dir;
};

struct ViewerWindowConfig {
    unsigned int width = 1280;
    unsigned int height = 720;

    std::string title = "GeoScatter3D 三维散点查看器";
    bool resizable = true;

    // Empty = do not persist the ImGui docking layout.
    std::filesystem::path ui_layout_ini_path = "config/imgui_layout.ini";
    float ui_scale_multiplier = 1.15f;
    std::string theme = "carbon-blue";
    // 顶层布局："workbench"（方案 A）、"floating-dock"（方案 B）
    // 或 "analysis-rail"（方案 C）。运行时仍可在界面里切换。
    std::string layout = "workbench";
    bool enable_multi_viewports = true;

    // --headless 时窗口不显示（无可见 UI、无交互），渲染/交换链/截图
    // 路径与 GUI 模式完全一致。
    bool visible = true;

    // Number of 3D views visible at startup. More can be opened up to 4.
    int viewport_count = 1;
};

struct ViewerGraphicsConfig {
    std::filesystem::path vertex_shader_path;
    std::filesystem::path fragment_shader_path;
    bool enable_validation_layers = true;
    std::string preferred_gpu = "auto";
    std::array<float, 4> clear_color{
        0.118f,
        0.133f,
        0.165f,
        1.0f
    };
    float initial_point_size = 1.5f;
};

struct ViewerCameraConfig {
    std::string mode = "fit";
    std::array<float, 3> position{
        0.0f,
        -14000.0f,
        6000.0f
    };
    std::array<float, 3> target{
        0.0f,
        0.0f,
        0.0f
    };
    std::array<float, 3> up{
        0.0f,
        0.0f,
        1.0f
    };
    float fov_y = 45.0f;
    float near_plane = 1.0f;
    float far_plane = 100000.0f;
};

struct ViewerControllerConfig {
    float rotate_speed = 1.0f;
    float pan_speed = 1.0f;
    float zoom_speed = 1.0f;
    bool invert_rotate_x = false;
    bool invert_rotate_y = false;
    bool invert_pan_x = false;
    bool invert_pan_y = true;
};

struct ViewerLodConfig {
    bool enabled = false;
    bool keep_full_buffer = false;
    std::filesystem::path sidecar_path = "data/test.gs3dlod";
    bool auto_load_sidecar = true;
    std::uint64_t finest_target_points = 2'000'000ull;
    float growth_factor = 1.414f;
    std::uint64_t min_points_per_level = 100'000ull;
    std::string voxel_mode = "XY";
    float voxel_scale = 1.0f;
    double medium_delay_seconds = 0.20;
    double high_delay_seconds = 0.80;
    bool use_lowest_while_interacting = true;
    bool adaptive_interacting_level = false;
    double frame_time_budget_ms = 14.0;
    InteractiveDisplayMode interactive_display_mode =
        InteractiveDisplayMode::KeepStableHighQuality;
    bool verbose = true;
};

struct ViewerTileConfig {
    bool enabled = false;
    std::filesystem::path index_path = "data/test.gs3dtiles.index";
    std::filesystem::path data_path = "data/test.gs3dtiles";
    float min_pixel_size = 50.0f;
    std::uint32_t max_visible_tiles = 0;
    bool use_full_z_range = true;
    bool verbose = true;
    std::uint32_t gpu_cache_max_tiles = 256;
    std::uint64_t gpu_upload_budget_bytes = 32ull * 1024ull * 1024ull;
    std::uint64_t cpu_cache_max_bytes = 512ull * 1024ull * 1024ull;
    bool preload_all = true;
    std::uint64_t preload_max_bytes = 1536ull * 1024ull * 1024ull;
    std::uint64_t preload_upload_budget_bytes = 64ull * 1024ull * 1024ull;
};

struct ViewerBenchmarkConfig {
    bool enabled = false;
    std::uint32_t frame_count = 600;
    std::string present_mode = "auto";
    std::filesystem::path pick_script_path;
    std::filesystem::path pick_result_path;
};

struct ViewerPickDebugConfig {
    bool dump_enabled = false;
    std::filesystem::path dump_dir = "bench/.pick_debug";
    bool dump_once_on_hover = true;
};

struct ViewerAppConfig {
    ViewerInputConfig input;
    ViewerWindowConfig window;
    ViewerGraphicsConfig graphics;
    ViewerCameraConfig camera;
    ViewerControllerConfig controller;
    ViewerLodConfig lod;
    ViewerTileConfig tile;
    ViewerBenchmarkConfig benchmark;
    ViewerPickDebugConfig pick_debug;

    // TIA-109: 控制面（TCP + JSON-RPC）。编译默认包含，运行时默认不监听。
    ViewerControlPlaneConfig control_plane;
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
    void fill_render_views(
        gs3d::app::AppState& app_state,
        const ViewerAppRenderViewContext& ctx,
        const ViewerPickState& pick,
        const std::vector<std::optional<gs3d::camera::Vec3>>& selected_focus_points
    );

    void apply_reset_camera_command(
        const UiActions& gui_cmds,
        ViewerAppCameraCommandContext& ctx
    );

    void apply_camera_view_axis_command(
        const UiActions& gui_cmds,
        ViewerAppCameraCommandContext& ctx
    );

    void apply_project_open_commands(
        const UiActions& gui_cmds,
        gs3d::platform::Window& window
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

    void handle_dataset_export_commands(
        const UiActions& gui_cmds,
        AppState& app_state
    );

    void print_benchmark_report(
        const BenchmarkFrameSamples& samples,
        VkPresentModeKHR present_mode
    ) const;

    ViewerAppConfig config_;
    std::optional<ViewerOpenRequest> open_request_;
    std::future<RegionStatsResult> region_stats_future_;
    std::future<gs3d::data::Gs3dExportResult> dataset_export_future_;
    std::atomic<std::uint64_t>     region_stats_gen_{0};
    int                            region_stats_view_index_ = 0;
};

} // namespace gs3d::app
