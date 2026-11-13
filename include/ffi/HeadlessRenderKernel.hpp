#pragma once

#include "app/TilePointCache.hpp"
#include "camera/Camera.hpp"
#include "camera/CameraController.hpp"
#include "data/core/PointData.hpp"
#include "data/core/TileData.hpp"
#include "data/format/Gs3dDataset.hpp"
#include "data/lod/Gs3dLodDataset.hpp"
#include "data/lod/Gs3dLodReader.hpp"
#include "data/tile/Gs3dTileReader.hpp"
#include "ffi/LodBuilder.hpp"
#include "render/OffscreenFramebuffer.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/PointCloudLodGpu.hpp"
#include "render/PointCloudTileGpu.hpp"
#include "render/PresentSurface.hpp"
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
        /// 这是「值得加载」的下限；瓦片实际介入的时机还受 tile_point_budget 约束。
        float min_tile_pixel_size = 50.0f;
        /*
         * 瓦片层点预算（0 = 不限）：每帧允许绘制的全精度瓦片点数上限。
         *
         * 选取已按屏幕投影降序，超预算时从尾部截断（只丢屏幕上最小的瓦片）。
         * 没有预算时，「放大到瓦片介入」会把帧时一次性打穿：真实场景下视锥
         * 内大半瓦片都会达标（掠射视角下投影估算偏保守），真机 10M 实测
         * 一帧要画 549 万瓦片点 → 17 FPS。有了预算，大数据上瓦片会自然
         * 推迟到更深放大才介入，而小数据集（总点数在预算内）不受影响。
         *
         * 取值依据（真机 Adreno 650 / 1204×704 实测）：
         *   无预算：549 万点 → 引擎 48.4 ms → 17 FPS
         *   150 万点 → 引擎 17.1 ms → 47 FPS
         *   → 实测约 11.4 ms / 百万点，取 100 万点（引擎 ≈ 11–12 ms + LOD 层
         *     约 4 ms ≈ 16 ms）刚好卡在 60 FPS 线上，故定为默认值。
         */
        std::uint64_t tile_point_budget = 1'200'000;
        /*
         * 瓦片层每帧绘制调用上限（每块瓦片一次 vkCmdDraw）。
         *
         * 真机实测：1478 块瓦片（1M 数据集、fov=19°）时引擎 19 ms，而 176 块
         * （10M 数据集、fov=1°）只有 8.6 ms —— 点数相近，差别在绘制调用数
         * （每块 ~13 µs 的驱动/状态开销）。所以预算必须同时管住「块数」。
         * 超上限同样走「全有或全无」：这一帧不出瓦片，等更放大时才出。
         */
        std::uint32_t tile_max_draws = 512;
        /*
         * 底图（LOD）点预算上限：瓦片层未生效时，底图可以随放大自动变细
         * （否则放大后整屏都是稀疏点阵，看起来像「模型缺了一半」）。
         * 瓦片层一旦覆盖可见面，底图立刻退回粗层（避免与瓦片叠加浪费）。
         * 取值与 tile_point_budget 对齐：两者任一时刻只开销一个。
         */
        std::uint64_t lod_point_cap = 1'200'000;
        /// 底图屏幕目标：点距投影不超过该像素数（越小越密、越贵）。
        float lod_max_projected_spacing_px = 2.5f;
        /*
         * 渲染像素 / 逻辑像素之比（= 设备 dpr），由平台层在接管物理像素纹理时告知。
         *
         * 为什么需要它：零拷贝纹理按物理像素建交换链（否则合成器把逻辑尺寸的
         * 纹理放大到屏幕 —— 点云发软，而且丢掉全屏硬件叠加直通，真机实测
         * 13.9 ms → 8.3 ms）。但下面这些「像素」参数是按逻辑像素调优的视觉门槛：
         * 物理像素下 viewport_height ×2.13，投影像素数整体放大 → 选取的瓦片
         * 翻几倍 → 超预算 → 「全有或全无」直接让瓦片层消失（真机 10M fov=1.4°
         * 实测整屏空白），底图也会越级变细。
         *
         * 因此：把 view_scale 当作「多少物理像素换 1 个逻辑像素」的汇率，
         * 所有像素门槛（min_tile_pixel_size / lod_max_projected_spacing_px /
         * 自动点预算）一律换算回逻辑像素再比。1.0 = 逻辑像素渲染
         * （回读路径；也等于纹理 dpr=1 的旧行为）。
         */
        float view_scale = 1.0f;
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

    /// 工作线程可调用的磁盘读取结果；装载前不触碰任何 Vulkan 对象。
    struct DatasetLoad {
        gs3d::data::Gs3dDataset dataset;
        std::filesystem::path dataset_path;
        std::filesystem::path bundle_dir;
    };

    /// 只读文件系统，可在任意线程执行。失败时写入 error 并返回 false。
    static bool load_dataset_from_disk(
        const std::string& path, DatasetLoad* out, std::string* error);

    /// 装载已读取的数据集；只能在渲染线程调用（上传 LOD、重置相机）。
    int install_dataset(DatasetLoad loaded);

    int load_dataset(const std::string& path);
    int load_sample();
    /// 生成 grid×grid 的内置地形（基准/压力测试用；point_count 会取为完全平方数）。
    int load_synthetic(std::uint64_t point_count);
    void unload_dataset() noexcept;
    [[nodiscard]] bool has_dataset() const noexcept { return has_dataset_; }

    /// 每帧绘制点预算（0 = 恢复自动）。
    void set_point_budget(std::uint64_t max_points);

    int resize(std::uint32_t width, std::uint32_t height);

    /*
     * 附加平台 surface：切换到「窗口呈现」上下文（内部重建 VulkanContext、
     * 交换链与 1 附件的 present 管线）。离屏回读路径保持不变，
     * 仍可用于截图与自动化测试。
     *
     * provider 与 C ABI 同形状，因此无需任何适配层；平台句柄只以
     * uint64 形式出现，核心不认识 HWND / ANativeWindow。
     */
    int attach_surface(
        const gs3d::render::VulkanSurfaceProvider& provider,
        std::uint32_t width,
        std::uint32_t height
    );
    void detach_surface();
    [[nodiscard]] bool has_surface() const;
    /// 渲染并呈递一帧到交换链；0 成功，-2 表示本帧需跳过（刚重建）。
    int present_frame();

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

    /// 设置「渲染像素 / 逻辑像素」汇率（见 Config::view_scale）；幂等。
    int set_view_scale(float scale);
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
        /// 透视相机的垂直视场角（度）。缩放走 FOV，因此这是「用户缩放状态」
        /// 的唯一可观测量：Surface 重建/尺寸变化都必须保留它。
        float fov_y_degrees = 0.0f;
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
        /// 最近一次 GPU 上传是否复用了已有 CPU LOD 链（Surface 重建路径）。
        bool cpu_chain_reused = false;
        bool automatic_budget = true;
        // ── 细尺度 tile ──────────────────────────────────────────────
        bool tiles_available = false;
        std::uint64_t tile_count = 0;
        std::uint64_t tile_needed = 0;
        /// 点预算截断「之前」的选取数（= 屏幕空间误差真正达标的瓦片数）。
        std::uint64_t tile_selected = 0;
        /// 截断后计划绘制的瓦片点数（resident 后会等于实际绘制量）。
        std::uint64_t tile_planned_points = 0;
        std::uint64_t tile_resident = 0;
        std::uint64_t tile_drawn_points = 0;
        std::uint64_t tile_uploaded_bytes_frame = 0;
        bool tile_clip_active = false;
        bool tiles_fully_resident = false;
        /// 瓦片层是否盖住整屏可见面；false → 底图必须够密（见 choose_lod_index）。
        bool tile_cover_view = false;
        /// 视锥内投影尺寸未达门槛的瓦片数（>0 就是底图被逼变密的原因）。
        std::uint32_t tile_below_threshold = 0;
        /// 本帧是否因「选取本身不是全覆盖」而整层不出瓦片（见 partial_coverage）。
        bool tile_skipped_by_coverage = false;
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
    /// keep_cpu_caches=true 时保留纯 CPU 资产（LOD 链、瓦片 sidecar 与点缓存），
    /// 供 Surface 重建后只重传 GPU 缓冲；false 时全部丢弃（换数据集/销毁）。
    void destroy_gpu_resources(bool keep_cpu_caches = false) noexcept;
    /// 申请/复用帧资源（命令缓冲、栅栏、常驻 staging 缓冲）。
    void ensure_frame_resources(std::size_t bytes);
    void destroy_frame_resources() noexcept;
    void update_push_from_camera();
    /// create()/attach_surface() 共用的资源创建序列（调用方已持锁）。
    int create_resources_locked();
    void create_present_resources_locked();
    void destroy_present_resources_locked() noexcept;

    // ── 大规模数据：LOD 链与点预算 ──────────────────────────────────────────
    /// 生成内置合成地形（基准/压力测试）。
    int generate_synthetic_grid(std::uint64_t requested_points);
    /// 根据当前数据集重建 CPU LOD 链并上传各级到 GPU。
    /// reuse_cpu_chain=true 时跳过 CPU 建链，只用已有链重新上传（Surface 重建后）。
    int rebuild_lod_locked(bool reuse_cpu_chain = false);
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
    /// 最近一次 rebuild 是否跳过 CPU 建链（供测试与诊断断言）。
    bool lod_cpu_chain_reused_ = false;
    /// 最近一次记录的瓦片选取需求数：仅在其变化时打日志（诊断选取为何开关）。
    std::size_t tile_needed_logged_ = static_cast<std::size_t>(-1);
    /// 最近一次选取（截断前）的瓦片数与计划点数，供诊断与测试断言。
    std::uint64_t tile_selected_ = 0;
    std::uint64_t tile_planned_points_ = 0;
    /// 本帧是否真的绘制了瓦片（决定底图选层的前提之一）。
    bool tile_layers_active_ = false;
    /// 本帧视锥内「投影尺寸未达门槛」的瓦片数；>0 说明有可见面只有底图覆盖。
    std::uint32_t tile_below_threshold_ = 0;
    /// 瓦片层是否盖住了整屏可见面（false → 底图必须足够密，见 choose_lod_index）。
    bool tile_cover_view_ = false;
    /// 本帧瓦片整层不出的原因是否是「选取不是全覆盖」（诊断/测试用）。
    bool tile_skipped_by_coverage_ = false;
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

    // ── 窗口呈现（present）──────────────────────────────────────────────────
    gs3d::render::VulkanSurfaceProvider surface_provider_{};
    std::unique_ptr<gs3d::render::PresentSurface> present_surface_;
    std::unique_ptr<gs3d::render::PointPipeline> present_pipeline_;
    VkCommandBuffer present_command_ = VK_NULL_HANDLE;
    VkFence present_fence_ = VK_NULL_HANDLE;

    mutable std::mutex mutex_;
};

}  // namespace gs3d::ffi
