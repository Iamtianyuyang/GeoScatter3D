#include "app/AppConfig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("geoscatter3d-config-test-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]]
    const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out) {
        throw std::runtime_error("failed to write config fixture");
    }
}

[[nodiscard]]
std::string read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

[[nodiscard]]
std::vector<char*> mutable_args(std::vector<std::string>& args)
{
    std::vector<char*> result;
    result.reserve(args.size());
    for (auto& arg : args) {
        result.push_back(arg.data());
    }
    return result;
}

} // namespace

TEST_CASE(
    "AppConfig resolves a portable CSV configuration without modifying it",
    "[app_config][paths]"
) {
    TemporaryDirectory fixture;
    const auto root = fixture.path() / "release";
    const auto config_path = root / "config" / "viewer.toml";
    const auto executable_path = root / "GeoScatter3D";
    const auto csv_path = root / "data" / "input.csv";
    const auto vertex_shader = root / "assets" / "shaders" / "point.vert.spv";
    const auto fragment_shader = root / "assets" / "shaders" / "point.frag.spv";
    std::filesystem::create_directories(executable_path.parent_path());
    write_file(csv_path, "x,y,elevation,fold\n0,0,0,1\n");
    write_file(vertex_shader, "vertex");
    write_file(fragment_shader, "fragment");

    const std::string toml = R"(
[input]
mode = "csv"
csv_path = "data/input.csv"
bundle_dir = "data/output.gs3d.bundle"

[shader]
vertex_shader_path = "assets/shaders/point.vert.spv"
fragment_shader_path = "assets/shaders/point.frag.spv"

[window]
width = 1024
height = 768
multi_viewports = true

[graphics]
preferred_gpu = "auto"

[tile]
enabled = false
gpu_cache_max_tiles = 288
)";
    write_file(config_path, toml);

    std::vector<std::string> args = {
        executable_path.string(),
        "--config",
        config_path.string(),
        "--no-validation"
    };
    auto argv = mutable_args(args);
    const auto config = gs3d::app::AppConfigLoader::load_from_args(
        static_cast<int>(argv.size()),
        argv.data()
    );

    CHECK(config.input_mode == "csv");
    CHECK(config.csv_input_path == csv_path);
    CHECK(config.bundle_dir == root / "data" / "output.gs3d.bundle");
    CHECK(config.viewer.graphics.vertex_shader_path == vertex_shader);
    CHECK(config.viewer.graphics.fragment_shader_path == fragment_shader);
    CHECK(config.viewer.window.width == 1024);
    CHECK(config.viewer.window.height == 768);
    CHECK(config.viewer.window.enable_multi_viewports);
    CHECK(config.viewer.graphics.preferred_gpu == "auto");
    CHECK(config.viewer.tile.gpu_cache_max_tiles == 288);
    CHECK_FALSE(config.viewer.graphics.enable_validation_layers);
    CHECK(read_file(config_path) == toml);
}

TEST_CASE("AppConfig rejects invalid input modes", "[app_config][validation]")
{
    TemporaryDirectory fixture;
    const auto config_path = fixture.path() / "invalid-mode.toml";
    write_file(config_path, "[input]\nmode = \"unknown\"\n");

    CHECK_THROWS_AS(
        gs3d::app::AppConfigLoader::load_from_file(config_path),
        std::runtime_error
    );
}

TEST_CASE("AppConfig rejects negative unsigned values", "[app_config][validation]")
{
    TemporaryDirectory fixture;
    const auto config_path = fixture.path() / "negative-value.toml";
    write_file(config_path, "[window]\nwidth = -1\n");

    CHECK_THROWS_AS(
        gs3d::app::AppConfigLoader::load_from_file(config_path),
        std::runtime_error
    );
}

TEST_CASE("AppConfig rejects a GPU tile budget below the active tile cap", "[app_config][validation]")
{
    TemporaryDirectory fixture;
    const auto config_path = fixture.path() / "invalid-tile-budget.toml";
    write_file(
        config_path,
        "[tile]\nenabled = true\nmax_visible_tiles = 16\ngpu_cache_max_tiles = 8\n"
    );

    CHECK_THROWS_AS(
        gs3d::app::AppConfigLoader::load_from_file(config_path),
        std::runtime_error
    );
}

TEST_CASE("AppConfig rejects invalid runtime domain values", "[app_config][validation]")
{
    TemporaryDirectory fixture;

    const auto expect_rejected = [&fixture](
        const std::string& name,
        const std::string& toml
    ) {
        const auto path = fixture.path() / name;
        write_file(path, toml);
        CHECK_THROWS_AS(
            gs3d::app::AppConfigLoader::load_from_file(path),
            std::runtime_error
        );
    };

    expect_rejected("zero-window.toml", "[window]\nwidth = 0\n");
    expect_rejected(
        "clear-color.toml",
        "[render]\nclear_color = [0.0, 0.0, 0.0, 1.1]\n"
    );
    expect_rejected(
        "camera-depth.toml",
        "[camera]\nnear = 10.0\nfar = 1.0\n"
    );
    expect_rejected("lod-growth.toml", "[lod]\ngrowth_factor = 1.0\n");
    expect_rejected(
        "tile-budget.toml",
        "[tile]\ngpu_upload_budget_bytes = 0\n"
    );
}

TEST_CASE("AppConfig keeps runtime domains independent", "[app_config][domains]")
{
    TemporaryDirectory fixture;
    const auto config_path = fixture.path() / "runtime-domains.toml";
    write_file(config_path, R"(
[input]
mode = "csv"
csv_path = "input.csv"
bundle_dir = "project.gs3d.bundle"

[csv_convert]
num_threads = 3
chunk_bytes = 4096
min_parallel_file_bytes = 8192
x_field = "east"
y_field = "north"
z_field = "depth"
primary_value_field = "grade"

[window]
width = 1600
height = 900
title = "portable test"
resizable = false
ui_layout_ini_path = "layout.ini"
ui_scale_multiplier = 1.4
theme = "instrument-amber"
multi_viewports = false

[vulkan]
validation_layers = false

[graphics]
preferred_gpu = "uuid:0123456789abcdef0123456789abcdef"

[render]
clear_color = [0.1, 0.2, 0.3, 0.4]
initial_point_size = 2.5

[camera]
mode = "explicit"
position = [1.0, 2.0, 3.0]
target = [4.0, 5.0, 6.0]
up = [0.0, 1.0, 0.0]
fov_y = 52.0
near = 0.5
far = 2500.0

[controller]
rotate_speed = 1.5
pan_speed = 0.5
zoom_speed = 2.0
invert_rotate_x = true
invert_rotate_y = true
invert_pan_x = true
invert_pan_y = false

[lod]
enabled = true
keep_full_buffer = true
sidecar_path = "detail.gs3dlod"
auto_load_sidecar = false
auto_save_sidecar = false
finest_target_points = 123456
growth_factor = 1.8
min_points_per_level = 789
voxel_mode = "XYZ"
voxel_scale = 2.5
medium_delay_seconds = 0.3
high_delay_seconds = 1.2
use_lowest_while_interacting = false
adaptive_interacting_level = true
frame_time_budget_ms = 11.0
interactive_display_mode = "coarse"
verbose = false

[viewport]
count = 3

[tile]
enabled = true
index_path = "tiles.index"
data_path = "tiles.data"
min_tile_pixel_size = 88.0
max_visible_tiles = 42
use_full_z_range = false
verbose = false
gpu_cache_max_tiles = 320
gpu_upload_budget_bytes = 65536
cpu_cache_max_bytes = 131072
preload_all = false
preload_max_bytes = 262144
preload_upload_budget_bytes = 32768

[debug]
pick_debug_dump_enabled = true
pick_debug_dump_dir = "diagnostics"
pick_debug_dump_once_on_hover = false
)");

    const auto config = gs3d::app::AppConfigLoader::load_from_file(config_path);

    CHECK(config.input_mode == "csv");
    CHECK(config.csv_convert.num_threads == 3);
    CHECK(config.csv_convert.chunk_bytes == 4096);
    CHECK(config.csv_convert.x_field == "east");
    CHECK(config.csv_convert.primary_value_field == "grade");
    CHECK(config.viewer.window.width == 1600);
    CHECK(config.viewer.window.height == 900);
    CHECK(config.viewer.window.title == "portable test");
    CHECK_FALSE(config.viewer.window.resizable);
    CHECK(config.viewer.window.theme == "instrument-amber");
    CHECK_FALSE(config.viewer.window.enable_multi_viewports);
    CHECK_FALSE(config.viewer.graphics.enable_validation_layers);
    CHECK(config.viewer.graphics.preferred_gpu ==
          "uuid:0123456789abcdef0123456789abcdef");
    CHECK(config.render.clear_color == std::array<float, 4>{0.1f, 0.2f, 0.3f, 0.4f});
    CHECK(config.render.initial_point_size == 2.5f);
    CHECK(config.camera.mode == "explicit");
    CHECK(config.camera.position == std::array<float, 3>{1.0f, 2.0f, 3.0f});
    CHECK(config.camera.far_plane == 2500.0f);
    CHECK(config.controller.rotate_speed == 1.5f);
    CHECK(config.controller.invert_rotate_x);
    CHECK_FALSE(config.controller.invert_pan_y);
    CHECK(config.viewer.lod.enabled);
    CHECK(config.viewer.lod.finest_target_points == 123456);
    CHECK(config.viewer.lod.voxel_mode == "XYZ");
    CHECK(config.viewer.lod.interactive_display_mode ==
          gs3d::app::InteractiveDisplayMode::AllowCoarseLOD);
    CHECK(config.viewer.window.viewport_count == 3);
    CHECK(config.viewer.tile.enabled);
    CHECK(config.viewer.tile.max_visible_tiles == 42);
    CHECK(config.viewer.tile.gpu_cache_max_tiles == 320);
    CHECK(config.viewer.tile.cpu_cache_max_bytes == 131072);
    CHECK(config.viewer.pick_debug.dump_enabled);
    CHECK(config.viewer.pick_debug.dump_dir == "diagnostics");
    CHECK_FALSE(config.viewer.pick_debug.dump_once_on_hover);
}
