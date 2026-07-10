#include "render/VulkanGpuInfo.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace gs3d::render {

namespace {

constexpr const char* kRequiredDeviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
};

bool has_required_extensions(VkPhysicalDevice device)
{
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(
        device, nullptr, &count, available.data());

    for (const char* required : kRequiredDeviceExtensions) {
        bool found = false;
        for (const auto& ext : available) {
            if (std::strcmp(ext.extensionName, required) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

bool has_graphics_and_present_queue(
    VkPhysicalDevice device,
    VkSurfaceKHR surface)
{
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(
        device, &count, families.data());

    bool graphics = false;
    bool present = false;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics = true;
        }
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(
            device, i, surface, &supported);
        if (supported == VK_TRUE) {
            present = true;
        }
        if (graphics && present) {
            return true;
        }
    }
    return false;
}

bool has_swapchain_support(
    VkPhysicalDevice device,
    VkSurfaceKHR surface)
{
    std::uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        device, surface, &format_count, nullptr);
    std::uint32_t present_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device, surface, &present_count, nullptr);
    return format_count > 0 && present_count > 0;
}

} // namespace

const char* gpu_type_label(VkPhysicalDeviceType type) noexcept
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "独立显卡";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "集成显卡";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "虚拟显卡";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "软件设备";
    default:
        return "其他设备";
    }
}

std::string device_uuid_from_properties(
    const VkPhysicalDeviceIDProperties& id)
{
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (std::uint32_t i = 0; i < VK_UUID_SIZE; ++i) {
        ss << std::setw(2)
           << static_cast<int>(id.deviceUUID[i]);
    }
    return ss.str();
}

std::string device_uuid_from_hex(const std::string& hex)
{
    // Must be exactly 32 hex chars (VK_UUID_SIZE * 2).
    if (hex.size() != VK_UUID_SIZE * 2) {
        return {};
    }
    std::string result;
    result.reserve(VK_UUID_SIZE);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        char high = static_cast<char>(std::tolower(
            static_cast<unsigned char>(hex[i])));
        char low = static_cast<char>(std::tolower(
            static_cast<unsigned char>(hex[i + 1])));
        if (!std::isxdigit(static_cast<unsigned char>(high)) ||
            !std::isxdigit(static_cast<unsigned char>(low))) {
            return {};
        }
        auto hex_to_nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            return c - 'a' + 10;
        };
        result.push_back(static_cast<char>(
            (hex_to_nibble(high) << 4) | hex_to_nibble(low)));
    }
    if (result.size() != VK_UUID_SIZE) {
        return {};
    }
    return result;
}

std::string normalise_device_uuid(std::string raw)
{
    // Remove whitespace
    raw.erase(
        std::remove_if(raw.begin(), raw.end(),
                       [](unsigned char c) {
                           return std::isspace(c);
                       }),
        raw.end());
    // Lowercase
    std::transform(raw.begin(), raw.end(), raw.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(
                           std::tolower(c));
                   });
    // Must be 32 hex chars
    if (raw.size() != VK_UUID_SIZE * 2) {
        return {};
    }
    for (char c : raw) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return {};
        }
    }
    return raw;
}

std::vector<VulkanGpuInfo> enumerate_gpus(
    VkInstance instance,
    VkSurfaceKHR surface)
{
    std::uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);

    if (device_count == 0) {
        return {};
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

    std::vector<VulkanGpuInfo> result;
    result.reserve(device_count);

    for (const auto device : devices) {
        VulkanGpuInfo info;
        info.physical_device = device;

        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

        VkPhysicalDeviceIDProperties id_props{};
        id_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

        props2.pNext = &id_props;
        vkGetPhysicalDeviceProperties2(device, &props2);

        info.name = props2.properties.deviceName;
        info.device_uuid = device_uuid_from_properties(id_props);
        info.device_type = props2.properties.deviceType;
        info.vendor_id = props2.properties.vendorID;
        info.device_id = props2.properties.deviceID;
        info.api_version = props2.properties.apiVersion;
        info.driver_version = props2.properties.driverVersion;

        // Sum device-local heap sizes.
        VkPhysicalDeviceMemoryProperties mem_props{};
        vkGetPhysicalDeviceMemoryProperties(device, &mem_props);
        for (std::uint32_t i = 0; i < mem_props.memoryHeapCount; ++i) {
            if (mem_props.memoryHeaps[i].flags &
                VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                info.device_local_memory_bytes +=
                    mem_props.memoryHeaps[i].size;
            }
        }

        // Duplicate ICD manifests can surface the same physical device
        // twice (identical UUID, name, and driver).  Keep the first.
        const bool duplicate = std::any_of(
            result.begin(), result.end(),
            [&info](const VulkanGpuInfo& existing) {
                return existing.device_uuid == info.device_uuid &&
                       existing.name == info.name &&
                       existing.driver_version == info.driver_version;
            });
        if (duplicate) {
            continue;
        }

        // Suitability check
        info.suitable = true;
        if (props2.properties.apiVersion < VK_API_VERSION_1_2) {
            info.suitable = false;
            info.rejection_reason = "需要 Vulkan 1.2 或更高版本";
        } else if (!has_graphics_and_present_queue(device, surface)) {
            info.suitable = false;
            info.rejection_reason = "缺少图形或呈现队列支持";
        } else if (!has_required_extensions(device)) {
            info.suitable = false;
            info.rejection_reason = "缺少所需设备扩展 (swapchain, dynamic rendering)";
        } else if (!has_swapchain_support(device, surface)) {
            info.suitable = false;
            info.rejection_reason = "交换链格式或呈现模式不兼容";
        }

        result.push_back(std::move(info));
    }

    return result;
}

std::optional<std::size_t> auto_select_gpu(
    const std::vector<VulkanGpuInfo>& gpus)
{
    // Priority score: discrete=4, integrated=3, virtual=2, cpu=1
    auto score = [](VkPhysicalDeviceType type) -> int {
        switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return 4;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return 3;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return 2;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return 1;
        default:
            return 0;
        }
    };

    std::optional<std::size_t> best_index;
    int best_score = -1;
    std::uint64_t best_memory = 0;

    for (std::size_t i = 0; i < gpus.size(); ++i) {
        if (!gpus[i].suitable) {
            continue;
        }
        const int s = score(gpus[i].device_type);
        if (s > best_score ||
            (s == best_score &&
             gpus[i].device_local_memory_bytes > best_memory)) {
            best_score = s;
            best_memory = gpus[i].device_local_memory_bytes;
            best_index = i;
        }
    }

    return best_index;
}

GpuSelectionResult select_gpu(
    const std::vector<VulkanGpuInfo>& gpus,
    const std::string& preferred_gpu)
{
    GpuSelectionResult result;

    if (preferred_gpu.empty() || preferred_gpu == "auto") {
        result.index = auto_select_gpu(gpus);
        return result;
    }

    // "uuid:<hex>" format
    constexpr std::string_view kUuidPrefix = "uuid:";
    if (!preferred_gpu.starts_with(kUuidPrefix)) {
        result.fallback_to_auto = true;
        result.fallback_reason =
            "无效的 preferred_gpu 格式，应为 auto 或 uuid:<hex>";
        result.index = auto_select_gpu(gpus);
        return result;
    }

    const std::string hex =
        std::string(preferred_gpu.substr(kUuidPrefix.size()));
    const std::string normalised = normalise_device_uuid(hex);
    if (normalised.empty()) {
        result.fallback_to_auto = true;
        result.fallback_reason =
            "preferred_gpu UUID 格式无效";
        result.index = auto_select_gpu(gpus);
        return result;
    }

    const std::string target = device_uuid_from_hex(normalised);

    for (std::size_t i = 0; i < gpus.size(); ++i) {
        const std::string candidate =
            device_uuid_from_hex(gpus[i].device_uuid);
        if (candidate == target) {
            if (!gpus[i].suitable) {
                result.fallback_to_auto = true;
                result.fallback_reason =
                    "首选 GPU 不满足需求: " +
                    gpus[i].rejection_reason;
                result.index = auto_select_gpu(gpus);
                return result;
            }
            result.index = i;
            return result;
        }
    }

    // UUID not found
    result.fallback_to_auto = true;
    result.fallback_reason = "首选 GPU 未找到 (UUID 不匹配)";
    result.index = auto_select_gpu(gpus);
    return result;
}

} // namespace gs3d::render
