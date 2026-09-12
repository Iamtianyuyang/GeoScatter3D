#pragma once

#include "render/VulkanContext.hpp"
#include "render/VulkanDepthBuffer.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace gs3d::render {

/*
 * 平台无关的「呈现目标」（零拷贝纹理通路的地基）。
 *
 * 与 VulkanSwapchain 的区别：后者绑定 gs3d::platform::Window（GLFW），
 * 只能给桌面旧 Viewer 用；本类只吃 VulkanContext 已经建好的 VkSurfaceKHR
 * —— 该 surface 由平台适配层经 VulkanSurfaceProvider 注入（Android 侧是
 * ANativeWindow，Windows 侧是 HWND）。核心因此不认识任何窗口句柄类型，
 * 也不依赖 GLFW。
 *
 * 职责：交换链生命周期（格式/呈现模式/尺寸、out-of-date 重建）、颜色与深度
 * 附件、渲染通道与 framebuffer、每帧 acquire/present。
 * 不负责：点云管线（用 render_pass() 自行创建 PointPipeline 即可）。
 */
class PresentSurface {
public:
    struct Config {
        /// 期望尺寸；0 表示完全跟随 surface 当前 extent。
        std::uint32_t requested_width = 0;
        std::uint32_t requested_height = 0;
        /// true = 优先 FIFO（省电、无撕裂），false = 优先 MAILBOX/IMMEDIATE。
        bool vsync = true;
        /// 与 OffscreenFramebuffer 的约定一致：颜色附件外部按只读使用。
        VkFormat preferred_format = VK_FORMAT_B8G8R8A8_UNORM;
    };

    // 注意：Config 是嵌套类型，默认实参 `= {}` 会在 GCC/Clang 下因
    // 「default member initializer 在类定义内尚不完整」而编译失败
    // （MSVC 容忍这种扩展），因此这里不给默认实参，调用方显式传 Config。
    PresentSurface(const VulkanContext& context, Config config);
    ~PresentSurface();

    PresentSurface(const PresentSurface&) = delete;
    PresentSurface& operator=(const PresentSurface&) = delete;

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] VkExtent2D extent() const noexcept {
        return extent_;
    }

    [[nodiscard]] VkFormat format() const noexcept {
        return format_;
    }

    [[nodiscard]] VkPresentModeKHR present_mode() const noexcept {
        return present_mode_;
    }

    /// 供 PointPipeline 构造函数使用（颜色 + 深度，无 pick 附件）。
    [[nodiscard]] VkRenderPass render_pass() const noexcept {
        return render_pass_;
    }

    [[nodiscard]] std::uint32_t image_count() const noexcept {
        return static_cast<std::uint32_t>(images_.size());
    }

    struct Frame {
        VkImage image = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkExtent2D extent{};
        std::uint32_t image_index = 0;
        bool acquired = false;
    };

    /*
     * 取一帧用于绘制。返回 false 有两种含义：
     *   a) 交换链刚被重建（out-of-date），调用方应跳过本帧稍后重试；
     *   b) 夹在 acquire 与 present 之间的极端情况 —— 调用方看 needs_recreate()。
     * 绘制内容应由调用方在 frame 的 render pass 内录制到给定 command buffer。
     */
    [[nodiscard]] bool begin_frame(Frame& frame);

    /// 提交并呈递该帧；内部处理 SUBOPTIMAL/OUT_OF_DATE 并置位 needs_recreate()。
    [[nodiscard]] bool end_frame(const Frame& frame);

    [[nodiscard]] bool needs_recreate() const noexcept {
        return needs_recreate_;
    }

    /// 按 surface 最新能力重建交换链（尺寸/格式/呈现模式可能变化）。
    void recreate();

    /*
     * 把 frame 的颜色附件拷回 host（截图与自动化回归用；必须在 present 前调用）。
     * 会自行转换 image layout 并在结束后恢复为可呈现布局。
     */
    [[nodiscard]] bool copy_frame_to_host(
        const Frame& frame,
        std::vector<std::uint8_t>& out_rgba
    );

private:
    void create_swapchain();
    void create_render_pass();
    void create_depth();
    void create_framebuffers();
    void destroy_swapchain_objects() noexcept;
    void destroy_render_objects() noexcept;

    [[nodiscard]] VkSurfaceFormatKHR choose_surface_format() const;
    [[nodiscard]] VkPresentModeKHR choose_present_mode() const;
    [[nodiscard]] VkExtent2D choose_extent() const;

    const VulkanContext* context_ = nullptr;
    Config config_{};

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR color_space_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D extent_{};

    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;
    std::vector<VkFramebuffer> framebuffers_;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VulkanDepthBuffer depth_{};
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer copy_command_ = VK_NULL_HANDLE;
    VkFence copy_fence_ = VK_NULL_HANDLE;

    bool needs_recreate_ = false;
};

}  // namespace gs3d::render
