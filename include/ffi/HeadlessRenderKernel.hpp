#pragma once

#include "app/TilePointCache.hpp"
#include "camera/Camera.hpp"
#include "camera/CameraController.hpp"
#include "core/PointData.hpp"
#include "core/TileData.hpp"
#include "data/Gs3dDataset.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "data/Gs3dLodReader.hpp"
#include "data/Gs3dTileReader.hpp"
#include "ffi/LodBuilder.hpp"
#include "render/OffscreenFramebuffer.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/PointCloudLodGpu.hpp"
#include "render/PointCloudTileGpu.hpp"
#include "render/PointPipeline.hpp"
#include "render/TileSelection.hpp"
#include "render/VulkanContext.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
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
        /// 每帧绘制点预算（0 = 按视口自动：像素数 / 6，夹在 [64k, 4M]）。
        std::uint64_t point_budget = 0;
        /// 超过该点数的层级不上传 GPU（0 = 不限）；防止超大数据的显存/上传峰值。
        std::uint64_t max_gpu_points = 12'000'000;
        /// 关闭 LOD 链（调试与回归对比用）。
        bool disable_lod = false;
        /// 细尺度 tile 流式：数据集带 tiles.gs3dtiles(.index) 时生效。
        /// 粗尺度走 LOD，细尺度（投影足够的瓦片）叠加全精度原始点。
        ///
        /// ⚠ 尚未完成：sidecar 解析/选取已通（真机验证），但 1M 点规模下
        /// 流式+合成的帧内在 Windows E2E 中触发原生崩溃
        /// （vkDestroyBuffer: Invalid device 后进程未完成），
        /// 默认关闭以便后续单独定位；打开前勿用于生产。
        bool tile_streaming = true;  // BISECT
        /// tile 投影宽度 ≥ 该像素数才加载全精度（Potree SSE；旧 Viewer 默认 50）。
        float min_tile_pixel_size = 50.0f;
        /// 每帧 tile 上传字节预算（避免一次拉入过多阻塞帧）。
        std::uint64_t tile_upload_budget_bytes = 8ull * 1024ull * 1024ull;
        /// CPU 侧 tile 点缓存上限（LRU）。
        std::uint64_t tile_cpu_cache_bytes = 256ull * 1024ull * 1024ull;
        /// 每帧最多从磁盘读几块 tile。
        std::uint32_t tile_max_reads_per_frame = 2;
    };

    HeadlessRenderKernel() = default;
    ~HeadlessRenderKernel();

    HeadlessRenderKernel(const HeadlessRenderKernel&) = delete;
    HeadlessRenderKernel& operator=(const HeadlessRenderKernel&) = delete;

    // 返回 0 成功；负数见统一返回码。
    int create(const Config& config);
    void destroy() noexcept;

    int load_dataset(const std::string& path);
    int load_sample();
    /// 生成 grid×grid 的内置地形（基准/压力测试用；point_count 会取为完全平方数）。
    int load_synthetic(std::uint64_t point_count);
    void unload_dataset() noexcept;
    [[nodiscard]] bool has_dataset() const noexcept { return has_dataset_; }

    /// 每帧绘制点预算（0 = 恢复自动）。
    void set_point_budget(std::uint64_t max_points);

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

    /// 大规模数据的层级状态（诊断/UI 投影用，不暴露 GPU 句柄）。
    struct LodState {
        std::uint64_t source_points = 0;
        std::uint64_t drawn_points = 0;
        std::uint64_t budget = 0;
        int level_index = 0;
        int level_count = 1;
        std::uint64_t gpu_points = 0;
        float estimated_spacing = 0.0f;
        double build_ms = 0.0;
        double upload_ms = 0.0;
        bool automatic_budget = true;
        // ── 细尺度 tile ──────────────────────────────────────────────
        bool tiles_available = false;
        std::uint64_t tile_count = 0;
        std::uint64_t tile_needed = 0;
        std::uint64_t tile_resident = 0;
        std::uint64_t tile_drawn_points = 0;
        std::uint64_t tile_uploaded_bytes_frame = 0;
        bool tile_clip_active = false;
        bool tiles_fully_resident = false;
    };

    [[nodiscard]] CameraState camera_state() const;
    [[nodiscard]] RenderState render_state() const;
    [[nodiscard]] ViewportState viewport_state() const;
    [[nodiscard]] LodState lod_state() const;

    [[nodiscard]] const std::string& last_error() const noexcept {
        return last_error_;
    }
    [[nodiscard]] std::uint64_t point_count() const noexcept {
        return point_count_;
    }

    /// 活动 GPU 与 Profile 诊断（供 ABI 曝光，不泄露 Vulkan 句柄）。
    struct GpuDiagnostics {
        std::string name;
        std::string type_label;
        std::string uuid;
        std::string profile;
        std::string selection;
        std::string api_version;  // "1.1.180"
        std::uint32_t driver_version = 0;
        std::uint64_t memory_bytes = 0;
    };
    [[nodiscard]] GpuDiagnostics gpu_diagnostics() const;

private:
    void create_pipeline();
    void destroy_gpu_resources() noexcept;
    /// 申请/复用帧资源（命令缓冲、栅栏、常驻 staging 缓冲）。
    void ensure_frame_resources(std::size_t bytes);
    void destroy_frame_resources() noexcept;
    void update_push_from_camera();
    // ── 大规模数据：LOD 链与点预算 ──────────────────────────────────────────
    /// 生成内置合成地形（基准/压力测试）。
    int generate_synthetic_grid(std::uint64_t requested_points);
    /// 根据当前数据集重建 CPU LOD 链并上传各级到 GPU。
    int rebuild_lod_locked();
    /// 解析并打开数据集旁边的 LOD / tile sidecar（无则保留为空）。
    void open_sidecars_locked(
        const std::filesystem::path& dataset_path,
        const std::filesystem::path& bundle_dir
    );
    /// 每帧 tile 流式：选取 → 限流读取 → 预算内上传 → 计算可绘制集与裁剪盒。
    void update_tile_streaming_locked();
    /// 当前视口对应的每帧点预算。
    [[nodiscard]] std::uint64_t effective_point_budget() const;
    /// 按预算选层（返回 lod_ 层索引）。
    [[nodiscard]] std::size_t choose_lod_index() const;
    void set_error(const std::string& message);
    void reset_camera_to_bounds_locked();
    void track_frame_timing_locked();
    float span_or_default() const;

    Config config_{};
    std::string last_error_;

    std::unique_ptr<gs3d::render::VulkanContext> context_;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;

    // 帧资源：跨帧复用，避免每帧 4MB 级别的分配/释放抖动。
    VkCommandBuffer frame_command_ = VK_NULL_HANDLE;
    VkFence frame_fence_ = VK_NULL_HANDLE;
    VkBuffer staging_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory_ = VK_NULL_HANDLE;
    void* staging_mapped_ = nullptr;
    std::size_t staging_capacity_ = 0;

    gs3d::render::OffscreenFramebuffer framebuffer_;
    std::unique_ptr<gs3d::render::PointPipeline> pipeline_;
    /// 多分辨率点云：level 0 = 全量，其后为 LodBuilder 产出的粗化层。
    gs3d::render::PointCloudLodGpu lod_;
    std::vector<LodLevel> lod_chain_;
    /// 预处理产出的 .gs3dlod（存在时优先于运行时建链，须存活到上传完成）。
    std::optional<gs3d::data::Gs3dLodDataset> lod_dataset_;
    std::filesystem::path dataset_path_;
    std::filesystem::path bundle_dir_;
    std::size_t drawn_level_ = 0;
    std::uint64_t drawn_point_count_ = 0;
    std::uint64_t point_budget_ = 0;
    double lod_build_ms_ = 0.0;
    double lod_upload_ms_ = 0.0;
    float estimated_spacing_ = 0.0f;
    std::uint64_t gpu_point_count_ = 0;

    // 细尺度：tile 选取 / CPU 缓存 / GPU 驻留 / 合成裁剪。
    std::unique_ptr<gs3d::data::Gs3dTileReader> tile_reader_;
    std::unique_ptr<gs3d::app::TilePointCache> tile_cache_;
    gs3d::render::PointCloudTileGpu tile_gpu_;
    gs3d::render::TileSelection tile_selection_{};
    gs3d::core::TileIndexView tile_index_view_{};
    /// 上传期间必须保住 CPU 点数据的生存期（PointDataView 指向它们）。
    std::vector<gs3d::app::SharedTilePoints> tile_pins_;
    std::vector<std::uint64_t> tile_needed_ids_;
    std::vector<std::uint64_t> tile_drawable_ids_;
    gs3d::core::Bounds3f tile_clip_bounds_{};
    bool tile_clip_active_ = false;
    bool tile_all_resident_ = false;
    std::uint64_t tile_drawn_points_ = 0;
    std::uint64_t tile_uploaded_bytes_frame_ = 0;
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
