#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gs3d::render {

/*
 * Information about an enumerated Vulkan physical device,
 * including its hardware UUID for stable identification across
 * reboots and driver updates.
 */
struct VulkanGpuInfo {
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;

    std::string name;
    // 32 hex chars (VK_UUID_SIZE == 16 bytes).
    std::string device_uuid;

    VkPhysicalDeviceType device_type =
        VK_PHYSICAL_DEVICE_TYPE_OTHER;

    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t api_version = 0;
    std::uint32_t driver_version = 0;

    // Heap memory that is both device-local and visible to the host
    // is counted here (conservative lower bound on VRAM).
    std::uint64_t device_local_memory_bytes = 0;

    bool suitable = false;
    std::string rejection_reason;
};

/*
 * Readable label for VkPhysicalDeviceType.
 */
[[nodiscard]]
const char* gpu_type_label(VkPhysicalDeviceType type) noexcept;

/*
 * UUID ↔ hex string conversion.  device_uuid_from_properties reads
 * VkPhysicalDeviceIDProperties::deviceUUID (16 bytes) and returns a
 * 32-char lowercase hex string.  device_uuid_from_hex parses the
 * inverse.
 */
[[nodiscard]]
std::string device_uuid_from_properties(
    const VkPhysicalDeviceIDProperties& id
);

[[nodiscard]]
std::string device_uuid_from_hex(
    const std::string& hex
);

/*
 * Normalise a user-supplied UUID string: lowercase, strip whitespace,
 * reject non-hex characters.  Returns empty string on invalid input.
 */
[[nodiscard]]
std::string normalise_device_uuid(std::string raw);

/*
 * Enumerate all physical devices with suitability checks against
 * the given surface.  The result vector is ordered by the driver's
 * enumeration order.
 */
[[nodiscard]]
std::vector<VulkanGpuInfo> enumerate_gpus(
    VkInstance instance,
    VkSurfaceKHR surface
);

/*
 * Auto-select the best suitable GPU.  Prefers discrete > integrated >
 * virtual > CPU, breaking ties by device-local memory size.
 *
 * Returns the index into |gpus|, or std::nullopt if no suitable
 * device exists.
 */
[[nodiscard]]
std::optional<std::size_t> auto_select_gpu(
    const std::vector<VulkanGpuInfo>& gpus
);

/*
 * Select a GPU by preferred specification:
 *   "auto"              → auto_select_gpu
 *   "uuid:<hex>"        → exact UUID match (must be suitable)
 *
 * Returns the index into |gpus|, or std::nullopt with a
 * rejection_reason if the preferred device is unavailable — the
 * caller should fall back to auto_select_gpu.
 */
struct GpuSelectionResult {
    std::optional<std::size_t> index;
    bool fallback_to_auto = false;
    std::string fallback_reason;
};

[[nodiscard]]
GpuSelectionResult select_gpu(
    const std::vector<VulkanGpuInfo>& gpus,
    const std::string& preferred_gpu
);

} // namespace gs3d::render
