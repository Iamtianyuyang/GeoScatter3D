#pragma once

#include <cstdint>

namespace gs3d::render {

/*
 * 平台层创建 surface 所需的最小回调契约。
 *
 * 形状刻意与 C ABI 完全一致（int32 返回码 + uint64 不透明句柄），因此：
 *   - 核心与 ABI 之间不需要任何适配层；
 *   - gs3d_engine_* 公开头文件里不出现 Vulkan 类型（R2 图形句柄不穿透）；
 *   - 平台适配层（Windows HWND / Android ANativeWindow）自己把平台句柄
 *     变成 VkSurfaceKHR 后只回传一个整数句柄。
 *
 * 核心只保存并销毁该句柄，不认识 HWND、ANativeWindow、NSView、GLFWwindow
 * 或任何 Flutter 类型。user_data 的所有权始终属于平台调用方，并且必须至少
 * 活到 create_surface 回调返回。
 *
 * 返回 0 表示成功；非 0 由平台适配层自定义（核心只负责转成可读错误）。
 */
struct VulkanSurfaceProvider {
    using CreateSurfaceFn = std::int32_t (*)(
        void* instance,
        void* user_data,
        std::uint64_t* out_surface_handle
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
