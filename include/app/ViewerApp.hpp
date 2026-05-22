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
        "GeoScatter3D Viewer";

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

    std::string lod_voxel_mode = "XY";
    float lod_voxel_scale = 1.0f;

    double lod_medium_delay_seconds = 0.20;
    double lod_high_delay_seconds = 0.80;

    bool lod_use_lowest_while_interacting = true;
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
