#pragma once

#include "platform/Window.hpp"
#include "render/VulkanGpuInfo.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gs3d::render {

struct QueueFamilyIndices {
    std::optional<std::uint32_t> graphics_family;
    std::optional<std::uint32_t> present_family;

    [[nodiscard]]
    bool complete() const noexcept {
        return graphics_family.has_value() &&
               present_family.has_value();
    }
};

struct VulkanContextConfig {
    bool enable_validation_layers = true;
    std::string application_name = "GeoScatter3D";
    std::uint32_t application_version = VK_MAKE_VERSION(0, 1, 0);
    // "auto" or "uuid:<32-char-hex>"
    std::string preferred_gpu = "auto";
};

class VulkanContext {
public:
    VulkanContext(
        const gs3d::platform::Window& window,
        VulkanContextConfig config = {}
    );

    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    [[nodiscard]]
    VkInstance instance() const noexcept;

    [[nodiscard]]
    VkSurfaceKHR surface() const noexcept;

    [[nodiscard]]
    VkPhysicalDevice physical_device() const noexcept;

    [[nodiscard]]
    VkDevice device() const noexcept;

    [[nodiscard]]
    VkQueue graphics_queue() const noexcept;

    [[nodiscard]]
    VkQueue present_queue() const noexcept;

    [[nodiscard]]
    const QueueFamilyIndices& queue_family_indices() const noexcept;

    [[nodiscard]]
    std::string physical_device_name() const;

    // All enumerated GPUs (populated after construction).
    [[nodiscard]]
    const std::vector<VulkanGpuInfo>& gpu_list() const noexcept;

    // Index into gpu_list() of the actually-selected device.
    [[nodiscard]]
    std::size_t active_gpu_index() const noexcept;

    // Whether the active GPU was selected by explicit UUID match
    // (vs. auto-selection or fallback).
    [[nodiscard]]
    bool active_gpu_is_preferred() const noexcept;

    // Human-readable summary of how the GPU was chosen.
    [[nodiscard]]
    const std::string& selection_summary() const noexcept;

private:
    VulkanContextConfig config_{};

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;

    QueueFamilyIndices queue_family_indices_{};

    std::vector<VulkanGpuInfo> gpu_list_;
    std::size_t active_gpu_index_ = 0;
    bool active_gpu_is_preferred_ = false;
    std::string selection_summary_;

private:
    void create_instance();
    void create_surface(const gs3d::platform::Window& window);
    void pick_physical_device();
    void create_logical_device();

    [[nodiscard]]
    std::vector<const char*> required_instance_extensions() const;

    [[nodiscard]]
    bool validation_layers_supported() const;

    [[nodiscard]]
    QueueFamilyIndices find_queue_families(VkPhysicalDevice device) const;

    [[nodiscard]]
    static std::vector<const char*> required_device_extensions();

    [[nodiscard]]
    static bool device_extensions_supported(VkPhysicalDevice device);

    [[nodiscard]]
    static std::string device_name(VkPhysicalDevice device);
};

} // namespace gs3d::render