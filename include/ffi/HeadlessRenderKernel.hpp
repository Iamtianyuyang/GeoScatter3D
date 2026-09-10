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
    /// height_source/color_source: 0=Z, 1=Value（与 AttrPhysicalSource 一致）
    void set_height_source(int source);
    void set_color_source(int source);
    /// 高度夸张倍率（写入 push.height_mult）
    void set_height_exaggeration(float mult);
    /// 离屏清屏色 RGBA [0,1]
    void set_background(float r, float g, float b, float a = 1.0f);
    /// 将当前数据集导出为 ply/csv；format 为 "ply" 或 "csv"
    int export_dataset(const std::string& path, const std::string& format);

    // 渲染一帧离屏；out_rgba 需 w*h*4 字节。返回 0 成功。
    int render_rgba(std::uint8_t* out_rgba, std::uint32_t max_bytes,
                    std::uint32_t* out_w, std::uint32_t* out_h);

    // 将最近一帧编码为 PNG（RGBA→内置编码）。out 需足够大；成功写 out_len。
    int encode_last_frame_png(std::uint8_t* out, std::uint32_t max_bytes,
                              std::uint32_t* out_len);

    /// 屏幕坐标拾取：返回 1 命中、0 未命中、负错误。out_point_xyzv 为 [x,y,z,value]。
    int pick_point(float screen_x, float screen_y, float tolerance_px,
                   float* out_point_xyzv);

    /// Gizmo 预设轴 0..5：-X/+X/-Y/+Y/-Z/+Z。
    int gizmo_axis(int axis);

    /// 软复位：清空最近帧缓存并重建 GPU 点云上传。
    int clear_tile_cache();

    struct CameraState {
        float azimuth_deg = 0.0f;
        float elevation_deg = 0.0f;
        float zoom = 1.0f;
        float pan[2] = {0.0f, 0.0f};
        float position[3] = {0.0f, 0.0f, 0.0f};
        float target[3] = {0.0f, 0.0f, 0.0f};
        float distance = 1.0f;
    };
    struct RenderState {
        float point_size = 1.0f;
        int colormap_index = 0;
        int point_shape = 0;
        int height_source = 0;
        int color_source = 0;
        float height_mult = 1.0f;
        int value_clip_enabled = 0;
        float value_clip_min = 0.0f;
        float value_clip_max = 0.0f;
    };
    struct ViewportState {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool frame_ready = false;
        float fps = 0.0f;
    };

    [[nodiscard]] CameraState camera_state() const;
    [[nodiscard]] RenderState render_state() const;
    [[nodiscard]] ViewportState viewport_state() const;

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
    void track_frame_timing_locked();
    float span_or_default() const;

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

    // 自定义清屏色（render_rgba 使用）
    float clear_r_ = 0.086f;
    float clear_g_ = 0.102f;
    float clear_b_ = 0.133f;
    float clear_a_ = 1.0f;
    bool has_custom_clear_ = false;

    // 渲染设置镜像（供 snapshot；与 push_ 同步写入）
    float point_size_ = 1.0f;
    int colormap_index_ = 0;
    int point_shape_ = 0;
    int height_source_ = 0;
    int color_source_ = 0;
    float height_mult_ = 1.0f;
    int value_clip_enabled_ = 0;
    float value_clip_min_ = 0.0f;
    float value_clip_max_ = 1.0f;

    // 帧计时（简单 fps 估计）
    std::int64_t last_frame_tick_ = 0;
    float estimated_fps_ = 0.0f;

    mutable std::mutex mutex_;
};

}  // namespace gs3d::ffi
