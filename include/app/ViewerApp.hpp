#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gs3d::app {

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

    bool enable_validation_layers = true;

    std::array<float, 4> clear_color{
        0.015f,
        0.018f,
        0.025f,
        1.0f
    };

    float initial_point_size = 1.0f;

    std::string camera_mode = "fixed";

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
    
    std::vector<std::uint64_t> lod_target_point_counts{
        3'000'000ull,
        1'000'000ull,
        300'000ull
    };

    /*
     * 按源点数比例算 target_point_counts，不用每次数据规模变了就手调绝对值
     * （见 include/data/Gs3dLodTargets.hpp）。非空时优先于上面的
     * lod_target_point_counts。默认空——不设置就完全是旧行为。
     */
    std::vector<double> lod_target_point_ratios{};

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
};

class ViewerApp {
public:
    ViewerApp() = default;

    explicit ViewerApp(ViewerAppConfig config);

    [[nodiscard]]
    int run();

private:
    ViewerAppConfig config_;
};

} // namespace gs3d::app
