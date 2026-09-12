#include "render/VulkanContext.hpp"
#include "util/Log.hpp"

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

std::uint32_t negotiate_vulkan_instance_api_version() {
    // vkEnumerateInstanceVersion 是 Vulkan 1.1 的 loader 入口；通过
    // vkGetInstanceProcAddr 获取，避免依赖平台直接导出该符号。
    std::uint32_t loader_api_version = VK_API_VERSION_1_0;
    const auto enumerate_instance_version =
        reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            vkGetInstanceProcAddr(
                VK_NULL_HANDLE,
                "vkEnumerateInstanceVersion"
            )
        );

    if (enumerate_instance_version != nullptr) {
        const VkResult result =
            enumerate_instance_version(&loader_api_version);
        if (result != VK_SUCCESS) {
            throw std::runtime_error(
                "VulkanContext: failed to query Vulkan loader version"
            );
        }
    }

    if (loader_api_version < kMinimumVulkanApiVersion) {
        throw std::runtime_error(
            std::string("VulkanContext: loader does not satisfy ") +
            kVulkanProfileName
        );
    }

    // Profile 是最低稳定契约；不向 loader 请求高于当前平台 Profile 的
    // 版本，避免 Android 驱动/系统组合因过高 apiVersion 创建失败。
    return std::min(loader_api_version, kMinimumVulkanApiVersion);
}

VulkanContext::VulkanContext(VulkanContextConfig config)
    : config_(std::move(config))
{
#ifndef NDEBUG
    // Debug 构建保留调用方的 validation 请求。
#else
    config_.enable_validation_layers = false;
#endif

    if (!config_.headless && !config_.surface_provider.valid()) {
        throw std::invalid_argument(
            "VulkanContext: a surface provider is required for a "
            "non-headless context"
        );
    }

    try {
        create_instance();
        if (!config_.headless) {
            create_surface();
        }
        pick_physical_device();
        create_logical_device();
    } catch (...) {
        // 构造函数抛异常时析构函数不会运行，因此必须在这里回收已创建的
        // instance/surface/device，避免设备选择或 surface 创建失败泄漏。
        cleanup();
        throw;
    }
}

VulkanContext::~VulkanContext() {
    cleanup();
}

void VulkanContext::cleanup() noexcept {
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

bool VulkanContext::headless() const noexcept {
    return config_.headless;
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
    app_info.apiVersion = negotiate_vulkan_instance_api_version();

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

void VulkanContext::create_surface() {
    if (config_.headless) {
        return;
    }

    const auto& provider = config_.surface_provider;
    if (!provider.valid()) {
        throw std::logic_error(
            "VulkanContext: cannot create a surface without a provider"
        );
    }

    std::uint64_t surface_handle = 0;
    const std::int32_t create_result = provider.create_surface(
        reinterpret_cast<void*>(instance_),
        provider.user_data,
        &surface_handle
    );
    if (create_result != 0) {
        throw std::runtime_error(
            "VulkanContext: platform failed to create Vulkan surface (code " +
            std::to_string(create_result) + ")"
        );
    }
    // 整个内核里唯一一处 uint64 → VkSurfaceKHR 的转换：平台句柄不穿透 ABI。
    surface_ = reinterpret_cast<VkSurfaceKHR>(
        static_cast<std::uintptr_t>(surface_handle)
    );

    if (surface_ == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "VulkanContext: platform returned a null Vulkan surface"
        );
    }
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
        std::string err = "VulkanContext: no suitable physical device found (found " +
                          std::to_string(gpu_list_.size()) + " GPUs: ";
        for (const auto& g : gpu_list_) {
            err += "[" + g.name + " (" + g.rejection_reason + ")] ";
        }
        err += ")";
        throw std::runtime_error(err);
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

    // 仅在非 headless（即使用 VulkanRenderer 交换链渲染）时启用 dynamic rendering。
    // 离屏渲染（HeadlessRenderKernel）通过 OffscreenFramebuffer + VkRenderPass 工作，
    // 无需也不依赖 VK_KHR_dynamic_rendering，兼容 Android 移动端与模拟器。
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_features{};
    dynamic_rendering_features.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    dynamic_rendering_features.dynamicRendering = VK_TRUE;

    const auto device_extensions =
        required_device_extensions(physical_device_, config_.headless);

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    if (!config_.headless) {
        create_info.pNext = &dynamic_rendering_features;
    }

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
    if (config_.headless) {
        // 离屏内核：不依赖窗口系统扩展。
        return {};
    }

    std::vector<const char*> extensions;
    extensions.reserve(
        1 + config_.surface_provider.instance_extension_count
    );
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);

    const auto& provider = config_.surface_provider;
    if (provider.instance_extension_count > 0 &&
        provider.instance_extensions == nullptr) {
        throw std::invalid_argument(
            "VulkanContext: surface extension array is null"
        );
    }

    for (std::uint32_t i = 0;
         i < provider.instance_extension_count;
         ++i) {
        const char* extension = provider.instance_extensions[i];
        if (extension == nullptr) {
            throw std::invalid_argument(
                "VulkanContext: surface extension name is null"
            );
        }

        const bool duplicate = std::any_of(
            extensions.begin(),
            extensions.end(),
            [extension](const char* existing) {
                return std::strcmp(existing, extension) == 0;
            }
        );
        if (!duplicate) {
            extensions.push_back(extension);
        }
    }

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
            if (config_.headless || surface_ == VK_NULL_HANDLE) {
                // 无 surface：呈现队列与图形队列共用。
                indices.present_family = i;
            }
        }

        if (!config_.headless && surface_ != VK_NULL_HANDLE) {
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
        }

        if (indices.complete()) {
            break;
        }
    }

    return indices;
}

bool VulkanContext::device_advertises_extension(
    VkPhysicalDevice device, const char* name) {
    if (device == VK_NULL_HANDLE || name == nullptr) {
        return false;
    }
    std::uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) !=
        VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> available(count);
    if (vkEnumerateDeviceExtensionProperties(
            device, nullptr, &count, available.data()) != VK_SUCCESS) {
        return false;
    }
    return std::any_of(
        available.begin(), available.end(),
        [name](const VkExtensionProperties& extension) {
            return std::strcmp(extension.extensionName, name) == 0;
        });
}

std::vector<const char*> VulkanContext::required_device_extensions(
    VkPhysicalDevice device, bool headless) {
    if (headless) {
        return {};
    }

    // 窗口呈现必然需要 swapchain。
    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    /*
     * VK_KHR_dynamic_rendering 在 Vulkan 1.3 被提升为核心，驱动可以选择不再
     * advertise 该扩展名（真机 Adreno 650 即如此）。因此这里按「设备是否声称
     * 支持」来启用：声称就启用扩展（1.1/1.2 设备必须如此），否则依赖 1.3 的
     * 核心功能（create_device 中通过 VkPhysicalDeviceVulkan13Features 打开）。
     * 旧 VulkanRenderer 的 vkCmdBeginRenderingKHR 也走同一条规则。
     */
    if (device_advertises_extension(device, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    }
    return extensions;
}

bool VulkanContext::device_extensions_supported(
    VkPhysicalDevice device
) const {
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

    for (const auto* extension :
         required_device_extensions(device, config_.headless)) {
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
