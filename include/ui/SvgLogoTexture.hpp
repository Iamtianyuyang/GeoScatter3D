#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace gs3d::ui {

class SvgLogoTexture {
public:
    SvgLogoTexture(
        VkDevice device,
        VkPhysicalDevice physical_device,
        VkQueue graphics_queue,
        VkCommandPool command_pool,
        const std::string& svg_path,
        std::uint32_t size
    );

    ~SvgLogoTexture();

    SvgLogoTexture(const SvgLogoTexture&) = delete;
    SvgLogoTexture& operator=(const SvgLogoTexture&) = delete;

    SvgLogoTexture(SvgLogoTexture&& other) noexcept;
    SvgLogoTexture& operator=(SvgLogoTexture&& other) noexcept;

    [[nodiscard]]
    VkDescriptorSet descriptor() const noexcept { return descriptor_; }

    [[nodiscard]]
    bool valid() const noexcept { return descriptor_ != VK_NULL_HANDLE; }

private:
    void cleanup() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;

    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory image_memory_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_ = VK_NULL_HANDLE;
};

} // namespace gs3d::ui
