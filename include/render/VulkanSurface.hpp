#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace gs3d::render {

// 平台层创建 VkSurfaceKHR 所需的最小回调契约。
//
// 核心只保存并销毁 VkSurfaceKHR，不认识 HWND、ANativeWindow、NSView、
// GLFWwindow 或任何 Flutter 类型。user_data 的所有权始终属于平台调用方，
// 并且必须至少活到 create_surface 回调返回。
struct VulkanSurfaceProvider {
    using CreateSurfaceFn = VkResult (*)(
        VkInstance instance,
        void* user_data,
        VkSurfaceKHR* out_surface
    );

    // 只借用这些字符串；它们只需活到 VulkanContext 构造完成。
    const char* const* instance_extensions = nullptr;
    std::uint32_t instance_extension_count = 0;

    CreateSurfaceFn create_surface = nullptr;
    void* user_data = nullptr;

    [[nodiscard]]
    bool valid() const noexcept {
        return create_surface != nullptr;
    }
};

} // namespace gs3d::render
