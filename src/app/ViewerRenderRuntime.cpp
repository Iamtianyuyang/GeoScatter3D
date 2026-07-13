#include "app/ViewerRenderRuntime.hpp"

#include "app/ViewerApp.hpp"
#include "app/ViewerAppInternal.hpp"
#include "app/ViewerRuntimeConfiguration.hpp"
#include "app/ViewerWorkbenchLayout.hpp"
#include "data/Gs3dDataset.hpp"
#include "data/Gs3dFormat.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "data/PointDataAdapters.hpp"
#include "gui/UiFonts.hpp"
#include "platform/Window.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/PointCloudLodGpu.hpp"
#include "render/PointCloudTileGpu.hpp"
#include "render/PointPipeline.hpp"
#include "render/ViewportManager.hpp"
#include "ui/SvgLogoTexture.hpp"
#include "util/Log.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace gs3d::app {

namespace {

gs3d::camera::CameraBounds make_camera_bounds(
    const gs3d::data::Gs3dDataset& dataset
) {
    return {
        .min = {
            dataset.bbox_min_x(),
            dataset.bbox_min_y(),
            dataset.bbox_min_z(),
        },
        .max = {
            dataset.bbox_max_x(),
            dataset.bbox_max_y(),
            dataset.bbox_max_z(),
        },
    };
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

void apply_workbench_layout(gs3d::platform::Window& window)
{
    std::optional<DesktopWorkArea> primary_work_area;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor(); monitor != nullptr) {
        DesktopWorkArea work_area;
        glfwGetMonitorWorkarea(
            monitor,
            &work_area.x,
            &work_area.y,
            &work_area.width,
            &work_area.height
        );
        primary_work_area = work_area;
    }
    WindowFrameInsets frame_insets;
    glfwGetWindowFrameSize(
        window.native_handle(),
        &frame_insets.left,
        &frame_insets.top,
        &frame_insets.right,
        &frame_insets.bottom
    );
    const auto layout = compute_workbench_window_layout(
        gs3d::gui::ui_fonts().ui_scale,
        primary_work_area,
        frame_insets
    );
    glfwSetWindowSize(
        window.native_handle(),
        layout.client_width,
        layout.client_height
    );
    if (layout.outer_x.has_value() && layout.outer_y.has_value()) {
        glfwSetWindowPos(window.native_handle(), *layout.outer_x, *layout.outer_y);
    }
}

} // namespace

ViewerRenderRuntime::ViewerRenderRuntime(
    gs3d::platform::Window& window,
    const ViewerRenderRuntimeInput& input
)
    : window_(window)
    , context_(window_, make_vulkan_context_config(input.graphics_config))
    , swapchain_(
        context_,
        window_,
        benchmark_present_mode_hint(input.benchmark_config.present_mode)
    )
    , renderer_(context_, swapchain_)
{
    if (input.on_context_created) {
        input.on_context_created();
    }
    gs3d::util::log::info() << "[OK] VulkanContext created.\n";
    gs3d::util::log::info() << "Physical device: "
              << context_.physical_device_name() << '\n';

    imgui_layer_.init(
        window_.native_handle(),
        context_,
        renderer_,
        swapchain_.image_count(),
        input.window_config.ui_layout_ini_path,
        input.window_config.ui_scale_multiplier,
        input.window_config.enable_multi_viewports
    );
    apply_workbench_layout(window_);

    const auto clear_color = make_clear_color(input.graphics_config);
    renderer_.set_clear_color(clear_color);
    logo_texture_ = std::make_unique<gs3d::ui::SvgLogoTexture>(
        context_.device(),
        context_.physical_device(),
        context_.graphics_queue(),
        renderer_.command_pool(),
        "assets/icon.svg",
        128
    );
    gs3d::util::log::info() << "[OK] SvgLogoTexture loaded.\n";

    if (input.lod_config.enabled) {
        lod_gpu_cloud_ = std::make_unique<gs3d::render::PointCloudLodGpu>(
            context_,
            renderer_.command_pool(),
            context_.graphics_queue(),
            build_lod_source(input.lod_dataset, input.lod_point_ids)
        );
        gs3d::util::log::info() << "[OK] PointCloudLodGpu uploaded.\n";
        gs3d::util::log::info() << lod_gpu_cloud_->summary();
    } else {
        full_gpu_cloud_ = std::make_unique<gs3d::render::PointCloudGpu>(
            context_,
            renderer_.command_pool(),
            context_.graphics_queue(),
            gs3d::data::make_point_data_view(
                input.dataset,
                input.full_point_ids.data()
            )
        );
        gs3d::util::log::info() << "[OK] PointCloudGpu uploaded.\n";
        gs3d::util::log::info() << "gpu point_count = "
                  << full_gpu_cloud_->point_count() << '\n';
    }

    bounds_ = make_camera_bounds(input.dataset);
    gs3d::camera::Camera initial_camera;
    const VkExtent2D initial_viewport_extent = swapchain_.extent();
    initial_camera.set_viewport(
        initial_viewport_extent.width,
        initial_viewport_extent.height
    );
    initialize_camera_from_config(initial_camera, input.camera_config, bounds_);

    viewport_manager_ = std::make_unique<gs3d::render::ViewportManager>();
    viewport_manager_->init(
        context_,
        swapchain_.image_format(),
        kMaxViewportCount,
        initial_viewport_extent,
        initial_camera
    );
    viewport_manager_->set_active_count(
        std::clamp(input.window_config.viewport_count, 1, kMaxViewportCount)
    );
    viewport_manager_->set_clear_color(clear_color);

    point_pipeline_ = std::make_unique<gs3d::render::PointPipeline>(
        context_,
        viewport_manager_->render_pass(),
        make_point_pipeline_config(input.graphics_config)
    );
    gs3d::util::log::info() << "[OK] PointPipeline created.\n";

    if (input.tile_config.enabled && input.tile_reader_available) {
        tile_gpu_cloud_ = std::make_unique<gs3d::render::PointCloudTileGpu>();
        tile_gpu_cloud_->set_resident_tile_budget(
            input.tile_config.gpu_cache_max_tiles
        );
        gs3d::util::log::info() << "[OK] Tile GPU cache initialized.\n";
    }
}

ViewerRenderRuntime::~ViewerRenderRuntime() = default;

gs3d::render::VulkanContext& ViewerRenderRuntime::context() noexcept
{
    return context_;
}

gs3d::render::VulkanSwapchain& ViewerRenderRuntime::swapchain() noexcept
{
    return swapchain_;
}

gs3d::render::VulkanRenderer& ViewerRenderRuntime::renderer() noexcept
{
    return renderer_;
}

gs3d::gui::ImGuiLayer& ViewerRenderRuntime::imgui_layer() noexcept
{
    return imgui_layer_;
}

gs3d::render::ViewportManager& ViewerRenderRuntime::viewport_manager() noexcept
{
    return *viewport_manager_;
}

gs3d::render::PointPipeline& ViewerRenderRuntime::point_pipeline() noexcept
{
    return *point_pipeline_;
}

const gs3d::camera::CameraBounds& ViewerRenderRuntime::bounds() const noexcept
{
    return bounds_;
}

const gs3d::ui::SvgLogoTexture& ViewerRenderRuntime::logo_texture() const noexcept
{
    return *logo_texture_;
}

gs3d::render::PointCloudGpu* ViewerRenderRuntime::full_gpu_cloud() noexcept
{
    return full_gpu_cloud_.get();
}

gs3d::render::PointCloudLodGpu* ViewerRenderRuntime::lod_gpu_cloud() noexcept
{
    return lod_gpu_cloud_.get();
}

gs3d::render::PointCloudTileGpu* ViewerRenderRuntime::tile_gpu_cloud() noexcept
{
    return tile_gpu_cloud_.get();
}

const gs3d::render::PointCloudGpu& ViewerRenderRuntime::navigation_cloud() const
{
    return lod_gpu_cloud_
        ? lod_gpu_cloud_->lowest_detail().gpu_cloud
        : *full_gpu_cloud_;
}

} // namespace gs3d::app
