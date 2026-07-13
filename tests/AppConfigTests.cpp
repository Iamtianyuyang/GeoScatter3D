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
    CHECK(config.viewer.vertex_shader_path == vertex_shader);
    CHECK(config.viewer.fragment_shader_path == fragment_shader);
    CHECK(config.viewer.window_width == 1024);
    CHECK(config.viewer.window_height == 768);
    CHECK(config.viewer.enable_multi_viewports);
    CHECK(config.viewer.preferred_gpu == "auto");
    CHECK(config.viewer.tile_gpu_cache_max_tiles == 288);
    CHECK_FALSE(config.viewer.enable_validation_layers);
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
