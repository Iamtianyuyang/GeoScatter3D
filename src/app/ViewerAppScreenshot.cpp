#include "app/ViewerApp.hpp"
#include "util/Log.hpp"
#include "app/ViewerAppRunState.hpp"

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

void ViewerApp::apply_screenshot_command(
    const UiActions& gui_cmds,
    ViewerAppScreenshotContext& ctx
) {
    if (!gui_cmds.screenshot_requested) {
        return;
    }
    // Map viewport 0's canvas_rect (ImGui screen coords) →
    // swapchain physical pixels.
    for (const auto& view : ctx.app_state.render_views) {
        if (view.viewport_index != 0) continue;
        if (view.canvas_rect_max_x <= view.canvas_rect_min_x ||
            view.canvas_rect_max_y <= view.canvas_rect_min_y) break;
        const auto& io = ImGui::GetIO();
        const ImVec2 vp_pos = ImGui::GetMainViewport()->Pos;
        const float sx = io.DisplayFramebufferScale.x;
        const float sy = io.DisplayFramebufferScale.y;
        int x = static_cast<int>((view.canvas_rect_min_x - vp_pos.x) * sx);
        int y = static_cast<int>((view.canvas_rect_min_y - vp_pos.y) * sy);
        int w = static_cast<int>((view.canvas_rect_max_x - view.canvas_rect_min_x) * sx);
        int h = static_cast<int>((view.canvas_rect_max_y - view.canvas_rect_min_y) * sy);
        // Clamp to swapchain extent
        const auto& sc_ext = ctx.swapchain.extent();
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > static_cast<int>(sc_ext.width))  w = static_cast<int>(sc_ext.width)  - x;
        if (y + h > static_cast<int>(sc_ext.height)) h = static_cast<int>(sc_ext.height) - y;
        if (w > 0 && h > 0) {
            ctx.screenshot_offset = {static_cast<std::uint32_t>(x),
                                     static_cast<std::uint32_t>(y)};
            ctx.screenshot_extent = {static_cast<std::uint32_t>(w),
                                     static_cast<std::uint32_t>(h)};
            ctx.screenshot_pending = true;
        }
        break;
    }
}

// post_pass: after the swapchain render pass ends, copy the viewport
// region to a staging buffer so write_pending_screenshot() can read it
// back on the CPU after draw_frame.
void ViewerApp::record_screenshot_copy(
    VkCommandBuffer cmd,
    std::uint32_t image_index,
    gs3d::render::VulkanContext& context,
    const gs3d::render::VulkanSwapchain& swapchain,
    ViewerAppScreenshotCaptureState& capture
) {
    if (!capture.pending) return;

    const VkDeviceSize buf_size =
        static_cast<VkDeviceSize>(
            capture.extent.width) *
        static_cast<VkDeviceSize>(
            capture.extent.height) * 4;

    // Create staging buffer on first use (or re-create if
    // extent changed since last screenshot).
    if (capture.staging_buf == VK_NULL_HANDLE) {
        VkBufferCreateInfo buf_info{};
        buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buf_info.size = buf_size;
        buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(context.device(), &buf_info,
                           nullptr, &capture.staging_buf) != VK_SUCCESS) {
            gs3d::util::log::error() << "[SCREENSHOT] buffer create failed\n";
            capture.pending = false;
            return;
        }

        VkMemoryRequirements mem_req{};
        vkGetBufferMemoryRequirements(context.device(),
                                      capture.staging_buf,
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
                             nullptr, &capture.staging_mem) != VK_SUCCESS) {
            gs3d::util::log::error() << "[SCREENSHOT] memory alloc failed\n";
            vkDestroyBuffer(context.device(), capture.staging_buf, nullptr);
            capture.staging_buf = VK_NULL_HANDLE;
            capture.pending = false;
            return;
        }
        if (vkBindBufferMemory(context.device(),
                               capture.staging_buf,
                               capture.staging_mem, 0) != VK_SUCCESS) {
            gs3d::util::log::error() << "[SCREENSHOT] bind memory failed\n";
            vkFreeMemory(context.device(), capture.staging_mem, nullptr);
            vkDestroyBuffer(context.device(), capture.staging_buf, nullptr);
            capture.staging_buf = VK_NULL_HANDLE;
            capture.staging_mem = VK_NULL_HANDLE;
            capture.pending = false;
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
            static_cast<std::int32_t>(capture.offset.width),
            static_cast<std::int32_t>(capture.offset.height),
            0
        };
        region.imageExtent = {
            capture.extent.width,
            capture.extent.height,
            1
        };
        vkCmdCopyImageToBuffer(cmd, src_img,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            capture.staging_buf, 1, &region);
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
void ViewerApp::write_pending_screenshot(
    gs3d::render::VulkanContext& context,
    const gs3d::render::VulkanSwapchain& swapchain,
    ViewerAppScreenshotCaptureState& capture
) {
    if (!capture.pending ||
        capture.staging_buf == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle(context.device());

    const auto w = static_cast<int>(capture.extent.width);
    const auto h = static_cast<int>(capture.extent.height);
    const VkDeviceSize buf_size =
        static_cast<VkDeviceSize>(w) * static_cast<VkDeviceSize>(h) * 4;

    void* mapped = nullptr;
    vkMapMemory(context.device(), capture.staging_mem,
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

    vkUnmapMemory(context.device(), capture.staging_mem);
    vkDestroyBuffer(context.device(),
                    capture.staging_buf, nullptr);
    vkFreeMemory(context.device(),
                 capture.staging_mem, nullptr);
    capture.staging_buf = VK_NULL_HANDLE;
    capture.staging_mem = VK_NULL_HANDLE;
    capture.pending = false;
}

} // namespace gs3d::app
