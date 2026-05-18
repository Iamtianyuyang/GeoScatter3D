#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace gs3d::app {

struct ViewerAppConfig {
    std::filesystem::path gs3d_path =
        "./data/test.gs3d";

    std::filesystem::path vertex_shader_path =
        "./assets/shaders/point.vert.spv";

    std::filesystem::path fragment_shader_path =
        "./assets/shaders/point.frag.spv";

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