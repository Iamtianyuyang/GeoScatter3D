#include "render/VulkanSwapchain.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace gs3d::render {

namespace {

void check_vk(VkResult result, const char* message) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(message);
    }
}

} // namespace

VulkanSwapchain::VulkanSwapchain(
    const VulkanContext& context,
    const gs3d::platform::Window& window,
    SwapchainPresentModeHint present_mode_hint
)
    : context_(context)
    , present_mode_hint_(present_mode_hint)
{
    create(window);
}

VulkanSwapchain::~VulkanSwapchain() {
    cleanup();
}

void VulkanSwapchain::recreate(
    const gs3d::platform::Window& window
) {
    vkDeviceWaitIdle(context_.device());

    cleanup();
    create(window);
}

VkSwapchainKHR VulkanSwapchain::handle() const noexcept {
    return swapchain_;
}

VkFormat VulkanSwapchain::image_format() const noexcept {
    return image_format_;
}

VkExtent2D VulkanSwapchain::extent() const noexcept {
    return extent_;
}

const std::vector<VkImage>& VulkanSwapchain::images() const noexcept {
    return images_;
}

const std::vector<VkImageView>& VulkanSwapchain::image_views() const noexcept {
    return image_views_;
}

std::uint32_t VulkanSwapchain::image_count() const noexcept {
    return static_cast<std::uint32_t>(images_.size());
}

VkPresentModeKHR VulkanSwapchain::present_mode() const noexcept {
    return present_mode_;
}

SwapchainSupportDetails VulkanSwapchain::query_support(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface
) {
    SwapchainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical_device,
        surface,
        &details.capabilities
    );

    std::uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        physical_device,
        surface,
        &format_count,
        nullptr
    );

    if (format_count != 0) {
        details.formats.resize(format_count);

        vkGetPhysicalDeviceSurfaceFormatsKHR(
            physical_device,
            surface,
            &format_count,
            details.formats.data()
        );
    }

    std::uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        physical_device,
        surface,
        &present_mode_count,
        nullptr
    );

    if (present_mode_count != 0) {
        details.present_modes.resize(present_mode_count);

        vkGetPhysicalDeviceSurfacePresentModesKHR(
            physical_device,
            surface,
            &present_mode_count,
            details.present_modes.data()
        );
    }

    return details;
}

void VulkanSwapchain::create(
    const gs3d::platform::Window& window
) {
    const auto support = query_support(
        context_.physical_device(),
        context_.surface()
    );

    if (!support.complete()) {
        throw std::runtime_error(
            "VulkanSwapchain: swapchain support is incomplete"
        );
    }

    const VkSurfaceFormatKHR surface_format =
        choose_surface_format(support.formats);

    const VkPresentModeKHR present_mode =
        choose_present_mode(support.present_modes);

    const VkExtent2D swap_extent =
        choose_extent(support.capabilities, window);

    std::uint32_t image_count =
        support.capabilities.minImageCount + 1;

    if (support.capabilities.maxImageCount > 0 &&
        image_count > support.capabilities.maxImageCount) {
        image_count = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = context_.surface();

    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = swap_extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT;   // ponytail: screenshots read back swapchain pixels

    const auto& indices = context_.queue_family_indices();

    const std::uint32_t queue_family_indices[] = {
        indices.graphics_family.value(),
        indices.present_family.value()
    };

    if (indices.graphics_family.value() != indices.present_family.value()) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_family_indices;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.queueFamilyIndexCount = 0;
        create_info.pQueueFamilyIndices = nullptr;
    }

    create_info.preTransform = support.capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;

    create_info.oldSwapchain = VK_NULL_HANDLE;

    check_vk(
        vkCreateSwapchainKHR(
            context_.device(),
            &create_info,
            nullptr,
            &swapchain_
        ),
        "VulkanSwapchain: failed to create swapchain"
    );

    vkGetSwapchainImagesKHR(
        context_.device(),
        swapchain_,
        &image_count,
        nullptr
    );

    images_.resize(image_count);

    vkGetSwapchainImagesKHR(
        context_.device(),
        swapchain_,
        &image_count,
        images_.data()
    );

    image_format_ = surface_format.format;
    extent_ = swap_extent;
    present_mode_ = present_mode;

    create_image_views();
}

void VulkanSwapchain::cleanup() {
    for (auto image_view : image_views_) {
        if (image_view != VK_NULL_HANDLE) {
            vkDestroyImageView(context_.device(), image_view, nullptr);
        }
    }

    image_views_.clear();
    images_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(context_.device(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }

    image_format_ = VK_FORMAT_UNDEFINED;
    extent_ = {};
}

VkSurfaceFormatKHR VulkanSwapchain::choose_surface_format(
    const std::vector<VkSurfaceFormatKHR>& formats
) const {
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    return formats.front();
}

VkPresentModeKHR VulkanSwapchain::choose_present_mode(
    const std::vector<VkPresentModeKHR>& present_modes
) const {
    const auto supports = [&](VkPresentModeKHR mode) {
        return std::find(
                   present_modes.begin(),
                   present_modes.end(),
                   mode
               ) != present_modes.end();
    };

    switch (present_mode_hint_) {
    case SwapchainPresentModeHint::Immediate:
        if (supports(VK_PRESENT_MODE_IMMEDIATE_KHR)) {
            return VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
        break;
    case SwapchainPresentModeHint::Mailbox:
        if (supports(VK_PRESENT_MODE_MAILBOX_KHR)) {
            return VK_PRESENT_MODE_MAILBOX_KHR;
        }
        break;
    case SwapchainPresentModeHint::Fifo:
        return VK_PRESENT_MODE_FIFO_KHR;
    case SwapchainPresentModeHint::Auto:
        break;
    }

    for (const auto mode : present_modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanSwapchain::choose_extent(
    const VkSurfaceCapabilitiesKHR& capabilities,
    const gs3d::platform::Window& window
) const {
    if (capabilities.currentExtent.width !=
        std::numeric_limits<std::uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    const auto framebuffer_size = window.framebuffer_size();

    VkExtent2D actual_extent{
        framebuffer_size.width,
        framebuffer_size.height
    };

    actual_extent.width = std::clamp(
        actual_extent.width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width
    );

    actual_extent.height = std::clamp(
        actual_extent.height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height
    );

    return actual_extent;
}

void VulkanSwapchain::create_image_views() {
    image_views_.resize(images_.size());

    for (std::size_t i = 0; i < images_.size(); ++i) {
        image_views_[i] = create_image_view(images_[i]);
    }
}

VkImageView VulkanSwapchain::create_image_view(VkImage image) const {
    VkImageViewCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = image;

    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = image_format_;

    create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    create_info.subresourceRange.baseMipLevel = 0;
    create_info.subresourceRange.levelCount = 1;
    create_info.subresourceRange.baseArrayLayer = 0;
    create_info.subresourceRange.layerCount = 1;

    VkImageView image_view = VK_NULL_HANDLE;

    check_vk(
        vkCreateImageView(
            context_.device(),
            &create_info,
            nullptr,
            &image_view
        ),
        "VulkanSwapchain: failed to create image view"
    );

    return image_view;
}

} // namespace gs3d::render
