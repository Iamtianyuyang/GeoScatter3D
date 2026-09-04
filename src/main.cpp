#include "app/AppConfig.hpp"
#include "util/Log.hpp"
#include "app/PreprocessedBundle.hpp"
#include "app/RecentProjects.hpp"
#include "app/UserPreferences.hpp"
#include "app/ViewerApp.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "preprocess/CsvToGs3dConverter.hpp"
#include "preprocess/Gs3dLodWriter.hpp"
#include "preprocess/Gs3dTileWriter.hpp"
#include "ui/Theme.hpp"
#include "util/Stopwatch.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace gs3d::app {
struct ProjectPreprocessProgress {
    float fraction = 0.0f;
    std::uint32_t stage_index = 0;
    std::string stage;
    std::string detail;
};

using ProjectPreprocessProgressCallback =
    std::function<void(const ProjectPreprocessProgress&)>;
}

namespace {

// TIA-109: 控制面开关（编译默认包含，运行时默认不监听）。
//   --control-plane          启用，默认端口 12735
//   --control-plane=PORT     启用并指定端口
//   --control-plane PORT     同上（下个参数为纯数字时视为端口）
void parse_control_plane_args(
    int argc,
    char** argv,
    gs3d::app::ViewerControlPlaneConfig& config
) {
    auto is_all_digits = [](const std::string& text) {
        return !text.empty() &&
               std::all_of(text.begin(), text.end(), [](const char c) {
                   return c >= '0' && c <= '9';
               });
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        constexpr std::string_view kPrefix = "--control-plane=";
        if (arg == "--control-plane") {
            config.enabled = true;
            if (i + 1 < argc && is_all_digits(argv[i + 1])) {
                config.port = static_cast<std::uint16_t>(
                    std::stoi(argv[i + 1])
                );
            }
        } else if (arg.rfind(kPrefix, 0) == 0) {
            config.enabled = true;
            const std::string port_text = arg.substr(kPrefix.size());
            if (!is_all_digits(port_text)) {
                throw std::runtime_error(
                    "--control-plane: invalid port: " + port_text
                );
            }
            config.port = static_cast<std::uint16_t>(
                std::stoi(port_text)
            );
        }
    }
}

bool has_flag(int argc, char** argv, const std::string_view flag) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == flag) {
            return true;
        }
    }
    return false;
}

[[nodiscard]]
gs3d::data::Gs3dLodBuildConfig make_lod_build_config(
    const gs3d::app::ViewerAppConfig& viewer,
    std::uint32_t num_threads
) {
    gs3d::data::Gs3dLodBuildConfig config;
    config.include_full_resolution_level = false;
    config.finest_target_points = viewer.lod.finest_target_points;
    config.growth_factor = viewer.lod.growth_factor;
    config.min_points_per_level = viewer.lod.min_points_per_level;
    config.voxel_scale = viewer.lod.voxel_scale;
    config.num_threads = num_threads;
    config.verbose = viewer.lod.verbose;

    if (viewer.lod.voxel_mode == "XYZ") {
        config.voxel_mode = gs3d::data::Gs3dLodVoxelMode::XYZ;
    } else {
        config.voxel_mode = gs3d::data::Gs3dLodVoxelMode::XY;
    }

    return config;
}

[[nodiscard]]
gs3d::app::PreprocessedBundlePaths resolve_bundle_paths(
    gs3d::app::AppConfig& app_config,
    const std::filesystem::path& source_path
) {
    if (app_config.bundle_dir.empty()) {
        app_config.bundle_dir =
            gs3d::app::derive_bundle_paths(source_path).bundle_dir;
    }

    auto paths =
        gs3d::app::make_bundle_paths(app_config.bundle_dir);
    paths.lod_enabled = app_config.viewer.lod.enabled;
    paths.tile_enabled = app_config.viewer.tile.enabled;
    return paths;
}

void configure_new_project_input(
    gs3d::app::AppConfig& app_config,
    const std::filesystem::path& source_path,
    const std::string& project_name,
    std::uint32_t thread_count
) {
    std::string extension = source_path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        }
    );
    app_config.input_mode =
        extension == ".dat" ? "dat" : "csv";
    app_config.csv_input_path = source_path;
    app_config.bundle_dir =
        source_path.parent_path() /
        (project_name + ".gs3d.bundle");
    const auto effective_threads =
        std::max(1u, thread_count);
    app_config.csv_convert.num_threads =
        effective_threads;
    app_config.tile_build.num_threads =
        effective_threads;
}

void preprocess_csv_input(
    gs3d::app::AppConfig& app_config,
    const gs3d::app::ProjectPreprocessProgressCallback&
        report_progress = {}
) {
    if (app_config.input_mode != "csv" &&
        app_config.input_mode != "dat") {
        return;
    }

    if (app_config.csv_input_path.empty()) {
        throw std::runtime_error(
            "AppConfig: input.mode is \"csv\" but input.csv_path is empty"
        );
    }

    const auto report = [&report_progress](
        float fraction,
        std::uint32_t stage_index,
        std::string stage,
        std::string detail
    ) {
        if (!report_progress) {
            return;
        }
        report_progress({
            .fraction = fraction,
            .stage_index = stage_index,
            .stage = std::move(stage),
            .detail = std::move(detail)
        });
    };

    report(
        0.03f,
        0,
        "检查数据",
        "正在确认源文件、项目目录与输出文件。"
    );
    auto bundle_paths =
        resolve_bundle_paths(
            app_config,
            app_config.csv_input_path
        );
    std::filesystem::create_directories(bundle_paths.bundle_dir);
    gs3d::app::apply_bundle_paths(app_config.viewer, bundle_paths);
    report(
        0.08f,
        0,
        "准备项目",
        "项目目录已就绪，正在分析数据字段。"
    );

    gs3d::util::Stopwatch preprocess_timer;

    gs3d::util::log::info() << "[PREPROCESS] csv_path = "
              << app_config.csv_input_path.string()
              << '\n';
    gs3d::util::log::info() << "[PREPROCESS] bundle_dir = "
              << bundle_paths.bundle_dir.string()
              << '\n';
    gs3d::util::log::info() << "[PREPROCESS] gs3d_path = "
              << app_config.viewer.input.gs3d_path.string()
              << '\n';

    gs3d::data::CsvChunkPlanConfig csv_chunk_plan_config;
    csv_chunk_plan_config.num_threads =
        app_config.csv_convert.num_threads;
    csv_chunk_plan_config.target_chunk_bytes =
        app_config.csv_convert.chunk_bytes;
    csv_chunk_plan_config.min_parallel_file_bytes =
        app_config.csv_convert.min_parallel_file_bytes;

    gs3d::data::CsvReadConfig csv_read_config;
    csv_read_config.schema.x_field =
        app_config.csv_convert.x_field;
    csv_read_config.schema.y_field =
        app_config.csv_convert.y_field;
    csv_read_config.schema.z_field =
        app_config.csv_convert.z_field;
    csv_read_config.schema.primary_value_field =
        app_config.csv_convert.primary_value_field;

    gs3d::preprocess::CsvToGs3dConverter converter(
        csv_read_config,
        csv_chunk_plan_config
    );
    report(
        0.14f,
        1,
        "转换点数据",
        "正在并行读取源文件并转换为 GS3D 数据。"
    );
    gs3d::util::Stopwatch convert_timer;
    auto [convert_result, dataset] =
        converter.convert(
            app_config.csv_input_path,
            app_config.viewer.input.gs3d_path
        );

    gs3d::util::log::info() << "[PREPROCESS] written_points = "
              << convert_result.written_points
              << '\n';
    gs3d::util::log::info() << "[TIME] preprocess.csv_convert_seconds = "
              << convert_timer.elapsed_seconds()
              << '\n';
    {
        std::ostringstream detail;
        detail << "已转换 "
               << convert_result.written_points
               << " 个点，正在构建显示层级与空间索引。";
        report(
            0.58f,
            2,
            "优化数据结构",
            detail.str()
        );
    }

    if (app_config.viewer.lod.enabled) {
        report(
            0.62f,
            2,
            "构建层级细节",
            "正在生成多级细节数据，加快大数据集浏览。"
        );
        gs3d::util::Stopwatch lod_timer;
        const auto lod_dataset =
            gs3d::data::Gs3dLodDataset::build(
                dataset,
                make_lod_build_config(
                    app_config.viewer,
                    app_config.csv_convert.num_threads
                )
            );
        if (lod_dataset.empty()) {
            app_config.viewer.lod.enabled = false;
            bundle_paths.lod_enabled = false;
            gs3d::util::log::info()
                << "[PREPROCESS] LOD skipped: dataset is below "
                << "the configured minimum point count.\n";
        } else {
            const auto lod_stats =
                gs3d::preprocess::Gs3dLodWriter::write(
                    app_config.viewer.lod.sidecar_path,
                    lod_dataset
                );
            gs3d::util::log::info() << "[PREPROCESS] lod_levels = "
                      << lod_stats.level_count
                      << '\n';
        }
        gs3d::util::log::info() << "[TIME] preprocess.lod_write_seconds = "
                  << lod_timer.elapsed_seconds()
                  << '\n';
        report(
            0.74f,
            2,
            "层级细节完成",
            "层级数据已生成，正在准备空间瓦片。"
        );
    }

    if (app_config.viewer.tile.enabled) {
        report(
            0.78f,
            2,
            "构建空间索引",
            "正在按空间位置划分瓦片并写入索引。"
        );
        gs3d::preprocess::Gs3dTileWriteConfig tile_config;
        tile_config.num_threads = app_config.tile_build.num_threads;
        tile_config.verbose = app_config.viewer.tile.verbose;

        gs3d::util::Stopwatch tile_timer;
        const auto tile_stats =
            gs3d::preprocess::Gs3dTileWriter::write(
                app_config.viewer.tile.index_path,
                app_config.viewer.tile.data_path,
                dataset,
                tile_config
            );

        gs3d::util::log::info() << "[PREPROCESS] tile_count = "
                  << tile_stats.tile_count
                  << '\n';
        gs3d::util::log::info() << "[TIME] preprocess.tile_write_seconds = "
                  << tile_timer.elapsed_seconds()
                  << '\n';
        report(
            0.93f,
            2,
            "空间索引完成",
            "空间瓦片已写入，正在保存项目配置。"
        );
    }

    report(
        0.96f,
        3,
        "写入项目",
        "正在生成项目清单并校验输出路径。"
    );
    gs3d::app::write_bundle_manifest(
        bundle_paths,
        app_config,
        dataset
    );
    report(
        1.0f,
        3,
        "预处理完成",
        "数据与项目清单已全部写入。"
    );

    gs3d::util::log::info() << "[TIME] preprocess.total_seconds = "
              << preprocess_timer.elapsed_seconds()
              << '\n';
}

void load_bundle_input(
    gs3d::app::AppConfig& app_config
) {
    if (app_config.input_mode != "bundle") {
        return;
    }

    if (app_config.bundle_dir.empty()) {
        throw std::runtime_error(
            "AppConfig: input.mode is \"bundle\" but input.bundle_dir is "
            "empty"
        );
    }

    const auto bundle_paths =
        gs3d::app::load_bundle_manifest(app_config.bundle_dir);
    gs3d::app::apply_bundle_paths(app_config.viewer, bundle_paths);
}

gs3d::app::ViewerAppConfig make_viewer_config(
    const gs3d::app::AppConfig& app_config
) {
    gs3d::app::ViewerAppConfig viewer = app_config.viewer;

    viewer.graphics.clear_color =
        app_config.render.clear_color;

    viewer.graphics.initial_point_size =
        app_config.render.initial_point_size;

    viewer.input.primary_value_field_name =
        app_config.csv_convert.primary_value_field;

    viewer.input.z_field_name =
        app_config.csv_convert.z_field;

    viewer.camera.mode =
        app_config.camera.mode;

    viewer.camera.position =
        app_config.camera.position;

    viewer.camera.target =
        app_config.camera.target;

    viewer.camera.up =
        app_config.camera.up;

    viewer.camera.fov_y =
        app_config.camera.fov_y;

    viewer.camera.near_plane =
        app_config.camera.near_plane;

    viewer.camera.far_plane =
        app_config.camera.far_plane;

    viewer.controller.rotate_speed =
        app_config.controller.rotate_speed;

    viewer.controller.pan_speed =
        app_config.controller.pan_speed;

    viewer.controller.zoom_speed =
        app_config.controller.zoom_speed;

    viewer.controller.invert_rotate_x =
        app_config.controller.invert_rotate_x;

    viewer.controller.invert_rotate_y =
        app_config.controller.invert_rotate_y;

    viewer.controller.invert_pan_x =
        app_config.controller.invert_pan_x;

    viewer.controller.invert_pan_y =
        app_config.controller.invert_pan_y;

    viewer.input.bundle_dir = app_config.bundle_dir;

    viewer.control_plane = app_config.control_plane;

    return viewer;
}

std::filesystem::path project_directory_from_selection(
    const std::filesystem::path& selection
) {
    if (selection.filename() == "manifest.toml") {
        return selection.parent_path();
    }
    return selection;
}

void apply_open_request(
    gs3d::app::AppConfig& app_config,
    const gs3d::app::ViewerOpenRequest& request
) {
    if (request.kind ==
        gs3d::app::ViewerOpenRequestKind::Project) {
        app_config.input_mode = "bundle";
        app_config.bundle_dir =
            project_directory_from_selection(request.path);
        app_config.csv_input_path.clear();
        return;
    }

    const auto extension = request.path.extension().string();
    if (extension == ".gs3d" || extension == ".GS3D") {
        app_config.input_mode = "gs3d";
        app_config.viewer.input.gs3d_path = request.path;
        app_config.csv_input_path.clear();
        app_config.bundle_dir.clear();
        return;
    }
    app_config.input_mode =
        extension == ".dat" || extension == ".DAT"
            ? "dat"
            : "csv";
    app_config.csv_input_path = request.path;
    app_config.bundle_dir.clear();
}

} // namespace

int main(int argc, char** argv) {
    try {
        auto app_config =
            gs3d::app::AppConfigLoader::load_from_args(
                argc,
                argv
            );

        // TIA-109: 控制面开关（--control-plane[=port] / --headless / --no-welcome）。
        parse_control_plane_args(argc, argv, app_config.control_plane);
        if (has_flag(argc, argv, "--headless")) {
            app_config.viewer.window.visible = false;
            // 隐藏窗口模式下不加载持久化布局：imgui_layout.ini 可能记录着
            // 其他视口（多显示器会话）上的窗口，ImGui 多视口会为它们创建
            // 可见的原生窗口——违背 headless 语义，也让主窗口 swapchain
            // 截图缺内容。TIA-151 实测复现并修复。
            app_config.viewer.window.ui_layout_ini_path.clear();
        }
        const bool no_welcome = has_flag(argc, argv, "--no-welcome");
        // GPU UUIDs identify hardware on one machine. Keep the project
        // template portable and apply a per-user choice only after it loads.
        if (const auto preferred_gpu =
                gs3d::app::load_preferred_gpu_preference()) {
            app_config.viewer.graphics.preferred_gpu = *preferred_gpu;
        }

        // TIA-92: 主题/布局持久化在用户偏好文件 [ui] 段
        if (const auto ui_prefs = gs3d::app::load_ui_preferences()) {
            if (!ui_prefs->theme.empty()) app_config.viewer.window.theme = ui_prefs->theme;
            if (!ui_prefs->layout.empty()) app_config.viewer.window.layout = ui_prefs->layout;
        }

        // 启动主题只记录，不触碰 ImGui；欢迎窗口和主查看器各自的
        // ImGuiLayer::init() 会以 active_theme() 完成首次应用。
        gs3d::ui::set_startup_theme(
            gs3d::ui::theme_from_string(app_config.viewer.window.theme)
        );

        bool show_welcome_window =
            !app_config.viewer.benchmark.enabled && !no_welcome;
        if (show_welcome_window) {
            // 现代化前端迁移：优先拉起已迁移的 Flutter 跨平台前端
            const std::vector<std::filesystem::path> candidates = {
                "ui_flutter/build/windows/x64/runner/Release/ui_flutter.exe",
                "../ui_flutter/build/windows/x64/runner/Release/ui_flutter.exe",
                "build/windows/x64/runner/Release/ui_flutter.exe",
                "ui_flutter.exe",
                "Release/ui_flutter.exe",
            };
            for (const auto& candidate : candidates) {
                if (std::filesystem::exists(candidate)) {
                    gs3d::util::log::info()
                        << "[Flutter] 启动现代化 Flutter 前端: "
                        << candidate.string() << '\n';
#if defined(_WIN32)
                    STARTUPINFOA si{};
                    PROCESS_INFORMATION pi{};
                    si.cb = sizeof(si);
                    std::string cmd = candidate.string();
                    if (CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                        return 0;
                    }
#else
                    std::string cmd = candidate.string() + " &";
                    if (std::system(cmd.c_str()) == 0) return 0;
#endif
                }
            }
            gs3d::util::log::warning()
                << "[Flutter] 未检测到 ui_flutter.exe，回退至命令行/无头数据模式。\n";
        }

        for (;;) {

            preprocess_csv_input(app_config);
            load_bundle_input(app_config);

            if (!app_config.bundle_dir.empty()) {
                gs3d::app::remember_recent_project(
                    app_config.bundle_dir
                );
            }

            gs3d::app::AppConfigPrinter::print(app_config);

            auto viewer_config =
                make_viewer_config(app_config);

            gs3d::app::ViewerApp app(viewer_config);
            const int result = app.run();
            if (result != 0 || !app.open_request().has_value()) {
                return result;
            }

            if (app.open_request()->kind ==
                gs3d::app::ViewerOpenRequestKind::Welcome) {
                show_welcome_window = true;
                continue;
            }
            apply_open_request(app_config, *app.open_request());
        }
    } catch (const std::exception& e) {
        gs3d::util::log::error() << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}
