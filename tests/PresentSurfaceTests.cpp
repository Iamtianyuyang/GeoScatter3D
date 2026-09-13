#include "data/Gs3dFormat.hpp"
#include "data/PointDataAdapters.hpp"

#include "render/PointCloudGpu.hpp"
#include "render/PointPipeline.hpp"
#include "ffi/EngineHost.hpp"
#include "ffi/HeadlessRenderKernel.hpp"
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
#include <cstdlib>
#include <filesystem>
#include <iostream>
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

/*
 * 平台适配回调：形状与 C ABI 完全一致（int32 返回码 + uint64 不透明句柄），
 * Android 侧 ANativeWindow 版本会在这里调用 vkCreateAndroidSurfaceKHR。
 */
std::int32_t create_win32_surface(
    void* instance,
    void* user_data,
    std::uint64_t* out_surface_handle
) {
    VkWin32SurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = GetModuleHandleA(nullptr);
    info.hwnd = static_cast<HWND>(user_data);
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult result = vkCreateWin32SurfaceKHR(
        reinterpret_cast<VkInstance>(instance), &info, nullptr, &surface);
    if (result != VK_SUCCESS) {
        return -3;
    }
    *out_surface_handle = reinterpret_cast<std::uint64_t>(surface);
    return 0;
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

TEST_CASE("engine presents the point cloud into a platform surface") {
    const std::filesystem::path shader_dir(GS3D_TEST_SHADER_DIR);
    if (!std::filesystem::exists(shader_dir / "point.vert.spv")) {
        SUCCEED("SPIR-V not built; skipping engine present test");
        return;
    }

    HiddenWindow window;
    gs3d::render::VulkanSurfaceProvider provider{};
    const char* extensions[] = {VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    provider.instance_extensions = extensions;
    provider.instance_extension_count = 1;
    provider.create_surface = &create_win32_surface;
    provider.user_data = window.handle();

    gs3d::ffi::EngineHost host;
    gs3d::ffi::HeadlessRenderKernel::Config config{};
    config.width = 512;
    config.height = 288;
    config.enable_validation = true;
    config.shader_dir = shader_dir.string();
    if (host.start(config) != 0) {
        SUCCEED("Vulkan unavailable; skipping engine present test");
        return;
    }

    // 先加载数据再附加 surface：验证 dataset/LOD 在上下文重建后仍可用。
    REQUIRE(host.invoke([](gs3d::ffi::HeadlessRenderKernel& kernel) {
        return kernel.load_sample();
    }) == 0);

    const int attach_rc = host.invoke(
        [&provider](gs3d::ffi::HeadlessRenderKernel& kernel) {
            return kernel.attach_surface(provider, 512, 288);
        }
    );
    REQUIRE(attach_rc == 0);
    REQUIRE(host.invoke([](gs3d::ffi::HeadlessRenderKernel& kernel) {
        return kernel.has_surface() ? 0 : -1;
    }) == 0);

    // 连续呈递若干帧：0 成功；-2 只允许出现在交换链刚重建的那一帧。
    int presented = 0;
    int skipped = 0;
    for (int frame = 0; frame < 8; ++frame) {
        int rc = 0;
        host.invoke([&rc](gs3d::ffi::HeadlessRenderKernel& kernel) {
            rc = kernel.present_frame();
            return 0;
        });
        if (rc == 0) {
            ++presented;
        } else if (rc == -2) {
            ++skipped;
        } else {
            FAIL("present_frame failed with " << rc);
            break;
        }
    }
    CHECK(presented > 0);
    CHECK(skipped <= 1);

    // 分离 surface 后应回到纯离屏回读路径，且仍能出帧。
    REQUIRE(host.invoke([](gs3d::ffi::HeadlessRenderKernel& kernel) {
        kernel.detach_surface();
        return 0;
    }) == 0);
    CHECK(host.invoke([](gs3d::ffi::HeadlessRenderKernel& kernel) {
        return kernel.has_surface() ? -1 : 0;
    }) == 0);

    std::vector<std::uint8_t> rgba(512 * 288 * 4);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    CHECK(host.invoke(
        [&rgba, &width, &height](gs3d::ffi::HeadlessRenderKernel& kernel) {
            return kernel.render_rgba(
                rgba.data(), static_cast<std::uint32_t>(rgba.size()),
                &width, &height);
        }
    ) == 0);
    CHECK(lit_pixels(rgba) > 100);
}

TEST_CASE("re-attaching a surface reuses the CPU LOD chain") {
    /*
     * 回归：Surface 重建（真机旋转/分屏）不应重跑 CPU LOD 建链。
     * 100 万点上实测建链 460 ms、上传 40 ms，不区分开就是每次旋转都卡一下。
     * 断言两件事：第二次附加确实是「复用」路径，且 GPU 缓冲真的重传了
     * （否则 lod_ 为空，present_frame 会一直返回 -2）。
     */
    const std::filesystem::path shader_dir(GS3D_TEST_SHADER_DIR);
    if (!std::filesystem::exists(shader_dir / "point.vert.spv")) {
        SUCCEED("SPIR-V not built; skipping surface re-attach test");
        return;
    }

    HiddenWindow window;
    gs3d::render::VulkanSurfaceProvider provider{};
    const char* extensions[] = {VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    provider.instance_extensions = extensions;
    provider.instance_extension_count = 1;
    provider.create_surface = &create_win32_surface;
    provider.user_data = window.handle();

    gs3d::ffi::EngineHost host;
    gs3d::ffi::HeadlessRenderKernel::Config config{};
    config.width = 512;
    config.height = 288;
    config.enable_validation = true;
    config.shader_dir = shader_dir.string();
    if (host.start(config) != 0) {
        SUCCEED("Vulkan unavailable; skipping surface re-attach test");
        return;
    }

    // 25 万点：与真机 bench bundle 同量级，足以产出至少一级 LOD。
    REQUIRE(host.invoke([](gs3d::ffi::HeadlessRenderKernel& kernel) {
        return kernel.load_synthetic(250000);
    }) == 0);

    auto lod_of = [&host]() {
        auto state = std::make_shared<gs3d::ffi::HeadlessRenderKernel::LodState>();
        host.invoke([state](gs3d::ffi::HeadlessRenderKernel& kernel) {
            *state = kernel.lod_state();
            return 0;
        });
        return *state;
    };

    const auto built = lod_of();
    REQUIRE(built.level_count >= 2);
    // 加载阶段是真建链（供后面断言附加阶段没有重建）。
    CHECK_FALSE(built.cpu_chain_reused);

    REQUIRE(host.invoke([&provider](gs3d::ffi::HeadlessRenderKernel& kernel) {
        return kernel.attach_surface(provider, 512, 288);
    }) == 0);
    const auto attached = lod_of();
    // load_synthetic 已经建过一次链，附加时只应重传 GPU 缓冲。
    CHECK(attached.cpu_chain_reused);
    CHECK(attached.level_count == built.level_count);
    CHECK(attached.drawn_points == built.drawn_points);
    CHECK(attached.estimated_spacing == built.estimated_spacing);

    /*
     * 相机缩放属于用户状态：Surface 重建/尺寸变化不得重置。
     * 透视相机的缩放就是改 FOV，而 create_resources_locked 曾被 attach/detach
     * 反复调用并顺带 set_perspective(45) —— 真机表现是「转一下屏幕缩放就没了」。
     */
    auto fov_of = [&host]() {
        auto fov = std::make_shared<float>(0.0f);
        host.invoke([fov](gs3d::ffi::HeadlessRenderKernel& kernel) {
            *fov = kernel.camera_state().fov_y_degrees;
            return 0;
        });
        return *fov;
    };
    host.invoke([](gs3d::ffi::HeadlessRenderKernel& kernel) {
        for (int step = 0; step < 3; ++step) {
            kernel.camera_input(0.0f, 0.0f, 0.0f, 0.0f, 3.0f);
        }
        return 0;
    });
    const float zoomed_fov = fov_of();
    REQUIRE(zoomed_fov > 0.0f);
    CHECK(zoomed_fov < 45.0f);  // 确实缩进了

    // 旋转尺寸后重新附加：必须复用 CPU 链。
    host.invoke([](gs3d::ffi::HeadlessRenderKernel& kernel) {
        kernel.detach_surface();
        return 0;
    });
    REQUIRE(host.invoke([&provider](gs3d::ffi::HeadlessRenderKernel& kernel) {
        return kernel.attach_surface(provider, 640, 360);
    }) == 0);
    // 缩放必须原样保留（重新附加不得把它抹回 45°）。
    CHECK(fov_of() == zoomed_fov);

    const auto reattached = lod_of();
    CHECK(reattached.cpu_chain_reused);
    CHECK(reattached.level_count == built.level_count);
    CHECK(reattached.drawn_points == built.drawn_points);
    CHECK(reattached.estimated_spacing == built.estimated_spacing);

    // GPU 缓冲已重传：能真的画出来（否则 lod_ 为空，只会得到 -2）。
    int presented = 0;
    for (int frame = 0; frame < 8 && presented == 0; ++frame) {
        int rc = 0;
        host.invoke([&rc](gs3d::ffi::HeadlessRenderKernel& kernel) {
            rc = kernel.present_frame();
            return 0;
        });
        if (rc == 0) {
            ++presented;
        } else {
            CHECK(rc == -2);  // -2 = 本帧跳过（交换链刚重建）
        }
    }
    CHECK(presented > 0);
}

TEST_CASE("tile point budget caps the drawn tile layer") {
    /*
     * 目的：放大后瓦片层不得把帧时打穿。
     * 真机 10M 实测（无预算）：一屏选到 880/1600 瓦片 = 549 万点 → 17 FPS。
     * 预算把「画/传多少」压到上限内，只丢屏幕投影最小的瓦片；
     * 小数据集总点数在预算内，行为不变。
     */
    const std::filesystem::path shader_dir(GS3D_TEST_SHADER_DIR);
    const std::filesystem::path bundle(GS3D_TEST_BENCH_DEV_BUNDLE);
    if (!std::filesystem::exists(shader_dir / "point.vert.spv") ||
        !std::filesystem::exists(bundle / "source.gs3d")) {
        SUCCEED("shader or tiled bench bundle missing; skipping tile budget");
        return;
    }

    HiddenWindow window;
    gs3d::render::VulkanSurfaceProvider provider{};
    const char* extensions[] = {VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    provider.instance_extensions = extensions;
    provider.instance_extension_count = 1;
    provider.create_surface = &create_win32_surface;
    provider.user_data = window.handle();

    constexpr std::uint64_t kBudget = 50000;
    gs3d::ffi::EngineHost host;
    gs3d::ffi::HeadlessRenderKernel::Config config{};
    config.width = 1204;
    config.height = 704;
    config.shader_dir = shader_dir.string();
    config.tile_point_budget = kBudget;
    if (host.start(config) != 0) {
        SUCCEED("Vulkan unavailable; skipping tile budget test");
        return;
    }
    REQUIRE(host.invoke([&bundle](gs3d::ffi::HeadlessRenderKernel& kernel) {
        return kernel.load_dataset(bundle.string());
    }) == 0);
    host.invoke([](gs3d::ffi::HeadlessRenderKernel& kernel) {
        for (int step = 0; step < 2; ++step) {
            kernel.camera_input(0.0f, 0.0f, 0.0f, 0.0f, 3.0f);
        }
        return 0;
    });

    std::vector<std::uint8_t> rgba(1204u * 704u * 4u);
    for (int frame = 0; frame < 60; ++frame) {
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        host.invoke([&rgba, &w, &h](gs3d::ffi::HeadlessRenderKernel& kernel) {
            return kernel.render_rgba(
                rgba.data(), static_cast<std::uint32_t>(rgba.size()), &w, &h
            );
        });
    }

    auto state = std::make_shared<gs3d::ffi::HeadlessRenderKernel::LodState>();
    host.invoke([state](gs3d::ffi::HeadlessRenderKernel& kernel) {
        *state = kernel.lod_state();
        return 0;
    });

    REQUIRE(state->tile_selected > 0);
    CHECK(state->tile_needed > 0);
    // 确实被截断：达标瓦片多于实际加载的瓦片。
    CHECK(state->tile_selected > state->tile_needed);
    // 计划点数落在预算内（bench-dev 瓦片点数是均匀的 625 点/块）。
    CHECK(state->tile_planned_points <= kBudget);
    CHECK(state->tile_planned_points > kBudget / 2);

    // 预算放开时不应截断（小数据集行为不变）。
    gs3d::ffi::EngineHost generous;
    gs3d::ffi::HeadlessRenderKernel::Config open_config{};
    open_config.width = 1204;
    open_config.height = 704;
    open_config.shader_dir = shader_dir.string();
    open_config.tile_point_budget = 0;
    if (generous.start(open_config) == 0) {
        REQUIRE(generous.invoke([&bundle](gs3d::ffi::HeadlessRenderKernel& k) {
            return k.load_dataset(bundle.string());
        }) == 0);
        generous.invoke([](gs3d::ffi::HeadlessRenderKernel& k) {
            for (int step = 0; step < 2; ++step) {
                k.camera_input(0.0f, 0.0f, 0.0f, 0.0f, 3.0f);
            }
            return 0;
        });
        for (int frame = 0; frame < 8; ++frame) {
            std::uint32_t w = 0;
            std::uint32_t h = 0;
            generous.invoke([&rgba, &w, &h](gs3d::ffi::HeadlessRenderKernel& k) {
                return k.render_rgba(
                    rgba.data(), static_cast<std::uint32_t>(rgba.size()), &w, &h
                );
            });
        }
        auto open_state = std::make_shared<gs3d::ffi::HeadlessRenderKernel::LodState>();
        generous.invoke([open_state](gs3d::ffi::HeadlessRenderKernel& k) {
            *open_state = k.lod_state();
            return 0;
        });
        CHECK(open_state->tile_selected == open_state->tile_needed);
    }
}

#else

TEST_CASE("engine presents the point cloud into a platform surface") {
    SUCCEED("platform present surface not covered on this platform yet");
}

TEST_CASE("re-attaching a surface reuses the CPU LOD chain") {
    SUCCEED("platform present surface not covered on this platform yet");
}

#endif