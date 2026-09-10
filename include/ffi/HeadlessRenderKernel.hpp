#pragma once

#include "camera/Camera.hpp"
#include "camera/CameraController.hpp"
#include "core/PointData.hpp"
#include "data/Gs3dDataset.hpp"
#include "render/OffscreenFramebuffer.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/PointPipeline.hpp"
#include "render/VulkanContext.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gs3d::ffi {

/*
 * 无窗口离屏点云渲染内核（单进程 FFI）。
 * 独占 Vulkan 对象；调用方通过互斥串行化（由 Gs3dEngine 渲染线程持有）。
 */
class HeadlessRenderKernel {
public:
    struct Config {
        std::uint32_t width = 1024;
        std::uint32_t height = 1024;
        std::string preferred_gpu = "auto";
        std::string shader_dir;  // 含 point.vert.spv / point.frag.spv；空则用默认相对路径
        bool enable_validation = false;
    };

    HeadlessRenderKernel() = default;
    ~HeadlessRenderKernel();

    HeadlessRenderKernel(const HeadlessRenderKernel&) = delete;
    HeadlessRenderKernel& operator=(const HeadlessRenderKernel&) = delete;

    // 返回 0 成功；负数见统一返回码。
    int create(const Config& config);
    void destroy() noexcept;

    int load_dataset(const std::string& path);
    void unload_dataset() noexcept;
    [[nodiscard]] bool has_dataset() const noexcept { return has_dataset_; }

    int resize(std::uint32_t width, std::uint32_t height);

    // 相机：position/target/up + 透视 FOV
    void set_camera_look_at(
        float pos_x, float pos_y, float pos_z,
        float tgt_x, float tgt_y, float tgt_z,
        float up_x, float up_y, float up_z
    );
    void reset_camera_to_bounds();
    /// 轨道/平移/缩放增量（与桌面 CameraController 同语义，同步生效）。
    int camera_input(float rotate_dx, float rotate_dy,
                     float pan_dx, float pan_dy, float scroll_y);

    void set_point_size(float size);
    void set_colormap_index(int index);
    void set_value_clip(bool enabled, float min_v, float max_v);
    void set_point_shape(int shape);  // 0-3

    // 渲染一帧离屏；out_rgba 需 w*h*4 字节。返回 0 成功。
    int render_rgba(std::uint8_t* out_rgba, std::uint32_t max_bytes,
                    std::uint32_t* out_w, std::uint32_t* out_h);

    // 将最近一帧编码为 PNG（RGBA→stb）。out 需足够大；成功写 out_len。
    int encode_last_frame_png(std::uint8_t* out, std::uint32_t max_bytes,
                              std::uint32_t* out_len);

    [[nodiscard]] const std::string& last_error() const noexcept {
        return last_error_;
    }
    [[nodiscard]] std::uint64_t point_count() const noexcept {
        return point_count_;
    }

private:
    void create_pipeline();
    void destroy_gpu_resources() noexcept;
    void update_push_from_camera();
    void set_error(const std::string& message);
    void reset_camera_to_bounds_locked();

    Config config_{};
    std::string last_error_;

    std::unique_ptr<gs3d::render::VulkanContext> context_;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;

    gs3d::render::OffscreenFramebuffer framebuffer_;
    std::unique_ptr<gs3d::render::PointPipeline> pipeline_;
    gs3d::render::PointCloudGpu cloud_;
    gs3d::camera::Camera camera_;
    gs3d::camera::CameraController camera_controller_{};
    gs3d::render::PointPushConstants push_{};

    gs3d::data::Gs3dDataset dataset_;
    bool has_dataset_ = false;
    std::uint64_t point_count_ = 0;

    // 最近一帧 RGBA（CPU 侧缓存，供 PNG 编码）
    std::vector<std::uint8_t> last_rgba_;
    std::uint32_t last_w_ = 0;
    std::uint32_t last_h_ = 0;

    mutable std::mutex mutex_;
};

}  // namespace gs3d::ffi
