#include "render/PresentSurface.hpp"

#include "render/VulkanBuffer.hpp"
#include "util/Log.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace gs3d::render {

namespace {

constexpr const char* kTag = "PresentSurface";

void check_vk(VkResult result, const char* message) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            std::string(kTag) + ": " + message +
            " (VkResult=" + std::to_string(static_cast<int>(result)) + ")"
        );
    }
}

}  // namespace

PresentSurface::PresentSurface(const VulkanContext& context, Config config)
    : context_(&context)
    , config_(config)
{
    if (context.headless() || context_->surface() == VK_NULL_HANDLE) {
        throw std::invalid_argument(
            "PresentSurface: a non-headless VulkanContext with a surface is "
            "required"
        );
    }

    try {
        create_swapchain();
        create_depth();
        create_render_pass();
        create_framebuffers();

        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex =
            context_->queue_family_indices().graphics_family.value();
        check_vk(
            vkCreateCommandPool(context_->device(), &pool_info, nullptr, &command_pool_),
            "create copy command pool"
        );

        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = command_pool_;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        check_vk(
            vkAllocateCommandBuffers(context_->device(), &alloc, &copy_command_),
            "allocate copy command buffer"
        );

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        check_vk(
            vkCreateFence(context_->device(), &fence_info, nullptr, &copy_fence_),
            "create copy fence"
        );
    } catch (...) {
        // 构造失败时析构不会运行：必须在这里回收已创建对象。
        destroy_swapchain_objects();
        destroy_render_objects();
        throw;
    }
}

PresentSurface::~PresentSurface() {
    if (context_ == nullptr) {
        return;
    }
    vkDeviceWaitIdle(context_->device());

    if (copy_fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(context_->device(), copy_fence_, nullptr);
        copy_fence_ = VK_NULL_HANDLE;
    }
    if (copy_command_ != VK_NULL_HANDLE && command_pool_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(context_->device(), command_pool_, 1, &copy_command_);
        copy_command_ = VK_NULL_HANDLE;
    }
    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_->device(), command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }

    destroy_swapchain_objects();
    destroy_render_objects();
}

bool PresentSurface::valid() const noexcept {
    return context_ != nullptr && swapchain_ != VK_NULL_HANDLE &&
           render_pass_ != VK_NULL_HANDLE &&
           framebuffers_.size() == images_.size();
}

VkSurfaceFormatKHR PresentSurface::choose_surface_format() const {
    // 不依赖 SwapchainSupportDetails（那个类型在 VulkanSwapchain.hpp 里，
    // 而后者把 GLFW 的 Window 也带了进来）。
    std::uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        context_->physical_device(), context_->surface(), &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    if (format_count > 0) {
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            context_->physical_device(), context_->surface(), &format_count,
            formats.data());
    }
    if (formats.empty()) {
        throw std::runtime_error(std::string(kTag) + ": surface has no formats");
    }
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
        return {config_.preferred_format, formats[0].colorSpace};
    }
    for (const auto& candidate : formats) {
        if (candidate.format == config_.preferred_format) {
            return candidate;
        }
    }
    // 退一步找 R8G8B8A8（Android 上更常见）。
    for (const auto& candidate : formats) {
        if (candidate.format == VK_FORMAT_R8G8B8A8_UNORM) {
            return candidate;
        }
    }
    return formats.front();
}

VkPresentModeKHR PresentSurface::choose_present_mode() const {
    std::uint32_t mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        context_->physical_device(), context_->surface(), &mode_count, nullptr);
    std::vector<VkPresentModeKHR> modes(mode_count);
    if (mode_count > 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            context_->physical_device(), context_->surface(), &mode_count,
            modes.data());
    }
    const bool has_mailbox = std::find(
        modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end();
    if (!config_.vsync && has_mailbox) {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    // FIFO 是唯一保证存在的模式。
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D PresentSurface::choose_extent() const {
    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        context_->physical_device(), context_->surface(), &capabilities);

    // 某些平台（含部分 Android）用 0xFFFFFFFF 表示"由窗口决定"。
    if (capabilities.currentExtent.width != 0xFFFFFFFFu) {
        return capabilities.currentExtent;
    }

    const std::uint32_t width = config_.requested_width != 0
        ? config_.requested_width
        : capabilities.minImageExtent.width;
    const std::uint32_t height = config_.requested_height != 0
        ? config_.requested_height
        : capabilities.minImageExtent.height;
    return VkExtent2D{
        std::clamp(width, capabilities.minImageExtent.width,
                   capabilities.maxImageExtent.width),
        std::clamp(height, capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height),
    };
}

void PresentSurface::create_swapchain() {
    const VkSurfaceFormatKHR surface_format = choose_surface_format();
    format_ = surface_format.format;
    color_space_ = surface_format.colorSpace;
    present_mode_ = choose_present_mode();
    extent_ = choose_extent();
    if (extent_.width == 0 || extent_.height == 0) {
        throw std::runtime_error(
            std::string(kTag) + ": surface reported a zero-sized extent"
        );
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        context_->physical_device(), context_->surface(), &capabilities);

    std::uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = context_->surface();
    create_info.minImageCount = image_count;
    create_info.imageFormat = format_;
    create_info.imageColorSpace = color_space_;
    create_info.imageExtent = extent_;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode_;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = swapchain_;

    VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
    check_vk(
        vkCreateSwapchainKHR(context_->device(), &create_info, nullptr, &new_swapchain),
        "failed to create swapchain"
    );

    // 旧交换链（若存在）此时可以释放；其 image view/framebuffer 需先销毁。
    if (swapchain_ != VK_NULL_HANDLE) {
        for (auto framebuffer : framebuffers_) {
            vkDestroyFramebuffer(context_->device(), framebuffer, nullptr);
        }
        framebuffers_.clear();
        for (auto view : image_views_) {
            vkDestroyImageView(context_->device(), view, nullptr);
        }
        image_views_.clear();
        images_.clear();
        vkDestroySwapchainKHR(context_->device(), swapchain_, nullptr);
    }
    swapchain_ = new_swapchain;

    std::uint32_t actual_count = 0;
    check_vk(
        vkGetSwapchainImagesKHR(context_->device(), swapchain_, &actual_count, nullptr),
        "failed to query swapchain images"
    );
    images_.resize(actual_count);
    check_vk(
        vkGetSwapchainImagesKHR(
            context_->device(), swapchain_, &actual_count, images_.data()),
        "failed to fetch swapchain images"
    );

    image_views_.resize(images_.size(), VK_NULL_HANDLE);
    for (std::size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = images_[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format_;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        check_vk(
            vkCreateImageView(
                context_->device(), &view_info, nullptr, &image_views_[i]),
            "failed to create swapchain image view"
        );
    }

    needs_recreate_ = false;
}

void PresentSurface::create_render_pass() {
    VkAttachmentDescription color{};
    color.format = format_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format = depth_.format();
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 1;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    // 呈现前需要把颜色附件从 COLOR_ATTACHMENT 转到 PRESENT_SRC。
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    const VkAttachmentDescription attachments[] = {color, depth};

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    check_vk(
        vkCreateRenderPass(context_->device(), &info, nullptr, &render_pass_),
        "failed to create present render pass"
    );
}

void PresentSurface::create_depth() {
    depth_.create(*context_, extent_);
}

void PresentSurface::create_framebuffers() {
    framebuffers_.resize(images_.size(), VK_NULL_HANDLE);
    for (std::size_t i = 0; i < images_.size(); ++i) {
        const VkImageView attachments[] = {image_views_[i], depth_.image_view()};
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = render_pass_;
        info.attachmentCount = 2;
        info.pAttachments = attachments;
        info.width = extent_.width;
        info.height = extent_.height;
        info.layers = 1;
        check_vk(
            vkCreateFramebuffer(
                context_->device(), &info, nullptr, &framebuffers_[i]),
            "failed to create swapchain framebuffer"
        );
    }
}

bool PresentSurface::begin_frame(Frame& frame) {
    if (!valid()) {
        return false;
    }
    if (needs_recreate_) {
        recreate();
        return false;
    }

    VkResult result = vkAcquireNextImageKHR(
        context_->device(),
        swapchain_,
        UINT64_MAX,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        &frame.image_index
    );
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        needs_recreate_ = true;
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        check_vk(result, "vkAcquireNextImageKHR failed");
    }

    frame.image = images_[frame.image_index];
    frame.framebuffer = framebuffers_[frame.image_index];
    frame.extent = extent_;
    frame.acquired = true;
    return true;
}

bool PresentSurface::end_frame(const Frame& frame) {
    if (!frame.acquired) {
        return false;
    }

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    const std::uint32_t image_index = frame.image_index;
    present.pImageIndices = &image_index;

    const VkResult result =
        vkQueuePresentKHR(context_->present_queue(), &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // 已提交的帧仍然可见；下一帧重建交换链即可。
        needs_recreate_ = true;
        return true;
    }
    if (result != VK_SUCCESS) {
        check_vk(result, "vkQueuePresentKHR failed");
    }
    return true;
}

void PresentSurface::recreate() {
    vkDeviceWaitIdle(context_->device());

    destroy_swapchain_objects();

    // 尺寸可能已变：深度附件与 framebuffer 必须一起重建。
    depth_.destroy();
    create_swapchain();
    create_depth();
    create_framebuffers();
    needs_recreate_ = false;

    gs3d::util::log::info()
        << "[PresentSurface] swapchain recreated: " << extent_.width << 'x'
        << extent_.height << " images=" << images_.size() << '\n';
}

bool PresentSurface::copy_frame_to_host(
    const Frame& frame,
    std::vector<std::uint8_t>& out_rgba
) {
    if (context_ == nullptr || !frame.acquired || copy_command_ == VK_NULL_HANDLE) {
        return false;
    }

    const std::size_t bytes =
        static_cast<std::size_t>(frame.extent.width) * frame.extent.height * 4u;
    out_rgba.resize(bytes);

    VulkanBuffer staging;
    try {
        staging.create(
            *context_,
            bytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        check_vk(vkResetFences(context_->device(), 1, &copy_fence_), "reset fence");
        check_vk(vkResetCommandBuffer(copy_command_, 0), "reset command buffer");

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check_vk(vkBeginCommandBuffer(copy_command_, &begin), "begin copy cmd");

        VkImageMemoryBarrier to_transfer{};
        to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_transfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        to_transfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.image = frame.image;
        to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_transfer.subresourceRange.levelCount = 1;
        to_transfer.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            copy_command_,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &to_transfer
        );

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = VkExtent3D{frame.extent.width, frame.extent.height, 1};
        vkCmdCopyImageToBuffer(
            copy_command_, frame.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            staging.handle(), 1, &region
        );

        VkImageMemoryBarrier back = to_transfer;
        back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        back.dstAccessMask = 0;
        back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        back.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(
            copy_command_,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &back
        );

        check_vk(vkEndCommandBuffer(copy_command_), "end copy cmd");

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &copy_command_;
        check_vk(
            vkQueueSubmit(context_->graphics_queue(), 1, &submit, copy_fence_),
            "submit copy cmd"
        );
        check_vk(
            vkWaitForFences(
                context_->device(), 1, &copy_fence_, VK_TRUE, UINT64_MAX),
            "wait copy"
        );

        void* mapped = nullptr;
        check_vk(
            vkMapMemory(
                context_->device(), staging.memory(), 0, bytes, 0, &mapped),
            "map staging"
        );
        std::memcpy(out_rgba.data(), mapped, bytes);
        vkUnmapMemory(context_->device(), staging.memory());
    } catch (const std::exception& ex) {
        gs3d::util::log::warning()
            << "[PresentSurface] copy_frame_to_host failed: " << ex.what() << '\n';
        staging.destroy();
        return false;
    }
    staging.destroy();
    return true;
}

void PresentSurface::destroy_swapchain_objects() noexcept {
    if (context_ == nullptr) {
        return;
    }
    const VkDevice device = context_->device();
    for (auto framebuffer : framebuffers_) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
    }
    framebuffers_.clear();
    for (auto view : image_views_) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
        }
    }
    image_views_.clear();
    images_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    needs_recreate_ = true;
}

void PresentSurface::destroy_render_objects() noexcept {
    if (context_ == nullptr) {
        return;
    }
    depth_.destroy();
    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(context_->device(), render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
}

}  // namespace gs3d::render
