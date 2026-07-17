#pragma once

#include "platform/NativeFileDialog.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

namespace gs3d::app {

struct AppState;
struct UiActions;
}

namespace gs3d::render {
class VulkanContext;
class VulkanSwapchain;
}

namespace gs3d::app {

struct ScreenshotCaptureRegion {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] bool valid() const noexcept {
        return width > 0 && height > 0;
    }
};

[[nodiscard]] ScreenshotCaptureRegion resolve_screenshot_capture_region(
    float canvas_min_x,
    float canvas_min_y,
    float canvas_max_x,
    float canvas_max_y,
    float viewport_x,
    float viewport_y,
    float framebuffer_scale_x,
    float framebuffer_scale_y,
    VkExtent2D swapchain_extent
) noexcept;

[[nodiscard]] std::filesystem::path make_screenshot_output_path(
    const std::filesystem::path& directory,
    std::uint64_t timestamp_milliseconds
);

[[nodiscard]] std::filesystem::path normalize_screenshot_output_path(
    std::filesystem::path path
);

struct ScreenshotWriteResult {
    std::filesystem::path path;
    std::string error;
};

// Owns the complete request → GPU copy → CPU write-back screenshot lifecycle.
// It is created after VulkanContext in ViewerApp::run(), so its capture buffer
// is always destroyed before the context goes out of scope.
class ScreenshotService {
public:
    ScreenshotService() = default;
    ~ScreenshotService();

    ScreenshotService(const ScreenshotService&) = delete;
    ScreenshotService& operator=(const ScreenshotService&) = delete;

    void request(
        const UiActions& actions,
        AppState& state,
        const gs3d::render::VulkanSwapchain& swapchain
    );

    void record_copy(
        VkCommandBuffer command_buffer,
        std::uint32_t image_index,
        gs3d::render::VulkanContext& context,
        const gs3d::render::VulkanSwapchain& swapchain
    );

    void write_pending(
        gs3d::render::VulkanContext& context,
        const gs3d::render::VulkanSwapchain& swapchain
    );

private:
    [[nodiscard]] bool prepare_capture(
        const AppState& state,
        const gs3d::render::VulkanSwapchain& swapchain
    );

    VkBuffer staging_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory_ = VK_NULL_HANDLE;
    VkExtent2D offset_{};
    VkExtent2D extent_{};
    std::filesystem::path output_path_;
    gs3d::platform::NativeSavePanel save_panel_;
    std::vector<std::future<ScreenshotWriteResult>> write_tasks_;
    std::string capture_error_;
    bool pending_ = false;
};

} // namespace gs3d::app
