#pragma once

#include "platform/Window.hpp"
#include "render/VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace gs3d::render {

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;

    [[nodiscard]]
    bool complete() const noexcept {
        return !formats.empty() && !present_modes.empty();
    }
};

enum class SwapchainPresentModeHint {
    Auto,
    Immediate,
    Mailbox,
    Fifo
};

class VulkanSwapchain {
public:
    VulkanSwapchain(
        const VulkanContext& context,
        const gs3d::platform::Window& window,
        SwapchainPresentModeHint present_mode_hint =
            SwapchainPresentModeHint::Auto
    );

    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    VulkanSwapchain(VulkanSwapchain&&) = delete;
    VulkanSwapchain& operator=(VulkanSwapchain&&) = delete;

    void recreate(const gs3d::platform::Window& window);

    [[nodiscard]]
    VkSwapchainKHR handle() const noexcept;

    [[nodiscard]]
    VkFormat image_format() const noexcept;

    [[nodiscard]]
    VkExtent2D extent() const noexcept;

    [[nodiscard]]
    const std::vector<VkImage>& images() const noexcept;

    [[nodiscard]]
    const std::vector<VkImageView>& image_views() const noexcept;

    [[nodiscard]]
    std::uint32_t image_count() const noexcept;

    [[nodiscard]]
    VkPresentModeKHR present_mode() const noexcept;

    [[nodiscard]]
    static SwapchainSupportDetails query_support(
        VkPhysicalDevice physical_device,
        VkSurfaceKHR surface
    );

private:
    const VulkanContext& context_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;

    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;

    VkFormat image_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    SwapchainPresentModeHint present_mode_hint_ =
        SwapchainPresentModeHint::Auto;

private:
    void create(const gs3d::platform::Window& window);
    void cleanup();

    [[nodiscard]]
    VkSurfaceFormatKHR choose_surface_format(
        const std::vector<VkSurfaceFormatKHR>& formats
    ) const;

    [[nodiscard]]
    VkPresentModeKHR choose_present_mode(
        const std::vector<VkPresentModeKHR>& present_modes
    ) const;

    [[nodiscard]]
    VkExtent2D choose_extent(
        const VkSurfaceCapabilitiesKHR& capabilities,
        const gs3d::platform::Window& window
    ) const;

    void create_image_views();

    [[nodiscard]]
    VkImageView create_image_view(VkImage image) const;
};

} // namespace gs3d::render
