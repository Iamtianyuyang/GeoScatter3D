#include "render/OffscreenFramebuffer.hpp"
#include "util/Log.hpp"

#include "backends/imgui_impl_vulkan.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace gs3d::render {

namespace {

void check_vk(VkResult result, const char* message) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(message);
    }
}

} // namespace

// ─── Move semantics ──────────────────────────────────────────────────────────

OffscreenFramebuffer::OffscreenFramebuffer(OffscreenFramebuffer&& other) noexcept
    : context_          (other.context_)
    , extent_           (other.extent_)
    , color_format_     (other.color_format_)
    , clear_color_      (other.clear_color_)
    , color_image_      (other.color_image_)
    , color_memory_     (other.color_memory_)
    , color_view_       (other.color_view_)
    , pick_image_       (other.pick_image_)
    , pick_memory_      (other.pick_memory_)
    , pick_view_        (other.pick_view_)
    , pick_format_      (other.pick_format_)
    , pick_depth_image_ (other.pick_depth_image_)
    , pick_depth_memory_(other.pick_depth_memory_)
    , pick_depth_view_  (other.pick_depth_view_)
    , pick_depth_format_(other.pick_depth_format_)
    , depth_buffer_     (std::move(other.depth_buffer_))
    , render_pass_      (other.render_pass_)
    , framebuffer_      (other.framebuffer_)
    , imgui_descriptor_ (other.imgui_descriptor_)
{
    other.context_          = nullptr;
    other.color_image_      = VK_NULL_HANDLE;
    other.color_memory_     = VK_NULL_HANDLE;
    other.color_view_       = VK_NULL_HANDLE;
    other.pick_image_       = VK_NULL_HANDLE;
    other.pick_memory_      = VK_NULL_HANDLE;
    other.pick_view_        = VK_NULL_HANDLE;
    other.pick_format_      = VK_FORMAT_R32_UINT;
    other.pick_depth_image_ = VK_NULL_HANDLE;
    other.pick_depth_memory_= VK_NULL_HANDLE;
    other.pick_depth_view_  = VK_NULL_HANDLE;
    other.pick_depth_format_= VK_FORMAT_R32_SFLOAT;
    other.render_pass_      = VK_NULL_HANDLE;
    other.framebuffer_      = VK_NULL_HANDLE;
    other.imgui_descriptor_ = VK_NULL_HANDLE;
}

OffscreenFramebuffer& OffscreenFramebuffer::operator=(OffscreenFramebuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        new (this) OffscreenFramebuffer(std::move(other));
    }
    return *this;
}

OffscreenFramebuffer::~OffscreenFramebuffer() {
    destroy();
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

void OffscreenFramebuffer::create(
    const VulkanContext& context,
    VkExtent2D           extent,
    VkFormat             color_format
) {
    destroy();

    context_      = &context;
    extent_       = extent;
    color_format_ = color_format;
    pick_format_ = find_supported_pick_format(context.physical_device());
    pick_depth_format_ =
        find_supported_pick_depth_format(context.physical_device());

    gs3d::util::log::info() << "[PICK] id_attachment_format = "
              << static_cast<int>(pick_format_) << '\n';
    gs3d::util::log::info() << "[PICK] depth_attachment_format = "
              << static_cast<int>(pick_depth_format_) << '\n';

    create_color_image();
    create_pick_image();
    create_pick_depth_image();
    depth_buffer_.create(
        context,
        extent,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    );
    create_render_pass();
    create_framebuffer();
    register_imgui_texture();
}

void OffscreenFramebuffer::resize(VkExtent2D new_extent) {
    if (!context_) return;
    if (new_extent.width == 0 || new_extent.height == 0) return;
    if (new_extent.width  == extent_.width &&
        new_extent.height == extent_.height) return;

    // GPU must be idle before destroying images that may be in use.
    vkDeviceWaitIdle(context_->device());
    recreate_image_resources(new_extent);
}

void OffscreenFramebuffer::resize_after_device_idle(VkExtent2D new_extent) {
    if (!context_) return;
    if (new_extent.width == 0 || new_extent.height == 0) return;
    if (new_extent.width  == extent_.width &&
        new_extent.height == extent_.height) return;

    recreate_image_resources(new_extent);
}

void OffscreenFramebuffer::recreate_image_resources(VkExtent2D new_extent) {
    unregister_imgui_texture();
    cleanup_image_resources();

    extent_ = new_extent;

    create_color_image();
    create_pick_image();
    create_pick_depth_image();
    depth_buffer_.create(
        *context_,
        extent_,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    );
    create_framebuffer();
    register_imgui_texture();
}

void OffscreenFramebuffer::destroy() noexcept {
    if (!context_) return;

    vkDeviceWaitIdle(context_->device());

    unregister_imgui_texture();
    cleanup_image_resources();

    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(context_->device(), render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }

    context_ = nullptr;
}

void OffscreenFramebuffer::set_clear_color(const ClearColor& color) noexcept {
    clear_color_ = color;
}

// ─── Per-frame recording ─────────────────────────────────────────────────────

void OffscreenFramebuffer::render(VkCommandBuffer cmd, const DrawCallback& callback) {
    VkClearValue clear_values[4]{};
    clear_values[0].color        = {{clear_color_.r, clear_color_.g,
                                     clear_color_.b, clear_color_.a}};
    clear_values[1].color.uint32[0] = 0;
    clear_values[2].color.float32[0] = 1.0f;
    clear_values[3].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo begin_info{};
    begin_info.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_info.renderPass        = render_pass_;
    begin_info.framebuffer       = framebuffer_;
    begin_info.renderArea.offset = {0, 0};
    begin_info.renderArea.extent = extent_;
    begin_info.clearValueCount   = 4;
    begin_info.pClearValues      = clear_values;

    vkCmdBeginRenderPass(cmd, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    if (callback) {
        callback(cmd);
    }
    vkCmdEndRenderPass(cmd);
    // Subpass dependency transitions color image to SHADER_READ_ONLY_OPTIMAL.
}

// ─── Accessors ───────────────────────────────────────────────────────────────

VkRenderPass OffscreenFramebuffer::render_pass() const noexcept {
    return render_pass_;
}

VkExtent2D OffscreenFramebuffer::extent() const noexcept {
    return extent_;
}

VkDescriptorSet OffscreenFramebuffer::imgui_descriptor() const noexcept {
    return imgui_descriptor_;
}

VkImage OffscreenFramebuffer::color_image() const noexcept {
    return color_image_;
}

VkImage OffscreenFramebuffer::pick_image() const noexcept {
    return pick_image_;
}

VkImage OffscreenFramebuffer::pick_depth_image() const noexcept {
    return pick_depth_image_;
}

VkFormat OffscreenFramebuffer::color_format() const noexcept {
    return color_format_;
}

VkFormat OffscreenFramebuffer::pick_format() const noexcept {
    return pick_format_;
}

VkFormat OffscreenFramebuffer::pick_depth_format() const noexcept {
    return pick_depth_format_;
}

const VulkanDepthBuffer& OffscreenFramebuffer::depth_buffer() const noexcept {
    return depth_buffer_;
}

bool OffscreenFramebuffer::valid() const noexcept {
    return context_          != nullptr        &&
           color_image_      != VK_NULL_HANDLE &&
           pick_image_       != VK_NULL_HANDLE &&
           pick_depth_image_ != VK_NULL_HANDLE &&
           framebuffer_      != VK_NULL_HANDLE &&
           render_pass_      != VK_NULL_HANDLE &&
           imgui_descriptor_ != VK_NULL_HANDLE;
}

// ─── Private helpers ─────────────────────────────────────────────────────────

void OffscreenFramebuffer::create_color_image() {
    const VkDevice device = context_->device();

    VkImageCreateInfo image_info{};
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.extent        = {extent_.width, extent_.height, 1};
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.format        = color_format_;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                             | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    check_vk(
        vkCreateImage(device, &image_info, nullptr, &color_image_),
        "OffscreenFramebuffer: failed to create color image"
    );

    VkMemoryRequirements mem_req{};
    vkGetImageMemoryRequirements(device, color_image_, &mem_req);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = mem_req.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        context_->physical_device(),
        mem_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    check_vk(
        vkAllocateMemory(device, &alloc_info, nullptr, &color_memory_),
        "OffscreenFramebuffer: failed to allocate color image memory"
    );

    check_vk(
        vkBindImageMemory(device, color_image_, color_memory_, 0),
        "OffscreenFramebuffer: failed to bind color image memory"
    );

    VkImageViewCreateInfo view_info{};
    view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image                           = color_image_;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = color_format_;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    check_vk(
        vkCreateImageView(device, &view_info, nullptr, &color_view_),
        "OffscreenFramebuffer: failed to create color image view"
    );
}

void OffscreenFramebuffer::create_pick_image() {
    const VkDevice device = context_->device();

    VkImageCreateInfo image_info{};
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.extent        = {extent_.width, extent_.height, 1};
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.format        = pick_format_;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage         =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    check_vk(
        vkCreateImage(device, &image_info, nullptr, &pick_image_),
        "OffscreenFramebuffer: failed to create pick image"
    );

    VkMemoryRequirements mem_req{};
    vkGetImageMemoryRequirements(device, pick_image_, &mem_req);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = mem_req.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        context_->physical_device(),
        mem_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    check_vk(
        vkAllocateMemory(device, &alloc_info, nullptr, &pick_memory_),
        "OffscreenFramebuffer: failed to allocate pick image memory"
    );

    check_vk(
        vkBindImageMemory(device, pick_image_, pick_memory_, 0),
        "OffscreenFramebuffer: failed to bind pick image memory"
    );

    VkImageViewCreateInfo view_info{};
    view_info.sType                           =
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image                           = pick_image_;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = pick_format_;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    check_vk(
        vkCreateImageView(device, &view_info, nullptr, &pick_view_),
        "OffscreenFramebuffer: failed to create pick image view"
    );
}

void OffscreenFramebuffer::create_pick_depth_image() {
    const VkDevice device = context_->device();

    VkImageCreateInfo image_info{};
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.extent        = {extent_.width, extent_.height, 1};
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.format        = pick_depth_format_;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage         =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    check_vk(
        vkCreateImage(
            device,
            &image_info,
            nullptr,
            &pick_depth_image_
        ),
        "OffscreenFramebuffer: failed to create pick depth image"
    );

    VkMemoryRequirements mem_req{};
    vkGetImageMemoryRequirements(device, pick_depth_image_, &mem_req);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = mem_req.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        context_->physical_device(),
        mem_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    check_vk(
        vkAllocateMemory(device, &alloc_info, nullptr, &pick_depth_memory_),
        "OffscreenFramebuffer: failed to allocate pick depth image memory"
    );

    check_vk(
        vkBindImageMemory(device, pick_depth_image_, pick_depth_memory_, 0),
        "OffscreenFramebuffer: failed to bind pick depth image memory"
    );

    VkImageViewCreateInfo view_info{};
    view_info.sType                           =
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image                           = pick_depth_image_;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = pick_depth_format_;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    check_vk(
        vkCreateImageView(device, &view_info, nullptr, &pick_depth_view_),
        "OffscreenFramebuffer: failed to create pick depth image view"
    );
}

void OffscreenFramebuffer::create_render_pass() {
    // Color attachment finalLayout = SHADER_READ_ONLY_OPTIMAL so ImGui can
    // sample the image in the same command buffer's subsequent render pass.
    // Pattern: SaschaWillems/Vulkan offscreen sample (MIT).
    VkAttachmentDescription color_att{};
    color_att.format         = color_format_;
    color_att.samples        = VK_SAMPLE_COUNT_1_BIT;
    color_att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color_att.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depth_att{};
    depth_att.format         = VulkanDepthBuffer::find_depth_format(*context_);
    depth_att.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth_att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_att.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_att.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription pick_att{};
    pick_att.format         = pick_format_;
    pick_att.samples        = VK_SAMPLE_COUNT_1_BIT;
    pick_att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    pick_att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    pick_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    pick_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    pick_att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    pick_att.finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentDescription pick_depth_att{};
    pick_depth_att.format         = pick_depth_format_;
    pick_depth_att.samples        = VK_SAMPLE_COUNT_1_BIT;
    pick_depth_att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    pick_depth_att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    pick_depth_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    pick_depth_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    pick_depth_att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    pick_depth_att.finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference pick_ref{};
    pick_ref.attachment = 1;
    pick_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 3;
    depth_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference pick_depth_ref{};
    pick_depth_ref.attachment = 2;
    pick_depth_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    const VkAttachmentReference color_refs[] = {
        color_ref,
        pick_ref,
        pick_depth_ref
    };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 3;
    subpass.pColorAttachments       = color_refs;
    subpass.pDepthStencilAttachment = &depth_ref;

    // dep[0]: wait for ImGui sampling of previous frame to finish before
    //         writing as color attachment this frame.
    // dep[1]: wait for color attachment write to finish before ImGui
    //         samples it in the subsequent main render pass.
    VkSubpassDependency deps[2]{};

    deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass      = 0;
    deps[0].srcStageMask    =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask   =
        VK_ACCESS_SHADER_READ_BIT |
        VK_ACCESS_TRANSFER_READ_BIT;
    deps[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    deps[1].srcSubpass      = 0;
    deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask    =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask   =
        VK_ACCESS_SHADER_READ_BIT |
        VK_ACCESS_TRANSFER_READ_BIT;
    deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    const VkAttachmentDescription attachments[] = {
        color_att,
        pick_att,
        pick_depth_att,
        depth_att
    };

    VkRenderPassCreateInfo rp_info{};
    rp_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.attachmentCount = 4;
    rp_info.pAttachments    = attachments;
    rp_info.subpassCount    = 1;
    rp_info.pSubpasses      = &subpass;
    rp_info.dependencyCount = 2;
    rp_info.pDependencies   = deps;

    check_vk(
        vkCreateRenderPass(context_->device(), &rp_info, nullptr, &render_pass_),
        "OffscreenFramebuffer: failed to create render pass"
    );
}

void OffscreenFramebuffer::create_framebuffer() {
    const VkImageView attachments[] = {
        color_view_,
        pick_view_,
        pick_depth_view_,
        depth_buffer_.image_view()
    };

    VkFramebufferCreateInfo fb_info{};
    fb_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass      = render_pass_;
    fb_info.attachmentCount = 4;
    fb_info.pAttachments    = attachments;
    fb_info.width           = extent_.width;
    fb_info.height          = extent_.height;
    fb_info.layers          = 1;

    check_vk(
        vkCreateFramebuffer(context_->device(), &fb_info, nullptr, &framebuffer_),
        "OffscreenFramebuffer: failed to create framebuffer"
    );
}

void OffscreenFramebuffer::register_imgui_texture() {
    imgui_descriptor_ = ImGui_ImplVulkan_AddTexture(
        color_view_,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
}

void OffscreenFramebuffer::unregister_imgui_texture() noexcept {
    if (imgui_descriptor_ != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(imgui_descriptor_);
        imgui_descriptor_ = VK_NULL_HANDLE;
    }
}

void OffscreenFramebuffer::cleanup_image_resources() noexcept {
    if (!context_) return;
    const VkDevice device = context_->device();

    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }

    depth_buffer_.destroy();

    if (color_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, color_view_, nullptr);
        color_view_ = VK_NULL_HANDLE;
    }

    if (color_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, color_image_, nullptr);
        color_image_ = VK_NULL_HANDLE;
    }

    if (color_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, color_memory_, nullptr);
        color_memory_ = VK_NULL_HANDLE;
    }

    if (pick_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, pick_view_, nullptr);
        pick_view_ = VK_NULL_HANDLE;
    }

    if (pick_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, pick_image_, nullptr);
        pick_image_ = VK_NULL_HANDLE;
    }

    if (pick_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, pick_memory_, nullptr);
        pick_memory_ = VK_NULL_HANDLE;
    }

    if (pick_depth_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, pick_depth_view_, nullptr);
        pick_depth_view_ = VK_NULL_HANDLE;
    }

    if (pick_depth_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, pick_depth_image_, nullptr);
        pick_depth_image_ = VK_NULL_HANDLE;
    }

    if (pick_depth_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, pick_depth_memory_, nullptr);
        pick_depth_memory_ = VK_NULL_HANDLE;
    }
}

std::uint32_t OffscreenFramebuffer::find_memory_type(
    VkPhysicalDevice      physical_device,
    std::uint32_t         type_filter,
    VkMemoryPropertyFlags properties
) {
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    for (std::uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error(
        "OffscreenFramebuffer: failed to find suitable memory type"
    );
}

VkFormat OffscreenFramebuffer::find_supported_format(
    VkPhysicalDevice physical_device,
    const VkFormat* candidates,
    std::uint32_t candidate_count,
    VkImageTiling tiling,
    VkFormatFeatureFlags features
) {
    for (std::uint32_t i = 0; i < candidate_count; ++i) {
        const VkFormat format = candidates[i];

        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(
            physical_device,
            format,
            &properties
        );

        if (tiling == VK_IMAGE_TILING_LINEAR &&
            (properties.linearTilingFeatures & features) == features) {
            return format;
        }

        if (tiling == VK_IMAGE_TILING_OPTIMAL &&
            (properties.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error(
        "OffscreenFramebuffer: failed to find supported format"
    );
}

VkFormat OffscreenFramebuffer::find_supported_pick_format(
    VkPhysicalDevice physical_device
) {
    const VkFormat candidates[] = {
        VK_FORMAT_R32_UINT
    };

    return find_supported_format(
        physical_device,
        candidates,
        1,
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
    );
}

VkFormat OffscreenFramebuffer::find_supported_pick_depth_format(
    VkPhysicalDevice physical_device
) {
    const VkFormat candidates[] = {
        VK_FORMAT_R32_SFLOAT
    };

    return find_supported_format(
        physical_device,
        candidates,
        1,
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
    );
}

} // namespace gs3d::render
