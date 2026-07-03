#include "render/VulkanRenderer.hpp"

#include <array>
#include <chrono>
#include <stdexcept>

namespace gs3d::render {

namespace {

void check_vk(VkResult result, const char* message) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(message);
    }
}

} // namespace

VulkanRenderer::VulkanRenderer(
    const VulkanContext& context,
    VulkanSwapchain& swapchain
)
    : context_(context)
    , swapchain_(swapchain)
    , gpu_frame_timer_(context, MAX_FRAMES_IN_FLIGHT)
{
    create_render_pass();
    depth_buffer_.create(context_, swapchain_.extent());
    create_framebuffers();
    create_command_pool();
    create_command_buffers();
    create_sync_objects();
}

VulkanRenderer::~VulkanRenderer() {
    if (context_.device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.device());
    }

    cleanup_sync_objects();

    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_.device(), command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }

    cleanup_swapchain_resources();
}

void VulkanRenderer::draw_frame(gs3d::platform::Window& window) {
    draw_frame(window, FrameDrawCallbacks{});
}

void VulkanRenderer::draw_frame(
    gs3d::platform::Window& window,
    const DrawCallback& draw_callback
) {
    draw_frame(window, FrameDrawCallbacks{.in_pass = draw_callback});
}

void VulkanRenderer::draw_frame(
    gs3d::platform::Window& window,
    const FrameDrawCallbacks& callbacks
) {
    last_frame_fence_wait_ms_ = 0.0;
    last_draw_record_cpu_ms_ = 0.0;

    if (!framebuffer_available(window)) {
        return;
    }

    const auto frame_fence_wait_t0 = std::chrono::steady_clock::now();
    vkWaitForFences(
        context_.device(),
        1,
        &in_flight_fences_[current_frame_],
        VK_TRUE,
        UINT64_MAX
    );
    last_frame_fence_wait_ms_ =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - frame_fence_wait_t0
        ).count();

    if (callbacks.frame_ready) {
        callbacks.frame_ready(current_frame_);
    }

    gpu_frame_timer_.collect_frame(current_frame_);

    std::uint32_t image_index = 0;

    const auto acquire_t0 = std::chrono::steady_clock::now();

    VkResult acquire_result = vkAcquireNextImageKHR(
        context_.device(),
        swapchain_.handle(),
        UINT64_MAX,
        image_available_semaphores_[current_frame_],
        VK_NULL_HANDLE,
        &image_index
    );

    last_acquire_wait_ms_ =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - acquire_t0
        ).count();

    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain_resources(window);
        return;
    }

    if (acquire_result != VK_SUCCESS &&
        acquire_result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error(
            "VulkanRenderer: failed to acquire swapchain image"
        );
    }

    vkResetFences(
        context_.device(),
        1,
        &in_flight_fences_[current_frame_]
    );

    vkResetCommandBuffer(
        command_buffers_[current_frame_],
        0
    );

    {
        const auto record_t0 = std::chrono::steady_clock::now();
        record_command_buffer(
            command_buffers_[current_frame_],
            image_index,
            callbacks
        );
        last_draw_record_cpu_ms_ =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - record_t0
            ).count();
    }

    VkSemaphore wait_semaphores[] = {
        image_available_semaphores_[current_frame_]
    };

    VkPipelineStageFlags wait_stages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    };

    VkSemaphore signal_semaphores[] = {
        render_finished_semaphores_[current_frame_]
    };

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffers_[current_frame_];

    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    check_vk(
        vkQueueSubmit(
            context_.graphics_queue(),
            1,
            &submit_info,
            in_flight_fences_[current_frame_]
        ),
        "VulkanRenderer: failed to submit draw command buffer"
    );

    VkSwapchainKHR swapchains[] = {
        swapchain_.handle()
    };

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;

    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &image_index;

    VkResult present_result = vkQueuePresentKHR(
        context_.present_queue(),
        &present_info
    );

    if (present_result == VK_ERROR_OUT_OF_DATE_KHR ||
        present_result == VK_SUBOPTIMAL_KHR ||
        window.framebuffer_resized()) {
        window.clear_framebuffer_resized();
        recreate_swapchain_resources(window);
    } else if (present_result != VK_SUCCESS) {
        throw std::runtime_error(
            "VulkanRenderer: failed to present swapchain image"
        );
    }

    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::set_clear_color(const ClearColor& color) noexcept {
    clear_color_ = color;
}

void VulkanRenderer::wait_for_in_flight_fences() {
    const auto wait_t0 = std::chrono::steady_clock::now();
    for (std::uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (in_flight_fences_[i] != VK_NULL_HANDLE) {
            vkWaitForFences(
                context_.device(),
                1,
                &in_flight_fences_[i],
                VK_TRUE,
                UINT64_MAX
            );
        }
    }
    last_upload_fence_wait_ms_ =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - wait_t0
        ).count();
}

double VulkanRenderer::last_acquire_wait_ms() const noexcept {
    return last_acquire_wait_ms_;
}

double VulkanRenderer::last_frame_fence_wait_ms() const noexcept {
    return last_frame_fence_wait_ms_;
}

double VulkanRenderer::last_upload_fence_wait_ms() const noexcept {
    return last_upload_fence_wait_ms_;
}

double VulkanRenderer::last_draw_record_cpu_ms() const noexcept {
    return last_draw_record_cpu_ms_;
}

bool VulkanRenderer::has_last_gpu_frame_ms() const noexcept {
    return gpu_frame_timer_.has_last_frame_time();
}

double VulkanRenderer::last_gpu_frame_ms() const noexcept {
    return gpu_frame_timer_.last_frame_time_ms();
}

VkRenderPass VulkanRenderer::render_pass() const noexcept {
    return render_pass_;
}

VkCommandPool VulkanRenderer::command_pool() const noexcept {
    return command_pool_;
}

VkExtent2D VulkanRenderer::extent() const noexcept {
    return swapchain_.extent();
}

std::uint32_t VulkanRenderer::frames_in_flight() const noexcept {
    return MAX_FRAMES_IN_FLIGHT;
}

bool VulkanRenderer::is_frame_slot_ready(std::uint32_t frame_slot) const {
    if (frame_slot >= MAX_FRAMES_IN_FLIGHT ||
        in_flight_fences_[frame_slot] == VK_NULL_HANDLE) {
        return false;
    }

    const VkResult result = vkGetFenceStatus(
        context_.device(),
        in_flight_fences_[frame_slot]
    );
    if (result == VK_SUCCESS) {
        return true;
    }
    if (result == VK_NOT_READY) {
        return false;
    }

    throw std::runtime_error(
        "VulkanRenderer: failed to query frame fence status"
    );
}

void VulkanRenderer::create_render_pass() {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = swapchain_.image_format();
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth_attachment{};
    depth_attachment.format =
        VulkanDepthBuffer::find_depth_format(context_);
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_attachment_ref{};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref{};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;

    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;

    dependency.srcAccessMask = 0;

    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;

    dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    const VkAttachmentDescription attachments[] = {
        color_attachment,
        depth_attachment
    };

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType =
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

    render_pass_info.attachmentCount = 2;
    render_pass_info.pAttachments = attachments;

    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;

    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    check_vk(
        vkCreateRenderPass(
            context_.device(),
            &render_pass_info,
            nullptr,
            &render_pass_
        ),
        "VulkanRenderer: failed to create render pass"
    );
}

void VulkanRenderer::create_framebuffers() {
    const auto& image_views = swapchain_.image_views();

    if (!depth_buffer_.valid()) {
        throw std::runtime_error(
            "VulkanRenderer: depth buffer is invalid"
        );
    }

    framebuffers_.resize(image_views.size());

    for (std::size_t i = 0; i < image_views.size(); ++i) {
        VkImageView attachments[] = {
            image_views[i],
            depth_buffer_.image_view()
        };

        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType =
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;

        framebuffer_info.renderPass = render_pass_;
        framebuffer_info.attachmentCount = 2;
        framebuffer_info.pAttachments = attachments;

        framebuffer_info.width = swapchain_.extent().width;
        framebuffer_info.height = swapchain_.extent().height;
        framebuffer_info.layers = 1;

        check_vk(
            vkCreateFramebuffer(
                context_.device(),
                &framebuffer_info,
                nullptr,
                &framebuffers_[i]
            ),
            "VulkanRenderer: failed to create framebuffer"
        );
    }
}

void VulkanRenderer::create_command_pool() {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex =
        context_.queue_family_indices().graphics_family.value();

    check_vk(
        vkCreateCommandPool(
            context_.device(),
            &pool_info,
            nullptr,
            &command_pool_
        ),
        "VulkanRenderer: failed to create command pool"
    );
}

void VulkanRenderer::create_command_buffers() {
    command_buffers_.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;

    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount =
        static_cast<std::uint32_t>(command_buffers_.size());

    check_vk(
        vkAllocateCommandBuffers(
            context_.device(),
            &alloc_info,
            command_buffers_.data()
        ),
        "VulkanRenderer: failed to allocate command buffers"
    );
}

void VulkanRenderer::create_sync_objects() {
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType =
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (std::uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        check_vk(
            vkCreateSemaphore(
                context_.device(),
                &semaphore_info,
                nullptr,
                &image_available_semaphores_[i]
            ),
            "VulkanRenderer: failed to create image available semaphore"
        );

        check_vk(
            vkCreateSemaphore(
                context_.device(),
                &semaphore_info,
                nullptr,
                &render_finished_semaphores_[i]
            ),
            "VulkanRenderer: failed to create render finished semaphore"
        );

        check_vk(
            vkCreateFence(
                context_.device(),
                &fence_info,
                nullptr,
                &in_flight_fences_[i]
            ),
            "VulkanRenderer: failed to create in-flight fence"
        );
    }
}

void VulkanRenderer::recreate_swapchain_resources(
    gs3d::platform::Window& window
) {
    if (!framebuffer_available(window)) {
        return;
    }

    vkDeviceWaitIdle(context_.device());

    cleanup_framebuffers_and_depth();

    swapchain_.recreate(window);

    depth_buffer_.create(context_, swapchain_.extent());
    create_framebuffers();
}

void VulkanRenderer::record_command_buffer(
    VkCommandBuffer command_buffer,
    std::uint32_t image_index,
    const FrameDrawCallbacks& callbacks
) {
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    check_vk(
        vkBeginCommandBuffer(command_buffer, &begin_info),
        "VulkanRenderer: failed to begin command buffer"
    );

    gpu_frame_timer_.begin_frame(current_frame_, command_buffer);

    // pre_pass: offscreen render passes go here, outside the swapchain render pass.
    if (callbacks.pre_pass) {
        callbacks.pre_pass(command_buffer);
    }

    VkClearValue clear_values[2]{};

    clear_values[0].color = {
        {
            clear_color_.r,
            clear_color_.g,
            clear_color_.b,
            clear_color_.a
        }
    };

    clear_values[1].depthStencil = {
        1.0f,
        0
    };

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType =
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

    render_pass_info.renderPass = render_pass_;
    render_pass_info.framebuffer = framebuffers_[image_index];

    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = swapchain_.extent();

    render_pass_info.clearValueCount = 2;
    render_pass_info.pClearValues = clear_values;

    vkCmdBeginRenderPass(
        command_buffer,
        &render_pass_info,
        VK_SUBPASS_CONTENTS_INLINE
    );

    if (callbacks.in_pass) {
        callbacks.in_pass(command_buffer);
    }

    vkCmdEndRenderPass(command_buffer);

    // post_pass: swapchain image is in the layout specified by the render pass
    // finalLayout (PRESENT_SRC_KHR). Callbacks may transition from/to this layout.
    if (callbacks.post_pass) {
        callbacks.post_pass(command_buffer, image_index);
    }

    gpu_frame_timer_.end_frame(current_frame_, command_buffer);

    check_vk(
        vkEndCommandBuffer(command_buffer),
        "VulkanRenderer: failed to record command buffer"
    );
}

void VulkanRenderer::cleanup_swapchain_resources() {
    cleanup_framebuffers_and_depth();

    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(
            context_.device(),
            render_pass_,
            nullptr
        );

        render_pass_ = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::cleanup_framebuffers_and_depth() {
    for (auto framebuffer : framebuffers_) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(
                context_.device(),
                framebuffer,
                nullptr
            );
        }
    }

    framebuffers_.clear();

    depth_buffer_.destroy();
}

void VulkanRenderer::cleanup_sync_objects() {
    for (std::uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (render_finished_semaphores_[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(
                context_.device(),
                render_finished_semaphores_[i],
                nullptr
            );

            render_finished_semaphores_[i] = VK_NULL_HANDLE;
        }

        if (image_available_semaphores_[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(
                context_.device(),
                image_available_semaphores_[i],
                nullptr
            );

            image_available_semaphores_[i] = VK_NULL_HANDLE;
        }

        if (in_flight_fences_[i] != VK_NULL_HANDLE) {
            vkDestroyFence(
                context_.device(),
                in_flight_fences_[i],
                nullptr
            );

            in_flight_fences_[i] = VK_NULL_HANDLE;
        }
    }
}

bool VulkanRenderer::framebuffer_available(
    const gs3d::platform::Window& window
) const noexcept {
    const auto size = window.framebuffer_size();

    return size.width > 0 && size.height > 0;
}

} // namespace gs3d::render
