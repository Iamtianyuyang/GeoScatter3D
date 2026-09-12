#pragma once

#include "render/VulkanGpuInfo.hpp"
#include "render/VulkanSurface.hpp"

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
    // true 时创建无 surface 的离屏上下文；false 时必须提供
    // surface_provider，不能静默退回离屏模式。
    bool headless = false;
    VulkanSurfaceProvider surface_provider{};
};

class VulkanContext {
public:
    // 无 surface 的离屏上下文（单进程 FFI 内核 / 多端）。
    explicit VulkanContext(VulkanContextConfig config);

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
    bool headless() const noexcept;

    [[nodiscard]]
    VkPhysicalDevice physical_device() const noexcept;

    [[nodiscard]]
    bool independent_blend_supported() const noexcept;

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

    [[nodiscard]]
    const std::vector<VulkanGpuInfo>& gpu_list() const noexcept;

    [[nodiscard]]
    std::size_t active_gpu_index() const noexcept;

    [[nodiscard]]
    bool active_gpu_is_preferred() const noexcept;

    [[nodiscard]]
    const std::string& selection_summary() const noexcept;

private:
    VulkanContextConfig config_{};

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    VkPhysicalDeviceFeatures supported_features_{};

    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;

    QueueFamilyIndices queue_family_indices_{};

    std::vector<VulkanGpuInfo> gpu_list_;
    std::size_t active_gpu_index_ = 0;
    bool active_gpu_is_preferred_ = false;
    std::string selection_summary_;

private:
    void create_instance();
    void create_surface();
    void pick_physical_device();
    void create_logical_device();

    void cleanup() noexcept;

    [[nodiscard]]
    std::vector<const char*> required_instance_extensions() const;

    [[nodiscard]]
    bool validation_layers_supported() const;

    [[nodiscard]]
    QueueFamilyIndices find_queue_families(VkPhysicalDevice device) const;

    [[nodiscard]]
    static std::vector<const char*> required_device_extensions(bool headless);

    [[nodiscard]]
    bool device_extensions_supported(VkPhysicalDevice device) const;

    [[nodiscard]]
    static std::string device_name(VkPhysicalDevice device);
};

} // namespace gs3d::render
