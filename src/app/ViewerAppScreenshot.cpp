#include "app/ScreenshotService.hpp"
#include "util/Log.hpp"

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "imgui.h"
#include "render/VulkanContext.hpp"
#include "render/VulkanSwapchain.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <string>
#include <utility>
#include <vector>

namespace gs3d::app {

namespace {

template <typename T>
bool future_is_ready(std::future<T>& future) {
    return future.valid() &&
        future.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready;
}

// stbi_write_png_to_func 的回调：把编码字节追加进内存缓冲。
void append_png_bytes(void* context, void* data, const int size) {
    auto* out = static_cast<std::vector<std::uint8_t>*>(context);
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

void set_screenshot_notice(
    AppState& state,
    const ScreenshotNoticeKind kind,
    std::string message,
    const float seconds_left = 0.0f
) {
    state.screenshot_notice.kind = kind;
    state.screenshot_notice.message = std::move(message);
    state.screenshot_notice.seconds_left = seconds_left;
}

} // namespace

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

std::filesystem::path make_screenshot_output_path(
    const std::filesystem::path& directory,
    const std::uint64_t timestamp_milliseconds
) {
    return directory /
        ("screenshot_" + std::to_string(timestamp_milliseconds) + ".png");
}

std::filesystem::path normalize_screenshot_output_path(
    std::filesystem::path path
) {
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](const unsigned char value) {
            return static_cast<char>(std::tolower(value));
        }
    );
    if (extension != ".png") {
        path.replace_extension(".png");
    }
    return path;
}

ScreenshotService::~ScreenshotService() {
    for (auto& task : write_tasks_) {
        if (task.valid()) {
            task.wait();
        }
    }
}

bool ScreenshotService::prepare_capture(
    const AppState& app_state,
    const gs3d::render::VulkanSwapchain& swapchain
) {
    const RenderViewState* target_view = nullptr;
    for (const auto& view : app_state.render_views) {
        if (view.viewport_index == app_state.active_viewport_index &&
            view.visible &&
            !view.detached) {
            target_view = &view;
            break;
        }
    }
    if (target_view == nullptr) {
        for (const auto& view : app_state.render_views) {
            if (view.visible && !view.detached) {
                target_view = &view;
                break;
            }
        }
    }
    if (target_view == nullptr ||
        target_view->canvas_rect_max_x <= target_view->canvas_rect_min_x ||
        target_view->canvas_rect_max_y <= target_view->canvas_rect_min_y) {
        return false;
    }

    const auto& io = ImGui::GetIO();
    const ImVec2 viewport_pos = ImGui::GetMainViewport()->Pos;
    const auto region = resolve_screenshot_capture_region(
        target_view->canvas_rect_min_x,
        target_view->canvas_rect_min_y,
        target_view->canvas_rect_max_x,
        target_view->canvas_rect_max_y,
        viewport_pos.x,
        viewport_pos.y,
        io.DisplayFramebufferScale.x,
        io.DisplayFramebufferScale.y,
        swapchain.extent()
    );
    if (!region.valid()) {
        return false;
    }
    offset_ = {region.x, region.y};
    extent_ = {region.width, region.height};
    return true;
}

void ScreenshotService::request(
    const UiActions& gui_cmds,
    AppState& app_state,
    const gs3d::render::VulkanSwapchain& swapchain
) {
    if (!capture_error_.empty()) {
        set_screenshot_notice(
            app_state,
            ScreenshotNoticeKind::kError,
            "截图失败：" + capture_error_,
            4.0f
        );
        capture_error_.clear();
    }

    for (auto task = write_tasks_.begin(); task != write_tasks_.end();) {
        if (!future_is_ready(*task)) {
            ++task;
            continue;
        }
        const ScreenshotWriteResult result = task->get();
        if (result.error.empty()) {
            set_screenshot_notice(
                app_state,
                ScreenshotNoticeKind::kSaved,
                "截图已保存：" + result.path.filename().string(),
                3.5f
            );
        } else {
            set_screenshot_notice(
                app_state,
                ScreenshotNoticeKind::kError,
                "截图失败：" + result.error,
                4.0f
            );
        }
        task = write_tasks_.erase(task);
    }

    if (const auto completed = save_panel_.poll();
        completed.has_value()) {
        const auto& result = *completed;
        if (result.path.has_value()) {
            output_path_ =
                normalize_screenshot_output_path(*result.path);
            if (prepare_capture(app_state, swapchain)) {
                pending_ = true;
                set_screenshot_notice(
                    app_state,
                    ScreenshotNoticeKind::kSaving,
                    "正在保存截图…"
                );
            } else {
                output_path_.clear();
                set_screenshot_notice(
                    app_state,
                    ScreenshotNoticeKind::kError,
                    "截图失败：当前主视图不可截图",
                    4.0f
                );
            }
        } else if (!result.error.empty()) {
            set_screenshot_notice(
                app_state,
                ScreenshotNoticeKind::kError,
                "无法打开保存面板：" + result.error,
                4.0f
            );
        } else {
            set_screenshot_notice(
                app_state,
                ScreenshotNoticeKind::kCancelled,
                "已取消截图",
                2.0f
            );
        }
    }

    if (!gui_cmds.screenshot_requested ||
        save_panel_.active() ||
        pending_) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto timestamp = std::chrono::duration_cast<
        std::chrono::milliseconds
    >(now.time_since_epoch()).count();
    const std::string default_filename =
        make_screenshot_output_path(
            {},
            static_cast<std::uint64_t>(timestamp)
        ).filename().string();
    if (!save_panel_.begin(
            default_filename,
            "保存截图",
            "PNG Images|*.png"
        )) {
        set_screenshot_notice(
            app_state,
            ScreenshotNoticeKind::kError,
            "无法打开系统保存面板",
            4.0f
        );
        return;
    }
    set_screenshot_notice(
        app_state,
        ScreenshotNoticeKind::kSelectingPath,
        "请选择截图名称和保存位置…"
    );
}

bool ScreenshotService::request_control_capture(
    const AppState& app_state,
    const gs3d::render::VulkanSwapchain& swapchain
) {
    static_cast<void>(app_state);
    if (pending_) {
        return false;  // 已有截图进行中（文件对话框路径或上一次控制面截图）
    }
    // 控制面截图 = 整个窗口画面（含面板/Dock），而非仅活动视口画布：
    // 外部 AI 需要看到它正在控制的全部 UI。多视口时所有视口都在画面内。
    offset_ = {0, 0};
    extent_ = swapchain.extent();
    if (extent_.width == 0 || extent_.height == 0) {
        return false;
    }
    in_memory_ = true;
    memory_png_ready_ = false;
    in_memory_armed_at_ = std::chrono::steady_clock::now();
    pending_ = true;
    return true;
}

std::optional<gs3d::control::CapturedImage> ScreenshotService::take_control_png() {
    if (!memory_png_ready_) {
        return std::nullopt;
    }
    memory_png_ready_ = false;
    return gs3d::control::CapturedImage{
        .png_bytes = std::move(memory_png_),
        .width = extent_.width,
        .height = extent_.height
    };
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
            capture_error_ = "无法创建截图读回缓冲区";
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
        if (mem_type_idx == mem_props.memoryTypeCount) {
            gs3d::util::log::error()
                << "[SCREENSHOT] no host-visible memory type\n";
            capture_error_ = "GPU 不支持截图读回内存";
            vkDestroyBuffer(context.device(), staging_buffer_, nullptr);
            staging_buffer_ = VK_NULL_HANDLE;
            pending_ = false;
            return;
        }
        alloc_info.memoryTypeIndex = mem_type_idx;
        if (vkAllocateMemory(context.device(), &alloc_info,
                             nullptr, &staging_memory_) != VK_SUCCESS) {
            gs3d::util::log::error() << "[SCREENSHOT] memory alloc failed\n";
            capture_error_ = "无法分配截图读回内存";
            vkDestroyBuffer(context.device(), staging_buffer_, nullptr);
            staging_buffer_ = VK_NULL_HANDLE;
            pending_ = false;
            return;
        }
        if (vkBindBufferMemory(context.device(),
                               staging_buffer_,
                               staging_memory_, 0) != VK_SUCCESS) {
            gs3d::util::log::error() << "[SCREENSHOT] bind memory failed\n";
            capture_error_ = "无法绑定截图读回内存";
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

// Reads the staged pixels back, releases the Vulkan resources immediately,
// then encodes the PNG on a worker so the render loop never waits on a native
// file dialog or PNG compression.
void ScreenshotService::write_pending(
    gs3d::render::VulkanContext& context,
    const gs3d::render::VulkanSwapchain& swapchain
) {
    if (!pending_ || staging_buffer_ == VK_NULL_HANDLE) {
        // 控制面截图武装后若长时间没有帧被记录（交换链重建/窗口不可用），
        // 主动放弃，避免 pending_ 永久卡死后续截图。
        if (pending_ && in_memory_ &&
            std::chrono::steady_clock::now() - in_memory_armed_at_ >
                std::chrono::seconds(2)) {
            gs3d::util::log::error()
                << "[SCREENSHOT] control capture abandoned: no frame recorded\n";
            pending_ = false;
            in_memory_ = false;
            memory_png_ready_ = false;
        }
        return;
    }

    vkDeviceWaitIdle(context.device());

    const auto w = static_cast<int>(extent_.width);
    const auto h = static_cast<int>(extent_.height);
    const VkDeviceSize buf_size =
        static_cast<VkDeviceSize>(w) * static_cast<VkDeviceSize>(h) * 4;

    void* mapped = nullptr;
    if (vkMapMemory(
            context.device(),
            staging_memory_,
            0,
            buf_size,
            0,
            &mapped
        ) != VK_SUCCESS) {
        gs3d::util::log::error() << "[SCREENSHOT] memory map failed\n";
        capture_error_ = "无法读取截图像素";
        vkDestroyBuffer(context.device(), staging_buffer_, nullptr);
        vkFreeMemory(context.device(), staging_memory_, nullptr);
        staging_buffer_ = VK_NULL_HANDLE;
        staging_memory_ = VK_NULL_HANDLE;
        pending_ = false;
        return;
    }
    const auto* mapped_pixels = static_cast<const std::uint8_t*>(mapped);
    std::vector<std::uint8_t> pixels(
        mapped_pixels,
        mapped_pixels + static_cast<std::size_t>(buf_size)
    );
    vkUnmapMemory(context.device(), staging_memory_);

    // BGR→RGB swizzle if swapchain uses B8G8R8A8 format
    const VkFormat fmt = swapchain.image_format();
    if (fmt == VK_FORMAT_B8G8R8A8_UNORM ||
        fmt == VK_FORMAT_B8G8R8A8_SRGB) {
        for (int i = 0; i < w * h; ++i) {
            std::swap(pixels[i * 4], pixels[i * 4 + 2]);
        }
    }

    vkDestroyBuffer(context.device(),
                    staging_buffer_, nullptr);
    vkFreeMemory(context.device(),
                 staging_memory_, nullptr);
    staging_buffer_ = VK_NULL_HANDLE;
    staging_memory_ = VK_NULL_HANDLE;
    pending_ = false;

    // 控制面内存截图：编码进 memory_png_，不写文件、不弹保存面板。
    if (in_memory_) {
        in_memory_ = false;
        memory_png_.clear();
        const int encoded = stbi_write_png_to_func(
            &append_png_bytes,
            &memory_png_,
            w,
            h,
            4,
            pixels.data(),
            w * 4
        );
        memory_png_ready_ = encoded != 0 && !memory_png_.empty();
        if (!memory_png_ready_) {
            gs3d::util::log::error()
                << "[SCREENSHOT] in-memory PNG encode failed\n";
        }
        return;
    }

    const auto output_path = output_path_;
    output_path_.clear();
    write_tasks_.push_back(std::async(
        std::launch::async,
        [output_path, w, h, pixels = std::move(pixels)]() {
            std::error_code directory_error;
            const auto parent = output_path.parent_path();
            if (!parent.empty()) {
                std::filesystem::create_directories(
                    parent,
                    directory_error
                );
            }
            if (directory_error) {
                gs3d::util::log::error()
                    << "[SCREENSHOT] directory create failed: "
                    << directory_error.message() << '\n';
                return ScreenshotWriteResult{
                    output_path,
                    "无法创建保存目录：" + directory_error.message()
                };
            }
            const std::string output = output_path.string();
            if (!stbi_write_png(
                    output.c_str(),
                    w,
                    h,
                    4,
                    pixels.data(),
                    w * 4
                )) {
                gs3d::util::log::error()
                    << "[SCREENSHOT] stbi_write_png failed: "
                    << output << '\n';
                return ScreenshotWriteResult{
                    output_path,
                    "无法写入 PNG 文件"
                };
            }
            gs3d::util::log::info()
                << "[SCREENSHOT] saved: " << output << '\n';
            return ScreenshotWriteResult{output_path, {}};
        }
    ));
}

} // namespace gs3d::app
