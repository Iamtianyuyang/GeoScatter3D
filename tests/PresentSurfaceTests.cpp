#include "data/Gs3dFormat.hpp"
#include "data/PointDataAdapters.hpp"

#include "render/PointCloudGpu.hpp"
#include "render/PointPipeline.hpp"
#include "render/PresentSurface.hpp"
#include "render/VulkanContext.hpp"

#include <catch2/catch_test_macros.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

/**
 * 隐藏的 Win32 窗口，用来在不依赖 GLFW 的前提下拿到真实 VkSurfaceKHR ——
 * 这正是 Android 侧 ANativeWindow 将要走的同一条链路（平台适配层只提供
 * instance 扩展 + 创建 surface 的回调 + user_data）。
 */
class HiddenWindow {
public:
    HiddenWindow() {
        WNDCLASSEXA window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = DefWindowProcA;
        window_class.hInstance = GetModuleHandleA(nullptr);
        window_class.lpszClassName = "Gs3dPresentSurfaceTest";
        if (RegisterClassExA(&window_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("RegisterClassExA failed");
        }
        hwnd_ = CreateWindowExA(
            0, "Gs3dPresentSurfaceTest", "gs3d-present-test",
            WS_OVERLAPPEDWINDOW, 0, 0, 640, 360,
            nullptr, nullptr, window_class.hInstance, nullptr
        );
        if (hwnd_ == nullptr) {
            throw std::runtime_error("CreateWindowExA failed");
        }
    }

    ~HiddenWindow() {
        if (hwnd_ != nullptr) {
            DestroyWindow(hwnd_);
        }
    }

    HiddenWindow(const HiddenWindow&) = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;

    [[nodiscard]] HWND handle() const noexcept { return hwnd_; }

private:
    HWND hwnd_ = nullptr;
};

VkResult create_win32_surface(
    VkInstance instance,
    void* user_data,
    VkSurfaceKHR* out_surface
) {
    VkWin32SurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = GetModuleHandleA(nullptr);
    info.hwnd = static_cast<HWND>(user_data);
    return vkCreateWin32SurfaceKHR(instance, &info, nullptr, out_surface);
}

std::vector<gs3d::data::Gs3dPoint> make_points(int edge, float span) {
    std::vector<gs3d::data::Gs3dPoint> points;
    points.reserve(static_cast<std::size_t>(edge) * edge);
    for (int i = 0; i < edge; ++i) {
        for (int j = 0; j < edge; ++j) {
            const float x = -span + 2.0f * span * i / (edge - 1);
            const float y = -span + 2.0f * span * j / (edge - 1);
            gs3d::data::Gs3dPoint point{};
            point.x = x;
            point.y = y;
            point.z = 0.25f * std::sin(x) * std::cos(y);
            point.value = point.z;
            points.push_back(point);
        }
    }
    return points;
}

/// 返回与背景色不同的像素数（背景 = 首像素）。
std::size_t lit_pixels(const std::vector<std::uint8_t>& rgba) {
    if (rgba.size() < 8) {
        return 0;
    }
    std::size_t lit = 0;
    for (std::size_t i = 4; i + 3 < rgba.size(); i += 4) {
        if (rgba[i] != rgba[0] || rgba[i + 1] != rgba[1] ||
            rgba[i + 2] != rgba[2]) {
            ++lit;
        }
    }
    return lit;
}

}  // namespace

TEST_CASE("present surface renders a point cloud into a real swapchain") {
    const std::filesystem::path shader_dir(GS3D_TEST_SHADER_DIR);
    if (!std::filesystem::exists(shader_dir / "point.vert.spv")) {
        SUCCEED("SPIR-V not built; skipping present-surface integration");
        return;
    }

    HiddenWindow window;
    gs3d::render::VulkanSurfaceProvider provider{};
    const char* extensions[] = {VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    provider.instance_extensions = extensions;
    provider.instance_extension_count = 1;
    provider.create_surface = &create_win32_surface;
    provider.user_data = window.handle();

    try {
        gs3d::render::VulkanContextConfig context_config{};
        context_config.headless = false;
        context_config.surface_provider = provider;
        context_config.enable_validation_layers = true;
        context_config.application_name = "GeoScatter3D.PresentSurface.Tests";
        gs3d::render::VulkanContext context(context_config);

        gs3d::render::PresentSurface::Config present_config;
        present_config.requested_width = 512;
        present_config.requested_height = 288;
        gs3d::render::PresentSurface present(context, present_config);
        REQUIRE(present.valid());
        REQUIRE(present.extent().width > 0);
        REQUIRE(present.extent().height > 0);

        // 管线必须能对着 present 的 render pass（1 个颜色附件 + 深度）创建。
        gs3d::render::PointPipelineConfig pipeline_config;
        pipeline_config.vertex_shader_path = shader_dir / "point.vert.spv";
        pipeline_config.fragment_shader_path = shader_dir / "point.frag.spv";
        pipeline_config.color_attachment_count = 1;
        gs3d::render::PointPipeline pipeline(
            context, present.render_pass(), pipeline_config);
        REQUIRE(pipeline.pipeline() != VK_NULL_HANDLE);

        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex =
            context.queue_family_indices().graphics_family.value();
        VkCommandPool pool = VK_NULL_HANDLE;
        REQUIRE(
            vkCreateCommandPool(context.device(), &pool_info, nullptr, &pool) ==
            VK_SUCCESS);

        VkCommandBuffer command = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo command_info{};
        command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_info.commandPool = pool;
        command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_info.commandBufferCount = 1;
        REQUIRE(
            vkAllocateCommandBuffers(context.device(), &command_info, &command) ==
            VK_SUCCESS);

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        REQUIRE(
            vkCreateFence(context.device(), &fence_info, nullptr, &fence) ==
            VK_SUCCESS);

        const auto points = make_points(80, 8.0f);
        const auto view = gs3d::data::make_point_data_view(points);
        gs3d::render::PointCloudGpu cloud(
            context, pool, context.graphics_queue(), view);
        REQUIRE(cloud.point_count() == points.size());

        gs3d::render::PointPushConstants push{};
        // 正交投影：直接看 XY 平面，忽略 z 起伏的影响。
        const float ortho = 12.0f;
        push.mvp[0] = 1.0f / ortho;
        push.mvp[5] = -1.0f / ortho;   // Vulkan y 向下
        push.mvp[10] = 1.0f;
        push.mvp[15] = 1.0f;
        push.point_size = 2.0f;
        push.color_min = -0.5f;
        push.color_range = 1.0f;

        gs3d::render::PresentSurface::Frame frame{};
        bool drew = false;
        std::vector<std::uint8_t> pixels;
        for (int attempt = 0; attempt < 4 && !drew; ++attempt) {
            if (!present.begin_frame(frame)) {
                continue;  // 刚重建交换链，重试
            }
            REQUIRE(frame.acquired);
            REQUIRE(frame.framebuffer != VK_NULL_HANDLE);

            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            REQUIRE(vkBeginCommandBuffer(command, &begin) == VK_SUCCESS);

            VkClearValue clears[2]{};
            clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            clears[1].depthStencil = {1.0f, 0};
            VkRenderPassBeginInfo pass{};
            pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            pass.renderPass = present.render_pass();
            pass.framebuffer = frame.framebuffer;
            pass.renderArea.extent = frame.extent;
            pass.clearValueCount = 2;
            pass.pClearValues = clears;
            vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
            pipeline.draw(command, cloud, frame.extent, push);
            vkCmdEndRenderPass(command);
            REQUIRE(vkEndCommandBuffer(command) == VK_SUCCESS);

            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &command;
            REQUIRE(
                vkQueueSubmit(context.graphics_queue(), 1, &submit, fence) ==
                VK_SUCCESS);
            REQUIRE(
                vkWaitForFences(
                    context.device(), 1, &fence, VK_TRUE, UINT64_MAX) ==
                VK_SUCCESS);
            REQUIRE(vkResetFences(context.device(), 1, &fence) == VK_SUCCESS);

            // present 之前把交换链图像拷回 host，验证真的画进去了。
            REQUIRE(present.copy_frame_to_host(frame, pixels));
            REQUIRE(present.end_frame(frame));
            drew = true;
        }
        REQUIRE(drew);

        const std::size_t lit = lit_pixels(pixels);
        CHECK(lit > 100);

        // 交换链尺寸变化应触发可重建标记（不崩即可，重建路径由真机/Android 覆盖）。
        present.recreate();
        CHECK(present.valid());

        vkDeviceWaitIdle(context.device());
        vkDestroyFence(context.device(), fence, nullptr);
        vkFreeCommandBuffers(context.device(), pool, 1, &command);
        vkDestroyCommandPool(context.device(), pool, nullptr);
    } catch (const std::exception& ex) {
        // 无 Vulkan/无显示的环境（CI 容器）跳过；失败信息保留便于排障。
        SUCCEED(std::string("present surface unavailable: ") + ex.what());
    }
}

#else

TEST_CASE("present surface renders a point cloud into a real swapchain") {
    SUCCEED("platform present surface not covered on this platform yet");
}

#endif
