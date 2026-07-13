#include "app/ScreenshotService.hpp"
#include "util/Log.hpp"

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "imgui.h"
#include "platform/NativeFileDialog.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanSwapchain.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace gs3d::app {

ScreenshotCaptureRegion resolve_screenshot_capture_region(
    float canvas_min_x,
    float canvas_min_y,
    float canvas_max_x,
    float canvas_max_y,
    float viewport_x,
    float viewport_y,
    float framebuffer_scale_x,
    float framebuffer_scale_y,
    VkExtent2D swapchain_extent
) noexcept {
    if (canvas_max_x <= canvas_min_x ||
        canvas_max_y <= canvas_min_y ||
        framebuffer_scale_x <= 0.0f ||
        framebuffer_scale_y <= 0.0f) {
        return {};
    }

    int x = static_cast<int>((canvas_min_x - viewport_x) * framebuffer_scale_x);
    int y = static_cast<int>((canvas_min_y - viewport_y) * framebuffer_scale_y);
    int width = static_cast<int>((canvas_max_x - canvas_min_x) * framebuffer_scale_x);
    int height = static_cast<int>((canvas_max_y - canvas_min_y) * framebuffer_scale_y);
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > static_cast<int>(swapchain_extent.width)) {
        width = static_cast<int>(swapchain_extent.width) - x;
    }
    if (y + height > static_cast<int>(swapchain_extent.height)) {
        height = static_cast<int>(swapchain_extent.height) - y;
    }
    if (width <= 0 || height <= 0) {
        return {};
    }
    return {
        static_cast<std::uint32_t>(x),
        static_cast<std::uint32_t>(y),
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height)
    };
}

void ScreenshotService::request(
    const UiActions& gui_cmds,
    const AppState& app_state,
    const gs3d::render::VulkanSwapchain& swapchain
) {
    if (!gui_cmds.screenshot_requested) {
        return;
    }
    // Map viewport 0's canvas_rect (ImGui screen coords) →
    // swapchain physical pixels.
    for (const auto& view : app_state.render_views) {
        if (view.viewport_index != 0) continue;
        if (view.canvas_rect_max_x <= view.canvas_rect_min_x ||
            view.canvas_rect_max_y <= view.canvas_rect_min_y) break;
        const auto& io = ImGui::GetIO();
        const ImVec2 vp_pos = ImGui::GetMainViewport()->Pos;
        const float sx = io.DisplayFramebufferScale.x;
        const float sy = io.DisplayFramebufferScale.y;
        const auto region = resolve_screenshot_capture_region(
            view.canvas_rect_min_x,
            view.canvas_rect_min_y,
            view.canvas_rect_max_x,
            view.canvas_rect_max_y,
            vp_pos.x,
            vp_pos.y,
            sx,
            sy,
            swapchain.extent()
        );
        if (region.valid()) {
            offset_ = {region.x, region.y};
            extent_ = {region.width, region.height};
            pending_ = true;
        }
        break;
    }
}

// post_pass: after the swapchain render pass ends, copy the viewport
// region to a staging buffer so write_pending_screenshot() can read it
// back on the CPU after draw_frame.
void ScreenshotService::record_copy(
    VkCommandBuffer cmd,
    std::uint32_t image_index,
    gs3d::render::VulkanContext& context,
    const gs3d::render::VulkanSwapchain& swapchain
) {
    if (!pending_) return;

    const VkDeviceSize buf_size =
        static_cast<VkDeviceSize>(
            extent_.width) *
        static_cast<VkDeviceSize>(
            extent_.height) * 4;

    // Create staging buffer on first use (or re-create if
    // extent changed since last screenshot).
    if (staging_buffer_ == VK_NULL_HANDLE) {
        VkBufferCreateInfo buf_info{};
        buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buf_info.size = buf_size;
        buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(context.device(), &buf_info,
                           nullptr, &staging_buffer_) != VK_SUCCESS) {
            gs3d::util::log::error() << "[SCREENSHOT] buffer create failed\n";
            pending_ = false;
            return;
        }

        VkMemoryRequirements mem_req{};
        vkGetBufferMemoryRequirements(context.device(),
                                      staging_buffer_,
                                      &mem_req);
        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_req.size;

        VkPhysicalDeviceMemoryProperties mem_props{};
        vkGetPhysicalDeviceMemoryProperties(
            context.physical_device(), &mem_props);
        std::uint32_t mem_type_idx = 0;
        for (; mem_type_idx < mem_props.memoryTypeCount; ++mem_type_idx) {
            if ((mem_req.memoryTypeBits & (1u << mem_type_idx)) &&
                (mem_props.memoryTypes[mem_type_idx].propertyFlags &
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                break;
            }
        }
        alloc_info.memoryTypeIndex = mem_type_idx;
        if (vkAllocateMemory(context.device(), &alloc_info,
                             nullptr, &staging_memory_) != VK_SUCCESS) {
            gs3d::util::log::error() << "[SCREENSHOT] memory alloc failed\n";
            vkDestroyBuffer(context.device(), staging_buffer_, nullptr);
            staging_buffer_ = VK_NULL_HANDLE;
            pending_ = false;
            return;
        }
        if (vkBindBufferMemory(context.device(),
                               staging_buffer_,
                               staging_memory_, 0) != VK_SUCCESS) {
            gs3d::util::log::error() << "[SCREENSHOT] bind memory failed\n";
            vkFreeMemory(context.device(), staging_memory_, nullptr);
            vkDestroyBuffer(context.device(), staging_buffer_, nullptr);
            staging_buffer_ = VK_NULL_HANDLE;
            staging_memory_ = VK_NULL_HANDLE;
            pending_ = false;
            return;
        }
    }

    const VkImage src_img = swapchain.images()[image_index];

    // PRESENT_SRC_KHR → TRANSFER_SRC_OPTIMAL
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = src_img;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Copy viewport region
    {
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {
            static_cast<std::int32_t>(offset_.width),
            static_cast<std::int32_t>(offset_.height),
            0
        };
        region.imageExtent = {
            extent_.width,
            extent_.height,
            1
        };
        vkCmdCopyImageToBuffer(cmd, src_img,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            staging_buffer_, 1, &region);
    }

    // TRANSFER_SRC_OPTIMAL → PRESENT_SRC_KHR
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = src_img;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
}

// Reads the staged pixels back, resolves the output path via the native
// save dialog (with a timestamped fallback), writes the PNG, and frees
// the staging resources.
void ScreenshotService::write_pending(
    gs3d::render::VulkanContext& context,
    const gs3d::render::VulkanSwapchain& swapchain
) {
    if (!pending_ || staging_buffer_ == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle(context.device());

    const auto w = static_cast<int>(extent_.width);
    const auto h = static_cast<int>(extent_.height);
    const VkDeviceSize buf_size =
        static_cast<VkDeviceSize>(w) * static_cast<VkDeviceSize>(h) * 4;

    void* mapped = nullptr;
    vkMapMemory(context.device(), staging_memory_,
                0, buf_size, 0, &mapped);
    auto* pixels = static_cast<std::uint8_t*>(mapped);

    // BGR→RGB swizzle if swapchain uses B8G8R8A8 format
    const VkFormat fmt = swapchain.image_format();
    if (fmt == VK_FORMAT_B8G8R8A8_UNORM ||
        fmt == VK_FORMAT_B8G8R8A8_SRGB) {
        for (int i = 0; i < w * h; ++i) {
            std::swap(pixels[i * 4], pixels[i * 4 + 2]);
        }
    }

    // Resolve output path: native file dialog → fallback.
    // Cross-platform: uses NativeFileDialog (zenity/kdialog on
    // Linux, GetSaveFileNameW on Windows, osascript on macOS).
    // If the user cancels, skip save.
    // If no dialog tool is available, fall back to timestamped file.
    std::string out_path;
    bool user_cancelled = false;
    {
        const auto save_result =
            gs3d::platform::choose_save_file(
                "screenshot.png",
                "保存截图",
                "PNG Images|*.png"
            );
        if (save_result.path.has_value()) {
            out_path = save_result.path->string();
        } else if (!save_result.error.empty()) {
            // Dialog tool unavailable — not a user cancel;
            // fall through to timestamped fallback below.
        } else {
            // Empty path + no error = user cancelled the dialog.
            user_cancelled = true;
        }
    }
    if (!user_cancelled && out_path.empty()) {
        std::filesystem::create_directories("screenshots");
        const auto now = std::chrono::system_clock::now();
        const auto tt = std::chrono::system_clock::to_time_t(now);
        char ts[64];
        std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S",
                      std::localtime(&tt));
        out_path = std::string("screenshots/screenshot_") +
                   ts + ".png";
    }
    if (user_cancelled || out_path.empty()) {
        gs3d::util::log::info() << "[SCREENSHOT] cancelled.\n";
    } else if (!stbi_write_png(out_path.c_str(), w, h, 4,
                               pixels, w * 4)) {
        gs3d::util::log::error() << "[SCREENSHOT] stbi_write_png failed: "
                  << out_path << '\n';
    } else {
        gs3d::util::log::info() << "[SCREENSHOT] saved: " << out_path << '\n';
    }

    vkUnmapMemory(context.device(), staging_memory_);
    vkDestroyBuffer(context.device(),
                    staging_buffer_, nullptr);
    vkFreeMemory(context.device(),
                 staging_memory_, nullptr);
    staging_buffer_ = VK_NULL_HANDLE;
    staging_memory_ = VK_NULL_HANDLE;
    pending_ = false;
}

} // namespace gs3d::app
