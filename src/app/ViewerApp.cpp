#include "app/ViewerApp.hpp"
#include "app/ViewerAppGpuPick.hpp"
#include "app/ViewerAppInternal.hpp"
#include "app/ViewerAppRunState.hpp"
#include "app/ViewerAppTileStreaming.hpp"

#include "app/AppState.hpp"
#include "app/PreprocessedBundle.hpp"
#include "app/TilePointCache.hpp"
#include "app/ViewportInteractionState.hpp"
#include "app/ViewportResizeScheduler.hpp"
#include "gui/ImGuiLayer.hpp"
#include "gui/UiFonts.hpp"
#include "ui/SvgLogoTexture.hpp"
#include "ui/UiPalette.hpp"
#include "render/ViewportManager.hpp"
#include "imgui.h"

#include "camera/BoxSelect.hpp"
#include "camera/Camera.hpp"
#include "camera/CameraController.hpp"
#include "camera/CameraHub.hpp"
#include "camera/MouseRay.hpp"
#include "core/DatasetDescriptor.hpp"
#include "data/Gs3dDataset.hpp"
#include "data/Gs3dReader.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "data/Gs3dLodReader.hpp"
#include "data/Gs3dLodTargets.hpp"
#include "data/PointDataAdapters.hpp"
#include "data/TileDataAdapters.hpp"
#include "data/Gs3dTileReader.hpp"

#include "platform/Window.hpp"
#include "render/AxisGrid.hpp"
#include "render/LodSelector.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/PointCloudLodGpu.hpp"
#include "render/PointPipeline.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanRenderer.hpp"
#include "render/VulkanSwapchain.hpp"
#include "render/PointCloudTileGpu.hpp"
#include "render/TileSelection.hpp"
#include "render/VulkanBuffer.hpp"

#include "preprocess/Gs3dLodWriter.hpp"
#include "util/Stopwatch.hpp"
#include "scene/SceneState.hpp"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace gs3d::app {

namespace {

gs3d::camera::CameraBounds make_camera_bounds(
    const gs3d::data::Gs3dDataset& dataset
) {
    gs3d::camera::CameraBounds bounds;

    bounds.min = {
        dataset.bbox_min_x(),
        dataset.bbox_min_y(),
        dataset.bbox_min_z()
    };

    bounds.max = {
        dataset.bbox_max_x(),
        dataset.bbox_max_y(),
        dataset.bbox_max_z()
    };

    return bounds;
}

gs3d::core::Bounds3f make_dataset_bounds(
    const gs3d::data::Gs3dDataset& dataset
) {
    return {
        dataset.bbox_min_x(),
        dataset.bbox_min_y(),
        dataset.bbox_min_z(),
        dataset.bbox_max_x(),
        dataset.bbox_max_y(),
        dataset.bbox_max_z()
    };
}

std::string format_bounds_label(
    const gs3d::core::Bounds3f& bounds
) {
    return
        "[" + std::to_string(bounds.min_x) + ", " +
        std::to_string(bounds.min_y) + ", " +
        std::to_string(bounds.min_z) + "] -> [" +
        std::to_string(bounds.max_x) + ", " +
        std::to_string(bounds.max_y) + ", " +
        std::to_string(bounds.max_z) + "]";
}

gs3d::render::SwapchainPresentModeHint benchmark_present_mode_hint(
    const std::string& mode
) {
    if (mode == "immediate") {
        return gs3d::render::SwapchainPresentModeHint::Immediate;
    }
    if (mode == "mailbox") {
        return gs3d::render::SwapchainPresentModeHint::Mailbox;
    }
    if (mode == "fifo") {
        return gs3d::render::SwapchainPresentModeHint::Fifo;
    }
    return gs3d::render::SwapchainPresentModeHint::Auto;
}

gs3d::data::Gs3dLodVoxelMode parse_lod_voxel_mode(
    const std::string& mode
) {
    if (mode == "XY" || mode == "xy") {
        return gs3d::data::Gs3dLodVoxelMode::XY;
    }

    if (mode == "XYZ" || mode == "xyz") {
        return gs3d::data::Gs3dLodVoxelMode::XYZ;
    }

    throw std::runtime_error(
        "ViewerApp: unsupported LOD voxel_mode: " + mode
    );
}

gs3d::render::TileSelectionConfig make_tile_selection_config(
    const ViewerAppConfig& config
) {
    gs3d::render::TileSelectionConfig tile_config;

    tile_config.min_tile_pixel_size =
        config.tile_min_pixel_size;
    tile_config.max_visible_tiles =
        config.tile_max_visible_tiles;

    tile_config.use_full_z_range =
        config.tile_use_full_z_range;

    return tile_config;
}

gs3d::data::Gs3dLodDataset load_or_build_lod_dataset(
    const gs3d::data::Gs3dDataset& dataset,
    const ViewerAppConfig& config
) {
    if (!config.lod_enabled) {
        return {};
    }

    const auto& sidecar_path =
        config.lod_sidecar_path;

    if (!config.lod_auto_load_sidecar) {
        throw std::runtime_error(
            "ViewerApp: lod.enabled is true but runtime LOD build is "
            "disabled and lod.auto_load_sidecar is false"
        );
    }

    if (sidecar_path.empty()) {
        throw std::runtime_error(
            "ViewerApp: lod.enabled is true but lod.sidecar_path is empty"
        );
    }

    if (!std::filesystem::exists(sidecar_path)) {
        throw std::runtime_error(
            "ViewerApp: required LOD sidecar not found: " +
            sidecar_path.string()
        );
    }

    gs3d::data::Gs3dLodReadConfig read_config;
    read_config.validate_against_source = true;
    read_config.verbose = config.lod_verbose;

    const auto read_result =
        gs3d::data::Gs3dLodReader::read(
            sidecar_path,
            dataset.header(),
            read_config
        );

    std::cout << "[OK] LOD sidecar loaded.\n";
    std::cout << "path = "
              << sidecar_path.string()
              << '\n';

    return read_result.dataset;
}




gs3d::render::PointCloudLodSource build_lod_source(
    const gs3d::data::Gs3dLodDataset& lod_dataset,
    const std::vector<std::vector<std::uint32_t>>& lod_point_ids
) {
    gs3d::render::PointCloudLodSource source;
    source.levels.reserve(lod_dataset.level_count());
    for (std::size_t i = 0; i < lod_dataset.level_count(); ++i) {
        const auto& level = lod_dataset.level(i);
        source.levels.push_back({
            level.name,
            gs3d::data::Gs3dLodDataset::voxel_mode_name(level.voxel_mode),
            level.level_index,
            level.source_point_count,
            level.target_point_count,
            level.voxel_size,
            gs3d::data::make_point_data_view(
                level.points,
                lod_point_ids[i].data()
            )
        });
    }
    return source;
}


void print_dataset_info(
    const gs3d::data::Gs3dDataset& dataset
) {
    std::cout << "[OK] Dataset loaded.\n";
    std::cout << "point_count = " << dataset.point_count() << '\n';
    std::cout << "loaded_point_bytes = "
              << dataset.point_bytes() << '\n';
    std::cout << "metadata_only = "
              << (dataset.metadata_only() ? "true" : "false")
              << '\n';

    std::cout << "bbox_min = ["
              << dataset.bbox_min_x() << ", "
              << dataset.bbox_min_y() << ", "
              << dataset.bbox_min_z() << "]\n";

    std::cout << "bbox_max = ["
              << dataset.bbox_max_x() << ", "
              << dataset.bbox_max_y() << ", "
              << dataset.bbox_max_z() << "]\n";

    std::cout << "value_range = ["
              << dataset.value_min() << ", "
              << dataset.value_max() << "]\n";
}

void print_controls(
    bool lod_enabled,
    bool tile_enabled
){
    std::cout << "[OK] Entering render loop.\n";
    std::cout << "操作说明：\n";
    std::cout << "  左键拖动：轨道旋转\n";
    std::cout << "  右键拖动：视角平移\n";
    std::cout << "  滚轮：缩放到光标位置\n";
    std::cout << "  Ctrl+左键拖动：框选\n";
    std::cout << "  双击点：选择并设置旋转中心\n";
    std::cout << "  F：聚焦选中点\n";
    std::cout << "  + / -：调整点大小\n";
    std::cout << "  R：恢复全局视图\n";
    std::cout << "  Tab：切换着色属性\n";
    std::cout << "  Esc：退出\n";
    std::cout << "渲染模式：\n";
    std::cout << "  LOD         : "
              << (lod_enabled ? "启用" : "关闭")
              << '\n';
    std::cout << "  全分辨率瓦片："
              << (tile_enabled ? "启用" : "关闭")
              << '\n';
}
} // namespace

// Round a world-space distance to a human-readable "nice" value:
//   1, 2, 5, 10, 20, 50, 100, 200, 500, 1000 …
// Follows the same algorithm used by Leaflet (BSD-2) and Cesium (Apache 2).
float nice_scale_distance(float raw)
{
    if (raw <= 0.0f) return 1.0f;
    // Guard against denormalized floats: log10(very small) -> pow underflow -> 0.
    constexpr float kMinRaw = 1.0e-30f;
    if (raw < kMinRaw) return kMinRaw;
    const double mag_d = std::pow(10.0, std::floor(std::log10(static_cast<double>(raw))));
    if (mag_d <= 0.0) return 1.0f;
    const float mag = static_cast<float>(mag_d);
    const float n   = raw / mag;
    if (n < 1.5f) return       mag;
    if (n < 3.5f) return 2.0f * mag;
    if (n < 7.5f) return 5.0f * mag;
    return 10.0f * mag;
}

// 假设数据集坐标单位为米（UTM / 本地网格）。若源数据使用其他单位
// （如英尺、度），需要按数据集配置比例尺单位标签。

// ponytail: 硬编码公制单位，若支持多数据源需改为可配置。
std::string format_scale_distance(float d)
{
    char buf[48];
    if (d >= 1000.0f) {
        std::snprintf(buf, sizeof(buf), "%.0f 千米",
            static_cast<double>(d / 1000.0f));
    } else if (d >= 1.0f) {
        std::snprintf(buf, sizeof(buf), "%.0f 米",
            static_cast<double>(d));
    } else if (d >= 0.01f) {
        std::snprintf(buf, sizeof(buf), "%.0f 厘米",
            static_cast<double>(d * 100.0f));
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f 毫米",
            static_cast<double>(d * 1000.0f));
    }
    return buf;
}

std::string format_vec3_text(const gs3d::camera::Vec3& value)
{
    char buf[96];
    std::snprintf(
        buf,
        sizeof(buf),
        "%.1f, %.1f, %.1f",
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z)
    );
    return buf;
}
namespace {

constexpr double kInteractingDebounceSeconds = 0.15;

} // namespace


ViewerApp::ViewerApp(ViewerAppConfig config)
    : config_(std::move(config))
{
}

int ViewerApp::run() {
    try {
        open_request_.reset();
        gs3d::util::Stopwatch startup_timer;

        const bool can_start_from_metadata =
            config_.lod_enabled &&
            !config_.lod_keep_full_buffer &&
            config_.lod_auto_load_sidecar &&
            !config_.lod_sidecar_path.empty() &&
            std::filesystem::exists(config_.lod_sidecar_path);

        gs3d::util::Stopwatch dataset_load_timer;
        auto dataset = can_start_from_metadata
            ? gs3d::data::Gs3dDatasetLoader::load_header_only(
                config_.gs3d_path
            )
            : gs3d::data::Gs3dDatasetLoader::load(
                config_.gs3d_path
            );
        std::cout << "[TIME] viewer.dataset_load_seconds = "
                  << dataset_load_timer.elapsed_seconds()
                  << '\n';

        if (!dataset.is_consistent()) {
            std::cerr << "[FAIL] dataset is inconsistent.\n";
            return 1;
        }

        if (dataset.point_count() == 0) {
            std::cerr << "[FAIL] dataset is empty.\n";
            return 1;
        }

        print_dataset_info(dataset);
        const auto full_point_ids =
            make_runtime_point_ids(dataset.point_count());

        std::optional<gs3d::data::Gs3dTileReader> tile_reader;
        gs3d::core::TileIndexView tile_index_view;
        std::unordered_map<std::uint64_t, std::vector<std::uint32_t>>
            tile_point_ids_by_tile;

        if (config_.tile_enabled) {
            gs3d::util::Stopwatch tile_reader_timer;
            tile_reader =
                gs3d::data::Gs3dTileReader::open(
                    config_.tile_index_path,
                    config_.tile_data_path,
                    dataset.header()
                );
            std::cout << "[TIME] viewer.tile_reader_open_seconds = "
                      << tile_reader_timer.elapsed_seconds()
                      << '\n';

            if (!tile_reader->valid()) {
                std::cerr << "[FAIL] TileReader is invalid.\n";
                return 1;
            }

            const auto tile_stats =
                tile_reader->stats();

            std::cout << "[OK] TileReader opened.\n";
            std::cout << "tile_count = "
                      << tile_stats.tile_count
                      << '\n';
            std::cout << "tile_total_point_count = "
                      << tile_stats.total_point_count
                      << '\n';
            std::cout << "tile_total_point_bytes = "
                      << tile_stats.total_point_bytes
                      << '\n';

            tile_index_view =
                gs3d::data::make_tile_index_view(*tile_reader);

            const bool tile_has_embedded_ids =
                tile_reader->has_embedded_point_ids();

            if (tile_has_embedded_ids) {
                std::cout
                    << "[OK] Tile format v2 — embedded point IDs. "
                    << "Fast startup (no source point scan needed).\n";
            } else {
                std::cout
                    << "[INFO] Tile format v1 — no embedded point IDs. "
                    << "Using slow startup path.\n";

                if (dataset.metadata_only()) {
                    // tile runtime-id reconstruction needs to scan the
                    // full point buffer (map_subsequence_point_ids does an
                    // exact point match against dataset.points()); in the
                    // metadata-only startup path the dataset has no points
                    // resident, so fall back to loading the full GS3D
                    // before building tile ids.
                    std::cout
                        << "[WARN] Metadata-only startup cannot build "
                        << "tile runtime ids; loading full GS3D data.\n";
                    gs3d::util::Stopwatch fallback_load_timer;
                    dataset = gs3d::data::Gs3dDatasetLoader::load(
                        config_.gs3d_path
                    );
                    std::cout
                        << "[TIME] viewer.dataset_fallback_load_seconds = "
                        << fallback_load_timer.elapsed_seconds()
                        << '\n';
                }

                tile_point_ids_by_tile =
                    build_runtime_tile_point_ids(
                        dataset,
                        *tile_reader,
                        full_point_ids
                    );
            }
        }

        /*
         * Even with v2 tile-embedded IDs, LOD point-id mapping
         * still needs the full source point buffer for exact
         * point matching (map_subsequence_point_ids).  Load it
         * now if we're still in metadata-only mode.
         */
        if (config_.lod_enabled && dataset.metadata_only()) {
            std::cout
                << "[INFO] LOD enabled — loading full GS3D data "
                << "for point-id mapping.\n";
            gs3d::util::Stopwatch lod_load_timer;
            dataset = gs3d::data::Gs3dDatasetLoader::load(
                config_.gs3d_path
            );
            std::cout
                << "[TIME] viewer.lod_dataset_load_seconds = "
                << lod_load_timer.elapsed_seconds()
                << '\n';
        }

        gs3d::data::Gs3dLodDataset lod_dataset;
        std::vector<std::vector<std::uint32_t>> lod_point_ids;

        if (config_.lod_enabled) {
            gs3d::util::Stopwatch lod_timer;
            lod_dataset =
                load_or_build_lod_dataset(
                    dataset,
                    config_
                );
            std::cout << "[TIME] viewer.lod_prepare_seconds = "
                      << lod_timer.elapsed_seconds()
                      << '\n';

            lod_point_ids.reserve(lod_dataset.level_count());
            for (const auto& level : lod_dataset.levels()) {
                lod_point_ids.push_back(
                    map_subsequence_point_ids(
                        dataset.points(),
                        full_point_ids,
                        level.points,
                        "LOD level"
                    )
                );
            }
        }

        std::vector<gs3d::data::Gs3dPoint> runtime_points_by_id(
            full_point_ids.size() + 1
        );
        std::vector<std::uint8_t> runtime_points_valid_by_id(
            full_point_ids.size() + 1,
            0
        );
        if (dataset.has_point_data()) {
            register_runtime_point_lookup(
                dataset.points(),
                full_point_ids,
                runtime_points_by_id,
                runtime_points_valid_by_id
            );
        }
        for (std::size_t i = 0; i < lod_dataset.level_count(); ++i) {
            register_runtime_point_lookup(
                lod_dataset.level(i).points,
                lod_point_ids[i],
                runtime_points_by_id,
                runtime_points_valid_by_id
            );
        }
        
        gs3d::platform::WindowConfig window_config;
        window_config.width = config_.window_width;
        window_config.height = config_.window_height;
        window_config.title = config_.window_title;
        window_config.resizable = config_.window_resizable;

        gs3d::platform::Window window(window_config);

        gs3d::render::VulkanContextConfig vk_config;
        vk_config.enable_validation_layers =
            config_.enable_validation_layers;
        vk_config.application_name = "GeoScatter3D";
        vk_config.preferred_gpu = config_.preferred_gpu;

        gs3d::render::VulkanContext context(window, vk_config);
        std::cout << "[TIME] viewer.startup_seconds = "
                  << startup_timer.elapsed_seconds()
                  << '\n';

        std::cout << "[OK] VulkanContext created.\n";
        std::cout << "Physical device: "
                  << context.physical_device_name() << '\n';

        gs3d::render::VulkanSwapchain swapchain(
            context,
            window,
            benchmark_present_mode_hint(
                config_.benchmark_present_mode
            )
        );
        gs3d::render::VulkanRenderer renderer(context, swapchain);

        gs3d::gui::ImGuiLayer imgui_layer;
        imgui_layer.init(
            window.native_handle(),
            context,
            renderer,
            swapchain.image_count(),
            config_.ui_layout_ini_path,
            config_.ui_scale_multiplier,
            config_.enable_multi_viewports
        );

        // Size the window adaptively to the monitor, mirroring the welcome
        // page's approach but with a wider 16:10 ratio suitable for a
        // multi-panel workbench.
        {
            const float ui_scale = gs3d::gui::ui_fonts().ui_scale;
            constexpr int kBaseOuterWidth = 1440;
            constexpr int kMinOuterWidth = 1100;
            constexpr float kAspect = 16.0f / 10.0f;

            int outer_w = std::max(kMinOuterWidth,
                static_cast<int>(std::lround(
                    static_cast<float>(kBaseOuterWidth) * ui_scale)));

            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            if (monitor != nullptr) {
                int work_x = 0, work_y = 0, work_w = 0, work_h = 0;
                glfwGetMonitorWorkarea(
                    monitor, &work_x, &work_y, &work_w, &work_h);
                if (work_w > 0 && work_h > 0) {
                    const int w_limit = static_cast<int>(
                        std::floor(static_cast<float>(work_w) * 0.85f));
                    const int h_limit = static_cast<int>(
                        std::floor(static_cast<float>(work_h) * 0.85f));
                    const int w_from_h = static_cast<int>(
                        std::floor(static_cast<float>(h_limit) * kAspect));
                    outer_w = std::clamp(outer_w, kMinOuterWidth,
                        std::max(kMinOuterWidth,
                                 std::min(w_limit, w_from_h)));
                }
            }

            const int outer_h = static_cast<int>(std::lround(
                static_cast<float>(outer_w) / kAspect));

            int frame_l = 0, frame_t = 0, frame_r = 0, frame_b = 0;
            glfwGetWindowFrameSize(window.native_handle(),
                                   &frame_l, &frame_t, &frame_r, &frame_b);
            const int client_w = std::max(1, outer_w - frame_l - frame_r);
            const int client_h = std::max(1, outer_h - frame_t - frame_b);
            glfwSetWindowSize(window.native_handle(), client_w, client_h);

            GLFWmonitor* center_monitor = glfwGetPrimaryMonitor();
            if (center_monitor != nullptr) {
                int work_x = 0, work_y = 0, work_w = 0, work_h = 0;
                glfwGetMonitorWorkarea(center_monitor,
                                       &work_x, &work_y, &work_w, &work_h);
                glfwSetWindowPos(window.native_handle(),
                                 work_x + (work_w - outer_w) / 2,
                                 work_y + (work_h - outer_h) / 2);
            }
        }

        // 交换链是 UNORM 格式——配置里的 clear_color 按 sRGB 语义书写，
        // 直接使用，无需颜色空间转换。
        gs3d::render::ClearColor clear_color;
        clear_color.r = config_.clear_color[0];
        clear_color.g = config_.clear_color[1];
        clear_color.b = config_.clear_color[2];
        clear_color.a = config_.clear_color[3];
        renderer.set_clear_color(clear_color);

        gs3d::ui::SvgLogoTexture logo_texture(
            context.device(),
            context.physical_device(),
            context.graphics_queue(),
            renderer.command_pool(),
            "assets/icon.svg",
            128
        );
        std::cout << "[OK] SvgLogoTexture loaded.\n";

        std::unique_ptr<gs3d::render::PointCloudGpu> full_gpu_cloud;
        std::unique_ptr<gs3d::render::PointCloudLodGpu> lod_gpu_cloud;
        std::unique_ptr<gs3d::render::PointCloudTileGpu> tile_gpu_cloud;

        if (config_.lod_enabled) {
            const auto lod_source =
                build_lod_source(lod_dataset, lod_point_ids);
            lod_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudLodGpu>(
                    context,
                    renderer.command_pool(),
                    context.graphics_queue(),
                    lod_source
                );

            std::cout << "[OK] PointCloudLodGpu uploaded.\n";
            std::cout << lod_gpu_cloud->summary();

        } else {
            full_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudGpu>(
                    context,
                    renderer.command_pool(),
                    context.graphics_queue(),
                    gs3d::data::make_point_data_view(
                        dataset,
                        full_point_ids.data()
                    )
                );

            std::cout << "[OK] PointCloudGpu uploaded.\n";
            std::cout << "gpu point_count = "
                      << full_gpu_cloud->point_count()
                      << '\n';
        }

        // Camera must be initialized before ViewportManager (which clones it).
        // Bounds are needed for fit-mode and for the CameraController.
        const gs3d::camera::CameraBounds bounds =
            make_camera_bounds(dataset);

        gs3d::camera::Camera initial_camera;
        VkExtent2D initial_viewport_extent = swapchain.extent();
        initial_camera.set_viewport(
            initial_viewport_extent.width,
            initial_viewport_extent.height
        );
        initialize_camera_from_config(initial_camera, config_, bounds);

        // ViewportManager: N (OffscreenFramebuffer, Camera) pairs.
        // All framebuffers use swapchain.image_format() → Vulkan-compatible with
        // each other, so a single PointPipeline works for all viewports.
        // Must be created after ImGui init (registers descriptors via AddTexture).
        gs3d::render::ViewportManager viewport_manager;
        viewport_manager.init(
            context,
            swapchain.image_format(),
            kMaxViewportCount,
            initial_viewport_extent,
            initial_camera
        );
        viewport_manager.set_active_count(
            std::clamp(config_.viewport_count, 1, kMaxViewportCount)
        );
        viewport_manager.set_clear_color(clear_color);

        gs3d::render::PointPipelineConfig pipeline_config;
        pipeline_config.vertex_shader_path =
            config_.vertex_shader_path;
        pipeline_config.fragment_shader_path =
            config_.fragment_shader_path;

        gs3d::render::PointPipeline point_pipeline(
            context,
            viewport_manager.render_pass(),
            pipeline_config
        );

        std::cout << "[OK] PointPipeline created.\n";

        GpuPickReadback gpu_pick_readback(
            context,
            renderer.frames_in_flight(),
            static_cast<std::size_t>(viewport_manager.viewport_count())
        );
        PickDebugFrameDumper pick_debug_frame_dumper(
            context,
            renderer.frames_in_flight()
        );
        std::uint64_t pick_debug_dump_count = 0;
        bool pick_debug_dump_completed = false;
        std::vector<bool> pending_hover_miss_dump(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            false
        );
        ViewerAppPickState pick;
        {
            const auto n = static_cast<std::size_t>(viewport_manager.viewport_count());
            pick.latest_hover_points.resize(n);
            pick.latest_capture_x.resize(n, 0.0f);
            pick.latest_capture_y.resize(n, 0.0f);
            // Frames since last successful pick hit — clears stale hover
            // data after ~0.5 s of no hits.
            pick.hover_timeout.resize(n, 0);
            // Consecutive GPU pick misses (has_hit=false) — clears hover
            // after a short debounce so moving between points doesn't flicker.
            pick.consecutive_no_hit.resize(n, 0);
            pick.requests.resize(n);
        }
        const bool benchmark_pick_enabled =
            config_.benchmark_mode &&
            !config_.benchmark_pick_script_path.empty();
        const auto benchmark_pick_queries =
            benchmark_pick_enabled
                ? load_benchmark_pick_script(
                      config_.benchmark_pick_script_path
                  )
                : std::vector<BenchmarkPickScriptQuery>{};
        std::vector<double> benchmark_pick_issue_cpu_ms(
            benchmark_pick_queries.size(),
            0.0
        );
        std::vector<BenchmarkPickIssuedMetadata> benchmark_pick_issue_metadata(
            benchmark_pick_queries.size()
        );
        std::vector<BenchmarkPickObservedResult> benchmark_pick_results;
        benchmark_pick_results.reserve(benchmark_pick_queries.size());
        std::size_t benchmark_pick_issue_index = 0;
        constexpr std::uint32_t kBenchmarkPickWarmupFrames = 80;
        std::uint32_t benchmark_target_frame_count =
            config_.benchmark_frame_count;
        if (benchmark_pick_enabled) {
            benchmark_target_frame_count = static_cast<std::uint32_t>(
                kBenchmarkPickWarmupFrames +
                benchmark_pick_queries.size() +
                static_cast<std::size_t>(renderer.frames_in_flight()) + 1
            );
        }

        gs3d::camera::CameraControllerConfig controller_config;
        controller_config.rotate_speed =
            config_.controller_rotate_speed;
        controller_config.pan_speed =
            config_.controller_pan_speed;
        controller_config.zoom_speed =
            config_.controller_zoom_speed;
        controller_config.invert_rotate_x =
            config_.controller_invert_rotate_x;
        controller_config.invert_rotate_y =
            config_.controller_invert_rotate_y;
        controller_config.invert_pan_x =
            config_.controller_invert_pan_x;
        controller_config.invert_pan_y =
            config_.controller_invert_pan_y;

        // One controller per viewport so each can be interacted with independently.
        std::vector<gs3d::camera::CameraController> controllers;
        controllers.reserve(
            static_cast<std::size_t>(viewport_manager.viewport_count())
        );
        for (int i = 0; i < viewport_manager.viewport_count(); ++i) {
            controllers.emplace_back(controller_config);
            controllers.back().set_bounds(bounds);
        }
        std::vector<std::optional<gs3d::camera::Vec3>>
            selected_focus_points(
                static_cast<std::size_t>(viewport_manager.viewport_count())
            );

        ViewportInteractionState viewport_interaction(
            static_cast<std::size_t>(viewport_manager.viewport_count())
        );

        // Views start independent. The per-view UI can opt into sync group 0.
        gs3d::camera::CameraHub camera_hub;
        for (int i = 0; i < viewport_manager.viewport_count(); ++i) {
            camera_hub.add(
                i,
                &viewport_manager.camera(i),
                gs3d::camera::CameraHub::kIndependent
            );
        }

        std::cout << "[OK] CameraController initialized.\n";
        std::cout << "camera position = ["
                  << viewport_manager.camera(0).position().x << ", "
                  << viewport_manager.camera(0).position().y << ", "
                  << viewport_manager.camera(0).position().z << "]\n";

        std::cout << "camera target = ["
                  << viewport_manager.camera(0).target().x << ", "
                  << viewport_manager.camera(0).target().y << ", "
                  << viewport_manager.camera(0).target().z << "]\n";

        std::cout << "camera distance = "
                  << viewport_manager.camera(0).distance() << '\n';

        gs3d::render::PointPushConstants push{};
        push.point_size  = config_.initial_point_size;
        // Channel attributes set below after attr_list is built.
        // MVP is set per-viewport inside render_all; flags is zero-initialized.

        /*
         * 瓦片流式状态：每视口可见瓦片集与 clip 包围盒、CPU 缓存、异步
         * future、全量预加载进度。见 ViewerAppTileStreaming.hpp；必须
         * 声明在 tile_reader / tile_point_ids_by_tile 之后（后台 future
         * 引用它们，析构时先 join future）。
         */
        ViewerAppTileStreamState tile_stream(
            config_.tile_cpu_cache_max_bytes
        );
        tile_stream.preload_enabled =
            config_.tile_enabled &&
            config_.tile_preload_all &&
            !config_.benchmark_mode &&
            tile_reader.has_value() &&
            !tile_reader->records().empty() &&
            tile_reader->stats().total_point_bytes <=
                config_.tile_preload_max_bytes;

        // Debounce interacting so rapid scroll zoom doesn't cause
        // frame-by-frame toggling (tiles pop in/out, LOD clip flicker).
        auto interacting_debounce_until =
            std::chrono::steady_clock::now();

        const auto resolve_hover_point_from_visible_tiles =
            [&tile_stream](
                std::size_t view_index,
                std::uint32_t point_id,
                float mouse_x,
                float mouse_y
            ) {
                static_cast<void>(mouse_x);
                static_cast<void>(mouse_y);
                const auto candidate_point_sets =
                    collect_visible_hover_tile_views(
                        tile_stream,
                        view_index
                    );
                if (candidate_point_sets.empty()) {
                    return std::optional<gs3d::data::Gs3dPoint>{};
                }

                if (const auto exact =
                        find_point_by_id_in_views(
                            candidate_point_sets,
                            point_id
                        )) {
                    return exact;
                }
                return std::optional<gs3d::data::Gs3dPoint>{};
            };

        ViewportResizeScheduler viewport_resize_scheduler(0.15);

        // Benchmark-mode instrumentation (no-ops when benchmark_mode is false).
        std::uint64_t app_frame_index = 0;
        std::uint32_t benchmark_frame_index = 0;
        ViewerAppBenchmarkFrameSamples benchmark_samples;
        if (config_.benchmark_mode) {
            benchmark_samples.reserve_frames(config_.benchmark_frame_count);
        }
        // Orbit for the first 2/3 of the run (measures interaction frame
        // time), then hold still for the last 1/3 (measures tile/LOD
        // settle/reload latency once the camera stops moving).
        const std::uint32_t benchmark_orbit_frames =
            config_.benchmark_frame_count * 2 / 3;

        gs3d::render::LodSelector lod_selector;

        if (config_.lod_enabled) {
            gs3d::render::LodSelectorConfig lod_selector_config;
            lod_selector_config.medium_delay_seconds =
                config_.lod_medium_delay_seconds;
            lod_selector_config.high_delay_seconds =
                config_.lod_high_delay_seconds;
            lod_selector_config.use_lowest_while_interacting =
                config_.lod_use_lowest_while_interacting;
            lod_selector_config.adaptive_interacting_level =
                config_.lod_adaptive_interacting_level;
            lod_selector_config.frame_time_budget_ms =
                config_.lod_frame_time_budget_ms;

            lod_selector.set_config(lod_selector_config);
        }

        gs3d::render::TileSelection tile_selection;
        gs3d::render::TileSelectionResult tile_result;
        bool tile_selection_dirty = true;
        int streaming_viewport_index = 0;

        if (config_.tile_enabled && tile_reader.has_value()) {
            tile_selection.set_config(
                make_tile_selection_config(config_)
            );

            tile_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudTileGpu>();
            tile_gpu_cloud->set_resident_tile_budget(
                config_.tile_gpu_cache_max_tiles
            );

            std::cout << "[OK] TileSelection initialized.\n";
        }

        bool r_was_pressed = false;
        bool f_was_pressed = false;
        bool tab_was_pressed = false;
        bool shift_tab_was_pressed = false;

        const std::string primary_value_name =
            config_.primary_value_field_name.empty()
                ? "value"
                : config_.primary_value_field_name;
        const std::string z_field_name =
            config_.z_field_name.empty()
                ? "z"
                : config_.z_field_name;

        // Logical names come from preprocessing; physical slots stay Value/Z.
        const std::vector<gs3d::app::AttrDescriptor> attr_list = {
            { primary_value_name,
              gs3d::app::AttrPhysicalSource::Value,
              dataset.value_min(),
              dataset.value_max() },
            { z_field_name,
              gs3d::app::AttrPhysicalSource::Z,
              dataset.bbox_min_z(),
              dataset.bbox_max_z() }
        };

        // 高程范围 — 用于非空间属性映射到物理 Z 坐标时的基准。
        const float elev_min   = dataset.bbox_min_z();
        const float elev_range = dataset.bbox_max_z() - dataset.bbox_min_z();

        // 当前高度夸张系数（跨源持久）。
        float height_exag = 1.0f;

        // 根据属性描述 + 夸张系数计算 height_offset / height_mult。
        auto apply_height_attr_to = [&](
            gs3d::render::PointPushConstants& target_push,
            const gs3d::app::AttrDescriptor& a,
            float exag
        ) {
            target_push.height_source = static_cast<std::uint32_t>(a.source);
            if (a.source == gs3d::app::AttrPhysicalSource::Z) {
                // 高程值已在空间尺度，stretch around origin
                target_push.height_mult   = exag;
                target_push.height_offset = 0.0f;
            } else {
                // 非空间属性 → 线性映射到 [elev_min, elev_min + elev_range * exag]
                const float r = a.range();
                const float m = (r > 0.0f) ? (elev_range / r * exag) : exag;
                target_push.height_mult   = m;
                target_push.height_offset = elev_min - a.min_val * m;
            }
        };

        // 初始化默认通道：颜色=fold (attr_list[0]), 高度=高程 (attr_list[1])
        {
            const auto& c = attr_list[0];
            push.color_source = static_cast<std::uint32_t>(c.source);
            push.color_min    = c.min_val;
            push.color_range  = c.range();
            if (push.color_range <= 0.0f) push.color_range = 1.0f;
        }
        apply_height_attr_to(push, attr_list[1], height_exag);

        // 初始化默认色标：Rainbow256 (索引 8)
        push.flags &= ~gs3d::render::PointFlags::kColormapMask;
        push.flags |= (8u << 1) & gs3d::render::PointFlags::kColormapMask;

        std::size_t last_lod_level =
             static_cast<std::size_t>(-1);
        // Hoisted out of the loop body so report_frame_time() can pair the
        // level rendered in frame N-1 with frame N-1's measured duration
        // (delta_seconds, computed at the top of frame N) before this
        // frame reassigns it.
        std::size_t lod_level_for_frame = 0;

        /*
         * 安全网：最后一次确认可用的 LOD 级别。select_level() 理论上
         * 永远返回有效值（LOD GPU cloud 所有级别都是启动时预上传的），
         * 但万一出现越界或无效索引，退回到 last_valid 而不是画空帧。
         */
        std::size_t last_valid_lod_level = 0;

        /*
         * KeepStableHighQuality 模式下,交互期间冻结的显示档位。空闲时持续
         * 刷新为当前稳定显示的档位(会收敛到 level 0 = 最高细节);交互开始
         * 时锁定该值,交互途中绝不切到比它更粗的档位。lower index = 更精细。
         */
        std::size_t frozen_display_lod = 0;

        /*
         * 空间选层的上一帧结果（用于滞回）。初始化为 level_count
         * 表示"无前值"，首帧不出滞回。
         */
        std::size_t last_spatial_level =
            static_cast<std::size_t>(-1);

        /*
         * 交互期间冻结的空间目标层。交互中不跟踪 ortho_height 变化，
         * 停手后才更新——与 tile 选择的 !interacting 门控语义一致，
         * 避免缩放中 LOD 硬切导致抽稀跳变。
         */
        std::size_t frozen_spatial_level =
            static_cast<std::size_t>(-1);

        auto previous_time =
            std::chrono::steady_clock::now();

        // Smoothed FPS via exponential moving average
        float fps_smooth = 0.0f;

        print_controls(
                        config_.lod_enabled,
                        config_.tile_enabled
                    );

        gs3d::core::DatasetDescriptor dataset_descriptor;
        dataset_descriptor.display_name =
            config_.gs3d_path.filename().string();
        dataset_descriptor.path = config_.gs3d_path.string();
        dataset_descriptor.format = "GS3D";
        dataset_descriptor.point_count = dataset.point_count();
        dataset_descriptor.bounds = make_dataset_bounds(dataset);
        dataset_descriptor.dataset_tree = {
            dataset_descriptor.display_name,
            "瓦片",
            "细节层级",
            "属性"
        };
        dataset_descriptor.attributes.clear();
        for (const auto& attr : attr_list) {
            dataset_descriptor.attributes.push_back(
                gs3d::core::AttributeDescriptor{attr.name});
        }
        {
            std::error_code ec;
            const auto file_bytes = std::filesystem::file_size(config_.gs3d_path, ec);
            if (!ec) {
                const double mb = static_cast<double>(file_bytes) / (1024.0 * 1024.0);
                std::ostringstream oss;
                oss.setf(std::ios::fixed);
                oss.precision(2);
                oss << mb << " MB";
                dataset_descriptor.file_size = oss.str();
            }
        }
        gs3d::scene::SceneState scene_state;
        scene_state.active_dataset = &dataset_descriptor;
        scene_state.active_attribute_index = 0;  // 颜色=fold (attr_list[0])
        scene_state.active_height_index    = 1;  // 高度=高程 (attr_list[1])
        std::vector<gs3d::render::PointPushConstants> viewport_pushes(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            push
        );
        std::vector<gs3d::scene::SceneState> viewport_scene_states(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            scene_state
        );
        std::vector<float> viewport_height_exags(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            height_exag
        );
        gs3d::app::AppState app_state;
        app_state.dataset.active_dataset = dataset_descriptor.display_name;
        app_state.dataset.path = dataset_descriptor.path;
        app_state.dataset.format = dataset_descriptor.format;
        app_state.dataset.point_count = dataset_descriptor.point_count;
        app_state.dataset.loaded_points = dataset_descriptor.point_count;
        app_state.dataset.file_size = dataset_descriptor.file_size;
        app_state.dataset.bounding_box =
            format_bounds_label(dataset_descriptor.bounds);
        app_state.dataset.dataset_tree = dataset_descriptor.dataset_tree;
        app_state.dataset.attributes.clear();
        app_state.dataset.tile_details.clear();
        if (tile_reader.has_value()) {
            const auto tile_stats = tile_reader->stats();
            app_state.dataset.tile_details = {
                "状态：已启用",
                "瓦片数：" + std::to_string(tile_stats.tile_count),
                "瓦片内点数：" +
                    std::to_string(tile_stats.total_point_count),
                "瓦片数据：" +
                    std::to_string(tile_stats.total_point_bytes / 1024 / 1024) +
                    " MB",
                tile_stream.preload_enabled
                    ? "加载方式：全量预加载"
                    : "加载方式：按需流式"
            };
        } else {
            app_state.dataset.tile_details = {"状态：未启用"};
        }
        app_state.dataset.lod_details.clear();
        if (!lod_dataset.empty()) {
            app_state.dataset.lod_details.push_back(
                "状态：已启用（" +
                std::to_string(lod_dataset.level_count()) + " 层）"
            );
            for (std::size_t i = 0; i < lod_dataset.level_count(); ++i) {
                const auto& level = lod_dataset.level(i);
                app_state.dataset.lod_details.push_back(
                    level.name + "：" +
                    std::to_string(level.target_point_count) + " 点"
                );
            }
        } else {
            app_state.dataset.lod_details = {"状态：未启用"};
        }
        app_state.render_settings.height_by_options.clear();
        app_state.render_settings.color_by_options.clear();
        for (const auto& attr : attr_list) {
            app_state.dataset.attributes.push_back(attr.name);
            app_state.render_settings.height_by_options.push_back(attr.name);
            app_state.render_settings.color_by_options.push_back(attr.name);
        }
        const auto viewport_state_count =
            static_cast<std::size_t>(viewport_manager.viewport_count());
        app_state.render_settings_by_view.assign(
            viewport_state_count,
            app_state.render_settings
        );
        app_state.navigation_maps.resize(viewport_state_count);
        app_state.measurements.resize(viewport_state_count);
        app_state.region_stats_by_view.resize(viewport_state_count);
        app_state.render_views.resize(
            viewport_state_count
        );
        const int startup_view_count =
            std::clamp(config_.viewport_count, 1, kMaxViewportCount);
        for (int i = 0; i < viewport_manager.viewport_count(); ++i) {
            auto& view =
                app_state.render_views[static_cast<std::size_t>(i)];
            view.viewport_index = i;
            view.visible = i < startup_view_count;
            view.camera_linked = false;
        }
        std::vector<bool> previous_view_visible(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            false
        );
        for (const auto& view : app_state.render_views) {
            if (view.viewport_index >= 0 &&
                view.viewport_index <
                    static_cast<int>(previous_view_visible.size())) {
                previous_view_visible[
                    static_cast<std::size_t>(view.viewport_index)
                ] = view.visible;
            }
        }
        if (config_.benchmark_mode) {
            app_state.panels.dataset = false;
            app_state.panels.render_settings = false;
            app_state.panels.debug_log = false;
            app_state.panels.tile_inspector = false;
            app_state.panels.lod_view = false;
            app_state.panels.performance = false;
        }

        app_state.logo_texture = logo_texture.descriptor();

        // ── analysis.toml persistence ──────────────────────────────────
        app_state.bundle_dir = config_.bundle_dir;
        auto& persisted_measurement =
            app_state.measurements.empty()
                ? app_state.measurement
                : app_state.measurements.front();
        load_analysis(app_state.bundle_dir, persisted_measurement);
        app_state.measurement = persisted_measurement;
        persisted_measurement.on_changed = [&app_state]() {
            const auto& measurement =
                app_state.measurements.empty()
                    ? app_state.measurement
                    : app_state.measurements.front();
            save_analysis(app_state.bundle_dir, measurement);
        };

        // ── 导航图缩略图：离屏预渲染到独立 framebuffer ─────────────────
        // 尺寸/坐标映射与首帧渲染都在 init_navigation_map 里完成；
        // 着色属性变更后由 pre_pass 里的 record_navigation_thumbnail 重渲。
        std::vector<gs3d::render::OffscreenFramebuffer> nav_thumbnail_fbs(
            viewport_state_count
        );
        const auto& nav_cloud =
            lod_gpu_cloud
                ? lod_gpu_cloud->lowest_detail().gpu_cloud
                : *full_gpu_cloud;
        for (std::size_t i = 0; i < app_state.navigation_maps.size(); ++i) {
            const ViewerAppNavThumbnailContext nav_thumbnail_ctx{
                point_pipeline,
                nav_cloud,
                i < viewport_pushes.size()
                    ? viewport_pushes[i]
                    : viewport_pushes.front(),
                dataset.bbox_max_z()
            };
            init_navigation_map(
                context,
                renderer.command_pool(),
                swapchain.image_format(),
                dataset,
                nav_thumbnail_fbs[i],
                app_state.navigation_maps[i],
                nav_thumbnail_ctx
            );
        }
        if (!app_state.navigation_maps.empty()) {
            app_state.navigation_map = app_state.navigation_maps.front();
        }

        std::vector<int> visible_viewports;
        visible_viewports.reserve(
            static_cast<std::size_t>(viewport_manager.viewport_count())
        );

        const auto consume_ready_pick_frame_slot =
            [this,
             &pick,
             &pick_debug_frame_dumper,
             &gpu_pick_readback,
             &runtime_points_by_id,
             &runtime_points_valid_by_id,
             &controllers,
             &selected_focus_points,
             &viewport_manager,
             &bounds,
             &viewport_pushes,
             &streaming_viewport_index,
             &tile_selection_dirty,
             &benchmark_pick_enabled,
             &benchmark_pick_issue_cpu_ms,
             &benchmark_pick_issue_metadata,
             &benchmark_pick_results,
             &resolve_hover_point_from_visible_tiles]
            (std::uint32_t frame_slot) {
                ViewerAppPickLookupContext pick_lookup{
                    runtime_points_by_id,
                    runtime_points_valid_by_id
                };
                ViewerAppPickCameraContext pick_camera{
                    controllers,
                    selected_focus_points,
                    viewport_manager,
                    bounds,
                    viewport_pushes,
                    streaming_viewport_index,
                    tile_selection_dirty
                };
                ViewerAppBenchmarkPickContext pick_benchmark{
                    benchmark_pick_enabled,
                    benchmark_pick_issue_cpu_ms,
                    benchmark_pick_issue_metadata,
                    benchmark_pick_results
                };
                this->consume_ready_pick_frame_slot(
                    frame_slot, pick, pick_debug_frame_dumper,
                    gpu_pick_readback, pick_lookup, pick_camera,
                    pick_benchmark, resolve_hover_point_from_visible_tiles);
            };

        // ponytail: screenshot staging — allocated on demand in post_pass, read
        // back after draw_frame. Only one screenshot at a time.
        ViewerAppScreenshotCaptureState screenshot_capture;

        while (!window.should_close() &&
               (!config_.benchmark_mode ||
                benchmark_frame_index < benchmark_target_frame_count)) {
            gs3d::util::Stopwatch benchmark_frame_timer;
            double benchmark_cpu_frame_ms = 0.0;
            double benchmark_camera_update_ms_frame = 0.0;
            double benchmark_lod_tile_select_ms_frame = 0.0;
            double benchmark_cpu_cull_ms_frame = 0.0;
            double benchmark_upload_record_ms_frame = 0.0;
            const auto current_time =
                std::chrono::steady_clock::now();

            const double delta_seconds =
                std::chrono::duration<double>(
                    current_time - previous_time
                ).count();

            previous_time = current_time;

            for (std::uint32_t frame_slot = 0;
                 frame_slot < renderer.frames_in_flight();
                 ++frame_slot) {
                if (!renderer.is_frame_slot_ready(frame_slot)) {
                    continue;
                }
                consume_ready_pick_frame_slot(frame_slot);
            }

            // Pairs the level rendered last frame with its measured
            // duration, driving LodSelectorConfig::adaptive_interacting_level
            // (no-op otherwise). Must run before lod_level_for_frame is
            // reassigned for *this* frame, further down.
            if (config_.lod_enabled && delta_seconds > 0.0) {
                /*
                 * On VK_PRESENT_MODE_FIFO_KHR the total wall-clock frame
                 * time includes vsync present-wait inside vkAcquireNextImageKHR
                 * (~16.67ms at 60Hz).  Subtracting that wait gives a better
                 * estimate of actual GPU render time so the adaptive LOD
                 * budget (14ms) doesn't silently pin to the lowest level on
                 * FIFO-only systems (iGPU / older hardware).
                 */
                const double present_wait_ms =
                    renderer.last_acquire_wait_ms();
                const double report_ms =
                    (delta_seconds * 1000.0) - present_wait_ms;
                lod_selector.report_frame_time(
                    lod_level_for_frame,
                    report_ms > 0.0 ? report_ms : 0.0
                );
            }

            window.poll_events();

            // Update FPS (exponential moving average, α=0.1)
            if (delta_seconds > 0.0) {
                const float frame_fps =
                    static_cast<float>(1.0 / delta_seconds);
                fps_smooth = fps_smooth > 0.0f
                    ? fps_smooth * 0.9f + frame_fps * 0.1f
                    : frame_fps;
            }

            const int n_viewports = viewport_manager.viewport_count();
            const auto& primary_camera =
                viewport_manager.camera(streaming_viewport_index);

            std::uint32_t loaded_tiles = 0;
            // Pending = required GPU working-set tiles not yet resident.
            // Stages 1/2 fall back to the full candidate set.
            std::size_t pending_tile_count = 0;
            if (config_.tile_enabled && tile_gpu_cloud &&
                tile_result.enabled) {
                const auto& pending_source =
                    tile_stream.gpu_required_tile_ids.empty()
                        ? tile_result.tile_ids
                        : tile_stream.gpu_required_tile_ids;
                for (const auto tile_id : pending_source) {
                    if (!tile_gpu_cloud->has_resident_tile(tile_id)) {
                        ++pending_tile_count;
                    }
                }
            }
            if (tile_stream.load_future.valid()) {
                pending_tile_count = std::max(
                    pending_tile_count,
                    tile_stream.loading_ids.size());
            }
            const std::uint32_t pending_tiles =
                static_cast<std::uint32_t>(
                    std::min<std::size_t>(
                        pending_tile_count,
                        std::numeric_limits<std::uint32_t>::max()
                    )
                );
            std::uint64_t gpu_buffer_bytes = 0;
            std::uint64_t gpu_resident_points = 0;

            if (config_.tile_enabled && tile_gpu_cloud) {
                const auto& ts = tile_gpu_cloud->stats();
                loaded_tiles =
                    static_cast<std::uint32_t>(
                        ts.resident_tile_count
                    );
                gpu_buffer_bytes += ts.gpu_buffer_bytes;
                gpu_resident_points += ts.point_count;
            }
            if (full_gpu_cloud) {
                gpu_buffer_bytes +=
                    static_cast<std::uint64_t>(full_gpu_cloud->vertex_buffer_size());
                gpu_resident_points += full_gpu_cloud->point_count();
            }
            if (lod_gpu_cloud) {
                for (std::size_t i = 0; i < lod_gpu_cloud->level_count(); ++i) {
                    gpu_buffer_bytes += static_cast<std::uint64_t>(
                        lod_gpu_cloud->gpu_cloud(i).vertex_buffer_size()
                    );
                    gpu_resident_points +=
                        lod_gpu_cloud->level(i).gpu_point_count;
                }
            }

            // 视窗中实际可见的点数：瓦片模式下统计视锥体筛选后
            // 的瓦片点数和，非瓦片模式下使用 GPU 驻留点数。
            std::uint64_t visible_points = gpu_resident_points;
            if (tile_result.enabled && tile_reader.has_value()) {
                visible_points = 0;
                for (const auto tile_id : tile_result.tile_ids) {
                    visible_points +=
                        tile_reader->record(tile_id).point_count;
                }
            }

            app_state.dataset.point_count = dataset.point_count();
            app_state.dataset.loaded_points = gpu_resident_points;
            const auto tile_cache_stats = tile_stream.point_cache.stats();
            const auto tile_cache_requests =
                tile_cache_stats.hits + tile_cache_stats.misses;
            const auto sync_render_settings_state =
                [&](gs3d::app::RenderSettingsState& settings,
                    int viewport_index) {
                    if (viewport_index < 0 ||
                        viewport_index >=
                            static_cast<int>(viewport_pushes.size())) {
                        viewport_index = 0;
                    }
                    const auto idx =
                        static_cast<std::size_t>(viewport_index);
                    const auto& view_push = viewport_pushes[idx];
                    const auto& view_scene = viewport_scene_states[idx];
                    settings.point_size = view_push.point_size;
                    settings.color_attr_index =
                        view_scene.active_attribute_index;
                    settings.height_attr_index =
                        view_scene.active_height_index;
                    settings.height_exaggeration =
                        viewport_height_exags[idx];
                    settings.colormap_index = static_cast<int>(
                        (view_push.flags &
                         gs3d::render::PointFlags::kColormapMask) >> 1
                    );
                    settings.point_shape = static_cast<int>(
                        (view_push.flags &
                         gs3d::render::PointFlags::kPointShapeMask) >>
                            gs3d::render::PointFlags::kPointShapeShift
                    );
                    settings.value_clip_enabled =
                        (view_push.flags &
                         gs3d::render::PointFlags::kValueClip) != 0;

                    float display_min = view_push.color_min;
                    if (view_push.color_source ==
                        static_cast<std::uint32_t>(
                            gs3d::app::AttrPhysicalSource::Z)) {
                        display_min +=
                            static_cast<float>(dataset.origin_z());
                    }
                    settings.data_value_min = display_min;
                    settings.data_value_max =
                        display_min + view_push.color_range;
                    settings.loaded_tiles = loaded_tiles;
                    settings.pending_tiles = pending_tiles;
                    settings.cache_usage =
                        std::to_string(loaded_tiles) + " / " +
                        std::to_string(config_.tile_gpu_cache_max_tiles);
                    settings.cpu_cache_usage =
                        std::to_string(
                            tile_cache_stats.resident_bytes /
                            (1024ull * 1024ull)
                        ) + " / " +
                        std::to_string(
                            tile_cache_stats.max_bytes /
                            (1024ull * 1024ull)
                        ) + " MB";
                    // < 0 ⇒ 尚无任何缓存请求（预加载快路径或未流式），
                    // UI 显示为 "—" 而不是误导性的 0%。
                    settings.cache_hit_rate =
                        tile_cache_requests > 0
                            ? 100.0f * static_cast<float>(
                                tile_cache_stats.hits
                            ) /
                                static_cast<float>(tile_cache_requests)
                            : -1.0f;
                };
            const auto view_owned_by_workspace = [&](int viewport_index) {
                for (const auto& workspace : app_state.workspace_windows) {
                    if (!workspace.visible) {
                        continue;
                    }
                    if (std::find(
                            workspace.viewport_indices.begin(),
                            workspace.viewport_indices.end(),
                            viewport_index
                        ) != workspace.viewport_indices.end()) {
                        return true;
                    }
                }
                return false;
            };
            auto first_main_view = 0;
            for (const auto& view : app_state.render_views) {
                if (view.visible &&
                    !view_owned_by_workspace(view.viewport_index)) {
                    first_main_view = view.viewport_index;
                    break;
                }
            }
            for (std::size_t i = 0;
                 i < app_state.render_settings_by_view.size();
                 ++i) {
                sync_render_settings_state(
                    app_state.render_settings_by_view[i],
                    static_cast<int>(i)
                );
            }
            app_state.render_settings = render_settings_for_view(
                app_state,
                app_state.active_viewport_index
            );
            for (auto& workspace : app_state.workspace_windows) {
                if (!workspace.viewport_indices.empty()) {
                    const int workspace_view_index =
                        std::find(
                            workspace.viewport_indices.begin(),
                            workspace.viewport_indices.end(),
                            app_state.active_viewport_index
                        ) != workspace.viewport_indices.end()
                            ? app_state.active_viewport_index
                            : workspace.viewport_indices.front();
                    workspace.components.render_settings =
                        render_settings_for_view(
                            app_state,
                            workspace_view_index
                        );
                }
            }

            app_state.performance.fps = fps_smooth;
            app_state.performance.frame_time_ms =
                delta_seconds > 0.0 ? static_cast<float>(delta_seconds * 1000.0) : 0.0f;
            app_state.performance.visible_points = visible_points;
            app_state.performance.total_points = dataset.point_count();
            app_state.performance.loaded_tiles = loaded_tiles;
            app_state.performance.pending_tiles = pending_tiles;
            app_state.performance.gpu_memory_bytes = gpu_buffer_bytes;
            app_state.performance.lod_mode =
                config_.lod_enabled
                    ? "已启用细节层级"
                    : "全分辨率";

            app_state.status_bar.fps = fps_smooth;
            app_state.status_bar.visible_points = visible_points;
            app_state.status_bar.loaded_tiles = loaded_tiles;
            app_state.status_bar.pending_tiles = pending_tiles;
            app_state.status_bar.gpu_memory_bytes = gpu_buffer_bytes;
            app_state.status_bar.camera_position = format_vec3_text(primary_camera.position());
            app_state.status_bar.crs = "本地坐标 / 未知";
            app_state.status_bar.ready_state = "就绪";

            {
                ViewerAppRenderViewContext render_ctx{
                    viewport_manager,
                    dataset,
                    bounds,
                    viewport_pushes,
                    primary_value_name,
                    z_field_name,
                    visible_points,
                    n_viewports
                };
                fill_render_views(app_state, render_ctx, pick, selected_focus_points);
            }

            for (std::size_t i = 0; i < app_state.navigation_maps.size(); ++i) {
                update_navigation_map_view_rect(
                    app_state.navigation_maps[i],
                    app_state.render_views,
                    static_cast<int>(i)
                );
            }
            app_state.navigation_map = navigation_map_for_view(
                app_state,
                app_state.active_viewport_index
            );
            app_state.measurement = measurement_for_view(
                app_state,
                app_state.active_viewport_index
            );
            app_state.region_stats = region_stats_for_view(
                app_state,
                app_state.active_viewport_index
            );

            auto gui_cmds = imgui_layer.new_frame(app_state);
            for (const auto& view : app_state.render_views) {
                if (view.viewport_index < 0 ||
                    view.viewport_index >=
                        static_cast<int>(previous_view_visible.size()) ||
                    !view.visible ||
                    previous_view_visible[
                        static_cast<std::size_t>(view.viewport_index)
                    ]) {
                    continue;
                }

                int source_view = first_main_view;
                for (const auto& workspace : app_state.workspace_windows) {
                    if (std::find(
                            workspace.viewport_indices.begin(),
                            workspace.viewport_indices.end(),
                            view.viewport_index
                        ) == workspace.viewport_indices.end()) {
                        continue;
                    }
                    for (const int workspace_view :
                         workspace.viewport_indices) {
                        if (workspace_view != view.viewport_index &&
                            workspace_view >= 0 &&
                            workspace_view <
                                static_cast<int>(
                                    previous_view_visible.size()
                                ) &&
                            previous_view_visible[
                                static_cast<std::size_t>(workspace_view)
                            ]) {
                            source_view = workspace_view;
                            break;
                        }
                    }
                    break;
                }

                if (source_view >= 0 &&
                    source_view <
                        static_cast<int>(viewport_pushes.size())) {
                    const auto src =
                        static_cast<std::size_t>(source_view);
                    const auto dst =
                        static_cast<std::size_t>(view.viewport_index);
                    viewport_pushes[dst] = viewport_pushes[src];
                    viewport_scene_states[dst] =
                        viewport_scene_states[src];
                    viewport_height_exags[dst] =
                        viewport_height_exags[src];
                    if (dst < app_state.render_settings_by_view.size() &&
                        src < app_state.render_settings_by_view.size()) {
                        app_state.render_settings_by_view[dst] =
                            app_state.render_settings_by_view[src];
                    }
                    if (dst < app_state.navigation_maps.size() &&
                        src < app_state.navigation_maps.size()) {
                        app_state.navigation_maps[dst].dirty = true;
                        app_state.navigation_maps[dst].view_rect_valid = false;
                    }
                    if (dst < app_state.measurements.size() &&
                        src < app_state.measurements.size()) {
                        app_state.measurements[dst] =
                            app_state.measurements[src];
                    }
                    if (dst < app_state.region_stats_by_view.size() &&
                        src < app_state.region_stats_by_view.size()) {
                        app_state.region_stats_by_view[dst] =
                            app_state.region_stats_by_view[src];
                    }
                }
            }
            int runtime_active_count = 1;
            for (const auto& view : app_state.render_views) {
                if (view.visible) {
                    runtime_active_count = std::max(
                        runtime_active_count,
                        view.viewport_index + 1
                    );
                }
            }
            runtime_active_count = std::clamp(
                runtime_active_count,
                1,
                viewport_manager.viewport_count()
            );
            viewport_manager.set_active_count(runtime_active_count);
            for (const auto& view : app_state.render_views) {
                if (view.viewport_index >= 0 &&
                    view.viewport_index <
                        static_cast<int>(previous_view_visible.size())) {
                    previous_view_visible[
                        static_cast<std::size_t>(view.viewport_index)
                    ] = view.visible;
                }
            }
            if (streaming_viewport_index < 0 ||
                streaming_viewport_index >=
                    static_cast<int>(app_state.render_views.size()) ||
                !app_state
                     .render_views[
                         static_cast<std::size_t>(streaming_viewport_index)
                     ]
                     .visible) {
                const auto first_visible = std::find_if(
                    app_state.render_views.begin(),
                    app_state.render_views.end(),
                    [](const auto& view) {
                        return view.visible;
                    }
                );
                streaming_viewport_index =
                    first_visible != app_state.render_views.end()
                        ? first_visible->viewport_index
                        : 0;
                tile_selection_dirty = true;
            }
            apply_project_open_commands(gui_cmds, window);
            const double now_seconds =
                std::chrono::duration<double>(
                    current_time.time_since_epoch()
                ).count();
            if (benchmark_pick_enabled) {
                // Benchmark queries are authored against the requested
                // benchmark viewport size, not whatever persisted ImGui
                // layout happened to leave in the current framebuffer.
                // Force the scripted viewport frame to that target size so
                // the offscreen framebuffer, camera projection, and query
                // coordinates stay in the same space.
                const std::uint32_t benchmark_viewport_width =
                    std::max(config_.window_width, 1u);
                const std::uint32_t benchmark_viewport_height =
                    std::max(config_.window_height, 1u);
                const bool query_active =
                    benchmark_frame_index >= kBenchmarkPickWarmupFrames &&
                    benchmark_pick_issue_index <
                        benchmark_pick_queries.size();
                const auto& query =
                    query_active
                        ? benchmark_pick_queries[benchmark_pick_issue_index]
                        : BenchmarkPickScriptQuery{};
                bool frame_found = false;
                for (auto& frame : gui_cmds.viewport_frames) {
                    if (frame.index != 0) {
                        continue;
                    }
                    frame_found = true;
                    frame.hovered = query_active;
                    frame.active = false;
                    frame.width = benchmark_viewport_width;
                    frame.height = benchmark_viewport_height;
                    frame.mouse_local_x = query.mouse_x;
                    frame.mouse_local_y = query.mouse_y;
                    frame.mouse_on_image = query_active;
                    frame.rotate = false;
                    frame.pan = false;
                    frame.mouse_delta_x = 0.0f;
                    frame.mouse_delta_y = 0.0f;
                    frame.mouse_wheel = 0.0f;
                    frame.box_select_completed = false;
                    break;
                }
                if (!frame_found) {
                    gui_cmds.viewport_frames.push_back({
                        .index = 0,
                        .hovered = query_active,
                        .active = false,
                        .width = benchmark_viewport_width,
                        .height = benchmark_viewport_height,
                        .mouse_delta_x = 0.0f,
                        .mouse_delta_y = 0.0f,
                        .mouse_wheel = 0.0f,
                        .mouse_local_x = query.mouse_x,
                        .mouse_local_y = query.mouse_y,
                        .mouse_on_image = query_active,
                        .rotate = false,
                        .pan = false,
                        .box_select_completed = false
                    });
                }
            }
            observe_viewport_resize_requests(
                gui_cmds, viewport_resize_scheduler, now_seconds);

            build_visible_viewports(visible_viewports, app_state.render_views);

            const bool imgui_wants_keyboard =
                ImGui::GetIO().WantCaptureKeyboard;
            const bool keyboard_shortcuts_allowed =
                !ImGui::GetIO().WantTextInput;
            gs3d::util::Stopwatch benchmark_camera_timer;

            {
                ViewerAppCameraCommandContext cam_ctx{
                    .n_viewports = n_viewports,
                    .controllers = controllers,
                    .viewport_manager = viewport_manager,
                    .camera_hub = camera_hub,
                    .bounds = bounds,
                    .streaming_viewport_index = streaming_viewport_index,
                    .tile_selection_dirty = tile_selection_dirty
                };
                apply_reset_camera_command(gui_cmds, cam_ctx);
            }
            {
                const auto main_targets = [&]() {
                    std::vector<int> targets;
                    targets.push_back(app_state.active_viewport_index);
                    return targets;
                };
                for (const auto& command :
                     gui_cmds.render_settings_commands) {
                    const auto targets =
                        !command.has_viewport_scope
                        ? main_targets()
                        : command.viewport_indices;
                    for (const int viewport_index : targets) {
                        if (viewport_index < 0 ||
                            viewport_index >=
                                static_cast<int>(viewport_pushes.size())) {
                            continue;
                        }
                        const auto idx =
                            static_cast<std::size_t>(viewport_index);
                        ViewerAppRenderSettingsContext render_ctx{
                            .push = viewport_pushes[idx],
                            .scene_state = viewport_scene_states[idx],
                            .navigation_map =
                                navigation_map_for_view(
                                    app_state,
                                    viewport_index
                                ),
                            .attr_list = attr_list,
                            .dataset = dataset,
                            .height_exag = viewport_height_exags[idx]
                        };
                        apply_render_setting_commands(command, render_ctx);
                    }
                }
            }
            if (gui_cmds.clear_cache_requested) {
                clear_tile_cpu_cache(tile_stream);
            }
            {
                ViewerAppScreenshotContext ss_ctx{
                    .app_state = app_state,
                    .swapchain = swapchain,
                    .screenshot_offset = screenshot_capture.offset,
                    .screenshot_extent = screenshot_capture.extent,
                    .screenshot_pending = screenshot_capture.pending
                };
                apply_screenshot_command(gui_cmds, ss_ctx);
            }

            {
                RegionStatsCommandContext rs_ctx{
                    .viewport_manager = viewport_manager,
                    .dataset = dataset,
                    .bounds = bounds,
                    .primary_value_name = primary_value_name,
                    .z_field_name = z_field_name
                };
                handle_region_stats_commands(gui_cmds, app_state, rs_ctx);
            }

            if (!imgui_wants_keyboard && window.key_pressed(GLFW_KEY_ESCAPE)) {
                window.request_close();
            }

            if (!imgui_wants_keyboard) {
                const auto active_render_index =
                    static_cast<std::size_t>(
                        std::clamp(
                            streaming_viewport_index,
                            0,
                            static_cast<int>(viewport_pushes.size()) - 1
                        )
                    );
                auto& active_push = viewport_pushes[active_render_index];
                if (window.key_pressed(GLFW_KEY_EQUAL) ||
                    window.key_pressed(GLFW_KEY_KP_ADD)) {
                    active_push.point_size = std::min(
                        active_push.point_size + 0.05f,
                        10.0f
                    );
                }

                if (window.key_pressed(GLFW_KEY_MINUS) ||
                    window.key_pressed(GLFW_KEY_KP_SUBTRACT)) {
                    active_push.point_size = std::max(
                        active_push.point_size - 0.05f,
                        1.0f
                    );
                }
            }

            const bool r_pressed =
                keyboard_shortcuts_allowed &&
                (window.key_pressed(GLFW_KEY_R) ||
                 ImGui::IsKeyPressed(ImGuiKey_R, false));

            if (r_pressed && !r_was_pressed) {
                controllers[
                    static_cast<std::size_t>(streaming_viewport_index)
                ].clear_orbit_pivot();
                initialize_camera_from_config(
                    viewport_manager.camera(streaming_viewport_index),
                    config_,
                    bounds
                );
                camera_hub.propagate(streaming_viewport_index);
                tile_selection_dirty = true;
            }

            r_was_pressed = r_pressed;

            const bool f_pressed =
                keyboard_shortcuts_allowed &&
                (window.key_pressed(GLFW_KEY_F) ||
                 ImGui::IsKeyPressed(ImGuiKey_F, false));

            if (f_pressed && !f_was_pressed) {
                const auto focus_index =
                    static_cast<std::size_t>(streaming_viewport_index);
                if (focus_index < selected_focus_points.size() &&
                    selected_focus_points[focus_index].has_value()) {
                    controllers[focus_index].focus_on(
                        viewport_manager.camera(
                            streaming_viewport_index
                        ),
                        *selected_focus_points[focus_index]
                    );
                    camera_hub.propagate(streaming_viewport_index);
                    tile_selection_dirty = true;
                    std::cout
                        << "[CAMERA] focused selected point in viewport "
                        << streaming_viewport_index << '\n';
                } else {
                    std::cout
                        << "[CAMERA] focus skipped: no selected point in viewport "
                        << streaming_viewport_index << '\n';
                }
            }

            f_was_pressed = f_pressed;

            // Tab: cycle color attribute; Shift+Tab: cycle height attribute
            // (zero GPU cost — push constant only)
            const bool tab_held =
                !imgui_wants_keyboard && window.key_pressed(GLFW_KEY_TAB);
            const bool shift_mod =
                window.key_pressed(GLFW_KEY_LEFT_SHIFT) ||
                window.key_pressed(GLFW_KEY_RIGHT_SHIFT);

            if (!tab_held) {
                tab_was_pressed = false;
                shift_tab_was_pressed = false;
            } else if (shift_mod && !shift_tab_was_pressed) {
                shift_tab_was_pressed = true;
                tab_was_pressed = true;
                const auto active_render_index =
                    static_cast<std::size_t>(
                        std::clamp(
                            streaming_viewport_index,
                            0,
                            static_cast<int>(viewport_pushes.size()) - 1
                        )
                    );
                auto& active_scene =
                    viewport_scene_states[active_render_index];
                auto& active_push = viewport_pushes[active_render_index];
                auto& active_height_exag =
                    viewport_height_exags[active_render_index];
                const auto n = static_cast<std::uint32_t>(attr_list.size());
                const std::uint32_t new_idx =
                    (static_cast<std::uint32_t>(
                        active_scene.active_height_index) + 1u) % n;
                active_scene.active_height_index = static_cast<int>(new_idx);
                apply_height_attr_to(
                    active_push,
                    attr_list[new_idx],
                    active_height_exag
                );
                std::cout << "[HEIGHT] switched to: " << attr_list[new_idx].name << '\n';
            } else if (!tab_was_pressed) {
                tab_was_pressed = true;
                const auto active_render_index =
                    static_cast<std::size_t>(
                        std::clamp(
                            streaming_viewport_index,
                            0,
                            static_cast<int>(viewport_pushes.size()) - 1
                        )
                    );
                auto& active_scene =
                    viewport_scene_states[active_render_index];
                auto& active_push = viewport_pushes[active_render_index];
                const auto n = static_cast<std::uint32_t>(attr_list.size());
                const std::uint32_t new_idx =
                    (static_cast<std::uint32_t>(
                        active_scene.active_attribute_index) + 1u) % n;
                active_scene.active_attribute_index = static_cast<int>(new_idx);
                const auto& a = attr_list[new_idx];
                active_push.color_source = static_cast<std::uint32_t>(a.source);
                active_push.color_min    = a.min_val;
                active_push.color_range  = a.range();
                if (active_push.color_range <= 0.0f) {
                    active_push.color_range = 1.0f;
                }
                navigation_map_for_view(
                    app_state,
                    static_cast<int>(active_render_index)
                ).dirty = true;
                std::cout << "[COLOR] switched to: " << a.name << '\n';
            }

            sync_camera_link_groups(app_state, camera_hub);

            bool interacting = false;
            bool camera_changed = false;
            for (const auto& frame : gui_cmds.viewport_frames) {
                if (frame.index < 0 ||
                    frame.index >= static_cast<int>(controllers.size())) {
                    continue;
                }

                if ((frame.hovered || frame.active) &&
                    streaming_viewport_index != frame.index) {
                    streaming_viewport_index = frame.index;
                    tile_selection_dirty = true;
                }

                gs3d::camera::CameraInput input;
                input.viewport_width = frame.width;
                input.viewport_height = frame.height;
                input.delta_x = frame.mouse_delta_x;
                input.delta_y = frame.mouse_delta_y;
                input.scroll_y = frame.mouse_wheel;
                input.mouse_x = frame.mouse_local_x;
                input.mouse_y = frame.mouse_local_y;
                input.mouse_position_valid = frame.mouse_on_image;
                input.rotate = frame.rotate;
                input.pan = frame.pan;

                viewport_interaction.apply_rotation_gate(frame, input);

                interacting = interacting || input.interacting();
                if (controllers[static_cast<std::size_t>(frame.index)]
                        .update(
                            viewport_manager.camera(frame.index),
                            input
                        )) {
                    camera_changed = true;
                    streaming_viewport_index = frame.index;
                    camera_hub.propagate(frame.index);
                }
            }

            if (config_.benchmark_mode &&
                !benchmark_pick_enabled &&
                benchmark_frame_index < benchmark_orbit_frames) {
                // Orbit + a slow zoom-in so the visible region actually
                // shrinks — a pure yaw orbit at a fixed distance can leave
                // the whole bbox in view the entire time, never forcing a
                // different tile selection, which would starve the reload-
                // latency measurement below.
                auto& bench_camera =
                    viewport_manager.camera(streaming_viewport_index);
                bench_camera.orbit(0.01f, 0.0f);
                bench_camera.zoom(0.999f);
                camera_hub.propagate(streaming_viewport_index);
                interacting = true;
                camera_changed = true;
            }

            benchmark_camera_update_ms_frame =
                benchmark_camera_timer.elapsed_milliseconds();

            if (camera_changed) {
                tile_selection_dirty = true;
            }

            gs3d::util::Stopwatch benchmark_lod_tile_timer;
            const auto flush_benchmark_lod_tile_stage =
                [&]() {
                    benchmark_lod_tile_select_ms_frame +=
                        benchmark_lod_tile_timer.elapsed_milliseconds();
                    benchmark_lod_tile_timer.reset();
                };

            /*
             * Debounce the interacting signal so rapid scroll zoom doesn't
             * cause frame-by-frame oscillation between true/false. Without
             * this, fast mouse-wheel scrolling produces frames where
             * scroll_y is 0 between discrete wheel events, briefly flipping
             * interacting to false.  That would:
             *   - toggle tiles on then off (tile_will_render)
             *   - toggle LOD clip bbox on then off
             *   - trigger tile selection update mid-scroll
             * …all of which cause visible flicker.
             *
             * The debounce keeps interacting=true for kInteractingDebounceSeconds
             * after the last real interacting frame, bridging the gaps between
             * discrete scroll events.
             */
            if (interacting) {
                interacting_debounce_until =
                    current_time +
                    std::chrono::milliseconds(
                        static_cast<long>(
                            kInteractingDebounceSeconds * 1000.0
                        )
                    );
            } else if (current_time < interacting_debounce_until) {
                interacting = true;
            }

            if (config_.lod_enabled) {
                lod_selector.update(
                    interacting,
                    delta_seconds
                );
            }

            if (config_.tile_enabled && tile_reader.has_value()) {
                auto tile_config =
                    make_tile_selection_config(config_);
                const auto stream_index =
                    static_cast<std::size_t>(
                        std::clamp(
                            streaming_viewport_index,
                            0,
                            static_cast<int>(viewport_pushes.size()) - 1
                        )
                    );
                const auto& stream_push = viewport_pushes[stream_index];
                tile_config.height_offset = stream_push.height_offset;
                tile_config.height_mult = stream_push.height_mult;
                tile_config.height_source = stream_push.height_source;
                tile_selection.set_config(tile_config);
            }

            {
                ViewerAppTileStreamFrameContext tile_ctx{
                    .context = context,
                    .renderer = renderer,
                    .viewport_manager = viewport_manager,
                    .tile_reader =
                        tile_reader.has_value() ? &*tile_reader : nullptr,
                    .tile_point_ids_by_tile = tile_point_ids_by_tile,
                    .tile_gpu_cloud = tile_gpu_cloud.get(),
                    .tile_selection = tile_selection,
                    .tile_result = tile_result,
                    .tile_index_view = tile_index_view,
                    .runtime_points_by_id = runtime_points_by_id,
                    .runtime_points_valid_by_id = runtime_points_valid_by_id,
                    .streaming_viewport_index = streaming_viewport_index,
                    .camera_changed = camera_changed,
                    .interacting = interacting,
                    .current_time = current_time,
                    .tile_selection_dirty = tile_selection_dirty,
                    .lod_tile_timer = benchmark_lod_tile_timer,
                    .lod_tile_select_ms_frame =
                        benchmark_lod_tile_select_ms_frame,
                    .cpu_cull_ms_frame = benchmark_cpu_cull_ms_frame,
                    .upload_record_ms_frame =
                        benchmark_upload_record_ms_frame,
                    .reload_seconds = benchmark_samples.reload_seconds
                };
                update_tile_streaming(tile_stream, tile_ctx);
            }
            // Select LOD level once per frame (not per-viewport) so all views
            // use the same level and the verbose log fires at most once.
            if (config_.lod_enabled && lod_gpu_cloud) {
                const auto level_count = lod_gpu_cloud->level_count();

                // --- Spatial: what level does the current zoom need? ---
                // 交互中冻结，停手后更新——与 tile 选择的 !interacting
                // 门控语义一致，避免缩放中 LOD 硬切导致抽稀跳变。
                const auto voxel_sizes = lod_gpu_cloud->voxel_sizes();
                float world_per_pixel = 0.0f;
                float spatial_ortho_h = 0.0f;
                {
                    const auto& cam =
                        viewport_manager.camera(streaming_viewport_index);
                    spatial_ortho_h = cam.ortho_height();
                    const float vp_h =
                        static_cast<float>(cam.viewport_height());
                    if (vp_h > 0.0f) {
                        world_per_pixel = spatial_ortho_h / vp_h;
                    }
                }

                std::size_t spatial_level;
                if (interacting) {
                    // 交互中冻结：首帧正常计算，后续用冻结值
                    if (frozen_spatial_level >= level_count) {
                        frozen_spatial_level =
                            gs3d::render::LodSelector::select_level_by_spacing(
                                world_per_pixel,
                                voxel_sizes,
                                last_spatial_level
                            );
                    }
                    spatial_level = frozen_spatial_level;
                } else {
                    // 停手后更新到当前 zoom 对应的目标层
                    spatial_level =
                        gs3d::render::LodSelector::select_level_by_spacing(
                            world_per_pixel,
                            voxel_sizes,
                            last_spatial_level
                        );
                    frozen_spatial_level = spatial_level;
                }
                last_spatial_level = spatial_level;

                // --- Temporal: existing time / frame-rate logic ---
                const auto temporal_level =
                    lod_selector.select_level(level_count);

                // --- Combine: coarser of the two constraints ---
                // spatial 定"当前缩放需要多精"，temporal 定"当前性能允许多精"
                // 取 max = 更粗的那个，既是空间底线也是性能保护
                const auto requested =
                    std::max(spatial_level, temporal_level);

                // 安全网：select_level 理论上永远返回有效值（所有 LOD
                // 级别都在启动时一次性上传到 GPU），但万一索引越界退回到
                // last_valid，绝不画空帧。
                std::size_t resolved_lod;
                if (requested < level_count) {
                    resolved_lod = requested;
                } else {
                    resolved_lod = last_valid_lod_level;
                    std::cerr << "[WARN] LOD level out of range: "
                              << requested << " >= " << level_count
                              << ", falling back to "
                              << last_valid_lod_level << '\n';
                }

                /*
                 * KeepStableHighQuality: frozen 是显示质量地板，交互中锁定，
                 * 空闲时向更精细方向更新。新增：空间缩放允许 coarsening
                 * —— 缩小后经 high_delay 延迟才降质，防止缩放刚停就闪跳。
                 */
                if (config_.interactive_display_mode ==
                        gs3d::app::InteractiveDisplayMode::AllowCoarseLOD) {
                    lod_level_for_frame = resolved_lod;
                } else {
                    if (!interacting) {
                        // 传统：temporal 改善时 frozen 跟踪到更精细
                        if (resolved_lod < frozen_display_lod) {
                            frozen_display_lod = resolved_lod;
                        }
                        // 新增：缩小后空间缩放允许 coarsening
                        // （仅在 idle ≥ high_delay 后，防松手瞬间跳粗）
                        if (lod_selector.idle_seconds() >=
                            config_.lod_high_delay_seconds) {
                            frozen_display_lod =
                                std::max(frozen_display_lod, spatial_level);
                        }
                    }
                    lod_level_for_frame =
                        std::min(resolved_lod, frozen_display_lod);
                }
                last_valid_lod_level = lod_level_for_frame;

                if (lod_level_for_frame != last_lod_level) {
                    if (config_.lod_verbose) {
                        const auto& level =
                            lod_gpu_cloud->level(lod_level_for_frame);
                        std::cout << "[LOD] active level = "
                                  << lod_level_for_frame
                                  << ", points = "
                                  << level.gpu_point_count
                                  << ", idle_seconds = "
                                  << lod_selector.idle_seconds()
                                  << ", ortho_h = "
                                  << world_per_pixel
                                  << " m/px"
                                  << '\n';
                    }

                    // 空间选层调试（GS3D_LOD_DEBUG=1）
                    if (const char* env =
                            std::getenv("GS3D_LOD_DEBUG")) {
                        if (env[0] == '1') {
                            std::fprintf(
                                stderr,
                                "[LODDBG] ortho_h=%.1f wpix=%.3f "
                                "spatial=%zu%s temporal=%zu → level=%zu "
                                "frozen=%zu %s\n",
                                static_cast<double>(spatial_ortho_h),
                                static_cast<double>(world_per_pixel),
                                spatial_level,
                                interacting ? "(frozen)" : "",
                                temporal_level,
                                lod_level_for_frame,
                                frozen_display_lod,
                                interacting ? "(interacting)" : "(idle)");
                        }
                    }

                    last_lod_level = lod_level_for_frame;
                }
            }

            flush_benchmark_lod_tile_stage();
            std::vector<float> viewport_point_sizes;
            viewport_point_sizes.reserve(viewport_pushes.size());
            for (const auto& view_push : viewport_pushes) {
                viewport_point_sizes.push_back(view_push.point_size);
            }
            prepare_gpu_pick_requests(
                pick,
                viewport_manager,
                app_state,
                gui_cmds,
                viewport_point_sizes,
                benchmark_pick_issue_index,
                benchmark_pick_queries
            );

            renderer.draw_frame(
                window,
                gs3d::render::VulkanRenderer::FrameDrawCallbacks{
                    .frame_ready = [&](std::uint32_t frame_slot) {
                        consume_ready_pick_frame_slot(frame_slot);
                    },
                    // pre_pass: all offscreen render passes execute here, before the
                    // swapchain render pass starts. Each viewport records its own
                    // vkCmdBeginRenderPass / draw / vkCmdEndRenderPass sequence into
                    // cmd; none of them nest inside each other or the swapchain pass.
                    .pre_pass = [&](VkCommandBuffer cmd) {
                        // ── 每视图导航图缩略图重渲（着色属性变更时触发）──
                        for (std::size_t i = 0;
                             i < app_state.navigation_maps.size() &&
                             i < nav_thumbnail_fbs.size();
                             ++i) {
                            auto& nav = app_state.navigation_maps[i];
                            if (!nav.dirty || !nav_thumbnail_fbs[i].valid()) {
                                continue;
                            }
                            const ViewerAppNavThumbnailContext nav_ctx{
                                point_pipeline,
                                nav_cloud,
                                i < viewport_pushes.size()
                                    ? viewport_pushes[i]
                                    : viewport_pushes.front(),
                                dataset.bbox_max_z()
                            };
                            record_navigation_thumbnail(
                                cmd,
                                nav_thumbnail_fbs[i],
                                nav,
                                nav_ctx
                            );
                        }

                        ViewerAppViewportDrawContext draw_ctx{
                            .visible_viewports = visible_viewports,
                            .viewport_manager = viewport_manager,
                            .point_pipeline = point_pipeline,
                            .viewport_pushes = viewport_pushes,
                            .lod_gpu_cloud = lod_gpu_cloud.get(),
                            .full_gpu_cloud = full_gpu_cloud.get(),
                            .tile_gpu_cloud = tile_gpu_cloud.get(),
                            .tile_stream = tile_stream,
                            .tile_result = tile_result,
                            .pick = pick,
                            .gpu_pick_readback = gpu_pick_readback,
                            .pick_debug_frame_dumper =
                                pick_debug_frame_dumper,
                            .pending_hover_miss_dump =
                                pending_hover_miss_dump,
                            .pick_debug_dump_count = pick_debug_dump_count,
                            .pick_debug_dump_completed =
                                pick_debug_dump_completed,
                            .lod_level_for_frame = lod_level_for_frame,
                            .interacting = interacting,
                            .benchmark_pick_enabled = benchmark_pick_enabled,
                            .app_frame_index = app_frame_index,
                            .benchmark_pick_issue_cpu_ms =
                                benchmark_pick_issue_cpu_ms,
                            .benchmark_pick_issue_metadata =
                                benchmark_pick_issue_metadata
                        };
                        record_viewport_passes(cmd, draw_ctx);
                    },
                    // in_pass: only ImGui runs in the swapchain render pass.
                    // Each ImGui::Image() samples its viewport's offscreen texture.
                    .in_pass = [&](VkCommandBuffer cmd) {
                        imgui_layer.render(cmd);
                    },
                    // post_pass: after the swapchain render pass ends, copy
                    // the viewport region to a staging buffer for screenshots.
                    .post_pass = [&](VkCommandBuffer cmd, std::uint32_t image_index) {
                        record_screenshot_copy(
                            cmd,
                            image_index,
                            context,
                            swapchain,
                            screenshot_capture
                        );
                    }
                }
            );
            const double benchmark_draw_record_ms_frame =
                renderer.last_draw_record_cpu_ms();
            const double benchmark_acquire_wait_ms_frame =
                renderer.last_acquire_wait_ms();
            const double benchmark_frame_fence_wait_ms_frame =
                renderer.last_frame_fence_wait_ms();
            const double benchmark_upload_fence_wait_ms_frame =
                renderer.last_upload_fence_wait_ms();
            benchmark_cpu_frame_ms =
                benchmark_camera_update_ms_frame +
                benchmark_lod_tile_select_ms_frame +
                benchmark_cpu_cull_ms_frame +
                benchmark_upload_record_ms_frame +
                benchmark_draw_record_ms_frame;
            // Render ImGui platform windows (docked panels torn out to separate
            // OS windows). Must happen outside the main render pass.
            imgui_layer.render_platform_windows();
            // If draw_frame() returned early (minimized / swapchain out-of-date)
            // the draw callback was never invoked, so close the dangling ImGui frame.
            imgui_layer.discard_frame();

            // ── Screenshot PNG write ──────────────────────────────────
            write_pending_screenshot(context, swapchain, screenshot_capture);

            // Rebuild framebuffer resources only after the user stops resizing.
            // All ready viewports share one device-idle synchronization point.
            const auto ready_resizes =
                viewport_resize_scheduler.take_ready(now_seconds);
            std::vector<gs3d::render::ViewportResizeRequest>
                resize_requests;
            resize_requests.reserve(ready_resizes.size());
            for (const auto& resize : ready_resizes) {
                resize_requests.push_back({
                    resize.index,
                    {resize.width, resize.height}
                });
                if (resize.index == streaming_viewport_index) {
                    tile_selection_dirty = true;
                }
            }
            viewport_manager.resize_many(resize_requests);

            if (config_.benchmark_mode) {
                benchmark_samples.wall_frame_times_ms.push_back(
                    benchmark_frame_timer.elapsed_milliseconds()
                );
                benchmark_samples.cpu_frame_times_ms.push_back(
                    benchmark_cpu_frame_ms
                );
                benchmark_samples.camera_update_ms.push_back(
                    benchmark_camera_update_ms_frame
                );
                benchmark_samples.lod_tile_select_ms.push_back(
                    benchmark_lod_tile_select_ms_frame
                );
                benchmark_samples.cpu_cull_ms.push_back(
                    benchmark_cpu_cull_ms_frame
                );
                benchmark_samples.upload_record_ms.push_back(
                    benchmark_upload_record_ms_frame
                );
                benchmark_samples.draw_record_ms.push_back(
                    benchmark_draw_record_ms_frame
                );
                benchmark_samples.acquire_wait_ms.push_back(
                    benchmark_acquire_wait_ms_frame
                );
                benchmark_samples.frame_fence_wait_ms.push_back(
                    benchmark_frame_fence_wait_ms_frame
                );
                benchmark_samples.upload_fence_wait_ms.push_back(
                    benchmark_upload_fence_wait_ms_frame
                );
                if (renderer.has_last_gpu_frame_ms()) {
                    benchmark_samples.gpu_frame_times_ms.push_back(
                        renderer.last_gpu_frame_ms()
                    );
                }
                ++benchmark_frame_index;
            }
            ++app_frame_index;
        }

        vkDeviceWaitIdle(context.device());

        if (config_.benchmark_mode) {
            print_benchmark_report(
                benchmark_samples,
                swapchain.present_mode()
            );
        }

        if (benchmark_pick_enabled &&
            !config_.benchmark_pick_result_path.empty()) {
            write_benchmark_pick_results(
                config_.benchmark_pick_result_path,
                benchmark_pick_results
            );
            std::cout << "[BENCH] pick_result_path = "
                      << config_.benchmark_pick_result_path.string()
                      << '\n';
            std::cout << "[BENCH] pick_result_count = "
                      << benchmark_pick_results.size()
                      << '\n';
        }

        std::cout << "[PASS] ViewerApp finished.\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}

} // namespace gs3d::app
