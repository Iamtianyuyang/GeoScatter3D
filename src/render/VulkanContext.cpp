#include "render/VulkanContext.hpp"
#include "util/Log.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

namespace gs3d::render {

namespace {

constexpr const char* VALIDATION_LAYER_NAME =
    "VK_LAYER_KHRONOS_validation";

void check_vk(VkResult result, const char* message) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(message);
    }
}

} // namespace

VulkanContext::VulkanContext(
    const gs3d::platform::Window& window,
    VulkanContextConfig config
)
    : config_(std::move(config))
{
#ifndef NDEBUG
    config_.enable_validation_layers = config_.enable_validation_layers;
#else
    config_.enable_validation_layers = false;
#endif

    create_instance();
    create_surface(window);
    pick_physical_device();
    create_logical_device();
}

VulkanContext::~VulkanContext() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

VkInstance VulkanContext::instance() const noexcept {
    return instance_;
}

VkSurfaceKHR VulkanContext::surface() const noexcept {
    return surface_;
}

VkPhysicalDevice VulkanContext::physical_device() const noexcept {
    return physical_device_;
}

bool VulkanContext::independent_blend_supported() const noexcept {
    return supported_features_.independentBlend == VK_TRUE;
}

VkDevice VulkanContext::device() const noexcept {
    return device_;
}

VkQueue VulkanContext::graphics_queue() const noexcept {
    return graphics_queue_;
}

VkQueue VulkanContext::present_queue() const noexcept {
    return present_queue_;
}

const QueueFamilyIndices& VulkanContext::queue_family_indices() const noexcept {
    return queue_family_indices_;
}

std::string VulkanContext::physical_device_name() const {
    if (physical_device_ == VK_NULL_HANDLE) {
        return {};
    }

    return device_name(physical_device_);
}

void VulkanContext::create_instance() {
    if (config_.enable_validation_layers && !validation_layers_supported()) {
        gs3d::util::log::error()
            << "[WARN] VulkanContext: validation layers requested but not "
               "available, disabling validation layers.\n";
        config_.enable_validation_layers = false;
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = config_.application_name.c_str();
    app_info.applicationVersion = config_.application_version;
    app_info.pEngineName = "GeoScatter3D";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;

    const auto extensions = required_instance_extensions();

    std::vector<const char*> layers;
    if (config_.enable_validation_layers) {
        layers.push_back(VALIDATION_LAYER_NAME);
    }

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;

    create_info.enabledExtensionCount =
        static_cast<std::uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    create_info.enabledLayerCount =
        static_cast<std::uint32_t>(layers.size());
    create_info.ppEnabledLayerNames =
        layers.empty() ? nullptr : layers.data();

    check_vk(
        vkCreateInstance(&create_info, nullptr, &instance_),
        "VulkanContext: failed to create Vulkan instance"
    );
}

void VulkanContext::create_surface(
    const gs3d::platform::Window& window
) {
    check_vk(
        glfwCreateWindowSurface(
            instance_,
            window.native_handle(),
            nullptr,
            &surface_
        ),
        "VulkanContext: failed to create GLFW Vulkan surface"
    );
}

void VulkanContext::pick_physical_device() {
    gpu_list_ = enumerate_gpus(instance_, surface_);

    if (gpu_list_.empty()) {
        throw std::runtime_error(
            "VulkanContext: no Vulkan-capable physical device found"
        );
    }

    const auto selection = select_gpu(gpu_list_, config_.preferred_gpu);

    if (!selection.index.has_value()) {
        throw std::runtime_error(
            "VulkanContext: no suitable physical device found"
        );
    }

    active_gpu_index_ = *selection.index;
    physical_device_ = gpu_list_[active_gpu_index_].physical_device;
    queue_family_indices_ = find_queue_families(physical_device_);

    // 缓存物理设备 feature 支持情况，供 create_logical_device 按支持度
    // 决定启用哪些 feature（禁止启用设备不支持的 feature）。
    vkGetPhysicalDeviceFeatures(physical_device_, &supported_features_);

    // Build selection summary for logging and UI.
    const auto& gpu = gpu_list_[active_gpu_index_];
    if (selection.fallback_to_auto) {
        selection_summary_ = "auto (回退: " + selection.fallback_reason + ")";
        active_gpu_is_preferred_ = false;
    } else if (config_.preferred_gpu == "auto" ||
               config_.preferred_gpu.empty()) {
        selection_summary_ = "auto";
        active_gpu_is_preferred_ = false;
    } else {
        selection_summary_ = "preferred";
        active_gpu_is_preferred_ = true;
    }

    gs3d::util::log::error()
        << "[VULKAN] Selected GPU:\n"
        << "  name=" << gpu.name << '\n'
        << "  uuid=" << gpu.device_uuid << '\n'
        << "  type=" << gpu_type_label(gpu.device_type) << '\n'
        << "  memory=" << (gpu.device_local_memory_bytes / (1024.0 * 1024.0 * 1024.0)) << " GiB\n"
        << "  selection=" << selection_summary_ << '\n';

    if (selection.fallback_to_auto) {
        gs3d::util::log::error()
            << "[VULKAN] Preferred GPU unavailable:\n"
            << "  preferred=" << config_.preferred_gpu << '\n'
            << "  reason=" << selection.fallback_reason << '\n'
            << "  falling back to automatic selection\n";
    }
}

const std::vector<VulkanGpuInfo>& VulkanContext::gpu_list() const noexcept {
    return gpu_list_;
}

std::size_t VulkanContext::active_gpu_index() const noexcept {
    return active_gpu_index_;
}

bool VulkanContext::active_gpu_is_preferred() const noexcept {
    return active_gpu_is_preferred_;
}

const std::string& VulkanContext::selection_summary() const noexcept {
    return selection_summary_;
}

void VulkanContext::create_logical_device() {
    if (!queue_family_indices_.complete()) {
        throw std::runtime_error(
            "VulkanContext: queue family indices are incomplete"
        );
    }

    std::set<std::uint32_t> unique_queue_families = {
        queue_family_indices_.graphics_family.value(),
        queue_family_indices_.present_family.value()
    };

    constexpr float queue_priority = 1.0f;

    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    queue_create_infos.reserve(unique_queue_families.size());

    for (const auto queue_family : unique_queue_families) {
        VkDeviceQueueCreateInfo queue_create_info{};
        queue_create_info.sType =
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_info.queueFamilyIndex = queue_family;
        queue_create_info.queueCount = 1;
        queue_create_info.pQueuePriorities = &queue_priority;

        queue_create_infos.push_back(queue_create_info);
    }

    VkPhysicalDeviceFeatures device_features{};

    // S2 修复（TIA-91）：PointPipeline 的 3 个 color attachment 使用不同
    // colorWriteMask（RGBA / R / R），必须启用 independentBlend，否则违反
    // VUID-VkPipelineColorBlendStateCreateInfo-pAttachments-00605。
    // 仅在物理设备支持时启用（启用不支持的 feature 同样违规/创建失败）；
    // 不支持时由 PointPipeline 回退为统一 colorWriteMask。
    device_features.independentBlend =
        supported_features_.independentBlend;

    // ImGui 多视口副窗口与主窗口共用 dynamic rendering 管线，
    // 由此副窗口交换链能继承主交换链的 sRGB 格式（配色一致）。
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_features{};
    dynamic_rendering_features.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    dynamic_rendering_features.dynamicRendering = VK_TRUE;

    const auto device_extensions = required_device_extensions();

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.pNext = &dynamic_rendering_features;

    create_info.queueCreateInfoCount =
        static_cast<std::uint32_t>(queue_create_infos.size());
    create_info.pQueueCreateInfos = queue_create_infos.data();

    create_info.pEnabledFeatures = &device_features;

    create_info.enabledExtensionCount =
        static_cast<std::uint32_t>(device_extensions.size());
    create_info.ppEnabledExtensionNames = device_extensions.data();

    check_vk(
        vkCreateDevice(physical_device_, &create_info, nullptr, &device_),
        "VulkanContext: failed to create logical device"
    );

    vkGetDeviceQueue(
        device_,
        queue_family_indices_.graphics_family.value(),
        0,
        &graphics_queue_
    );

    vkGetDeviceQueue(
        device_,
        queue_family_indices_.present_family.value(),
        0,
        &present_queue_
    );
}

std::vector<const char*> VulkanContext::required_instance_extensions() const {
    std::uint32_t glfw_extension_count = 0;
    const char** glfw_extensions =
        glfwGetRequiredInstanceExtensions(&glfw_extension_count);

    if (!glfw_extensions || glfw_extension_count == 0) {
        throw std::runtime_error(
            "VulkanContext: GLFW did not return required Vulkan extensions"
        );
    }

    std::vector<const char*> extensions(
        glfw_extensions,
        glfw_extensions + glfw_extension_count
    );

    return extensions;
}

bool VulkanContext::validation_layers_supported() const {
    std::uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(
        &layer_count,
        available_layers.data()
    );

    for (const auto& layer : available_layers) {
        if (std::strcmp(layer.layerName, VALIDATION_LAYER_NAME) == 0) {
            return true;
        }
    }

    return false;
}

QueueFamilyIndices VulkanContext::find_queue_families(
    VkPhysicalDevice device
) const {
    QueueFamilyIndices indices;

    std::uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        device,
        &queue_family_count,
        nullptr
    );

    std::vector<VkQueueFamilyProperties> queue_families(
        queue_family_count
    );

    vkGetPhysicalDeviceQueueFamilyProperties(
        device,
        &queue_family_count,
        queue_families.data()
    );

    for (std::uint32_t i = 0; i < queue_family_count; ++i) {
        const auto& queue_family = queue_families[i];

        if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics_family = i;
        }

        VkBool32 present_supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(
            device,
            i,
            surface_,
            &present_supported
        );

        if (present_supported == VK_TRUE) {
            indices.present_family = i;
        }

        if (indices.complete()) {
            break;
        }
    }

    return indices;
}

std::vector<const char*> VulkanContext::required_device_extensions() {
    return {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
    };
}

bool VulkanContext::device_extensions_supported(
    VkPhysicalDevice device
) {
    std::uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(
        device,
        nullptr,
        &extension_count,
        nullptr
    );

    std::vector<VkExtensionProperties> available_extensions(
        extension_count
    );

    vkEnumerateDeviceExtensionProperties(
        device,
        nullptr,
        &extension_count,
        available_extensions.data()
    );

    std::set<std::string> required_extensions;

    for (const auto* extension : required_device_extensions()) {
        required_extensions.insert(extension);
    }

    for (const auto& extension : available_extensions) {
        required_extensions.erase(extension.extensionName);
    }

    return required_extensions.empty();
}

std::string VulkanContext::device_name(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);

    return properties.deviceName;
}

} // namespace gs3d::render
