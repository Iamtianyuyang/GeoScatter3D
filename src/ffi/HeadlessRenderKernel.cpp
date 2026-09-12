#include "ffi/HeadlessRenderKernel.hpp"

#include "camera/CameraController.hpp"
#include "camera/MouseRay.hpp"
#include "data/PointDataAdapters.hpp"
#include "data/TileDataAdapters.hpp"
#include "util/Log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

// 仅本编译单元定义 STB 写实现；gs3d_ffi 不链接 gs3d_app（ViewerAppScreenshot），
// 不会在同一镜像内重复符号。
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace gs3d::ffi {

namespace {

void check_vk(VkResult result, const char* message) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(message) + " (" +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
}

/** VkPhysicalDeviceProperties::apiVersion → "1.1.180"（仅供诊断展示）。 */
std::string format_api_version(std::uint32_t version) {
    return std::to_string(VK_VERSION_MAJOR(version)) + "." +
           std::to_string(VK_VERSION_MINOR(version)) + "." +
           std::to_string(VK_VERSION_PATCH(version));
}

}  // namespace

HeadlessRenderKernel::~HeadlessRenderKernel() {
    destroy();
}

void HeadlessRenderKernel::set_error(const std::string& message) {
    last_error_ = message;
    gs3d::util::log::error() << "[HeadlessRenderKernel] " << message << '\n';
}

int HeadlessRenderKernel::create(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    destroy_gpu_resources();
    config_ = config;
    if (config_.width == 0 || config_.height == 0) {
        set_error("invalid viewport size");
        return -1;
    }
    return create_resources_locked();
}

int HeadlessRenderKernel::create_resources_locked() {
    try {
        gs3d::render::VulkanContextConfig ctx_cfg;
        // 附加了平台 surface 就走窗口上下文，否则维持纯离屏。
        ctx_cfg.headless = !surface_provider_.valid();
        ctx_cfg.surface_provider = surface_provider_;
        ctx_cfg.preferred_gpu = config_.preferred_gpu;
        ctx_cfg.enable_validation_layers = config_.enable_validation;
        ctx_cfg.application_name = "GeoScatter3D.FFI.Engine";
        context_ = std::make_unique<gs3d::render::VulkanContext>(ctx_cfg);

        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex =
            context_->queue_family_indices().graphics_family.value();
        check_vk(
            vkCreateCommandPool(context_->device(), &pool_info, nullptr,
                                &command_pool_),
            "create command pool"
        );

        framebuffer_.create(
            *context_,
            VkExtent2D{config_.width, config_.height},
            VK_FORMAT_R8G8B8A8_UNORM
        );
        create_pipeline();

        if (surface_provider_.valid()) {
            create_present_resources_locked();
        }

        camera_.set_viewport(config_.width, config_.height);
        camera_.set_perspective(45.0f, 1.0f, 100000.0f);
        last_error_.clear();
        return 0;
    } catch (const std::exception& ex) {
        // 资源创建失败必须留下可诊断痕迹（logcat tag=gs3d）。
        gs3d::util::log::error()
            << "[HeadlessRenderKernel] create_resources failed: " << ex.what()
            << '\n';
        set_error(ex.what());
        destroy_gpu_resources();
        return -3;
    }
}

void HeadlessRenderKernel::create_pipeline() {
    gs3d::render::PointPipelineConfig pipe_cfg;
    if (!config_.shader_dir.empty()) {
        pipe_cfg.vertex_shader_path =
            std::filesystem::path(config_.shader_dir) / "point.vert.spv";
        pipe_cfg.fragment_shader_path =
            std::filesystem::path(config_.shader_dir) / "point.frag.spv";
    }
    pipeline_ = std::make_unique<gs3d::render::PointPipeline>(
        *context_, framebuffer_.render_pass(), pipe_cfg
    );
}

void HeadlessRenderKernel::destroy_gpu_resources() noexcept {
    if (context_) {
        vkDeviceWaitIdle(context_->device());
    }
    destroy_present_resources_locked();
    destroy_frame_resources();
    lod_.destroy();
    lod_chain_.clear();
    drawn_point_count_ = 0;
    gpu_point_count_ = 0;
    tile_reader_.reset();
    tile_cache_.reset();
    tile_gpu_.destroy();
    tile_index_view_ = {};
    tile_needed_ids_.clear();
    tile_drawable_ids_.clear();
    tile_drawn_points_ = 0;
    pipeline_.reset();
    framebuffer_.destroy();
    if (context_ && command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_->device(), command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }
    context_.reset();
}

void HeadlessRenderKernel::ensure_frame_resources(std::size_t bytes) {
    const VkDevice device = context_->device();

    if (frame_command_ == VK_NULL_HANDLE) {
        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = command_pool_;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        check_vk(
            vkAllocateCommandBuffers(device, &alloc, &frame_command_),
            "alloc frame command buffer"
        );
    }

    if (frame_fence_ == VK_NULL_HANDLE) {
        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        check_vk(
            vkCreateFence(device, &fence_info, nullptr, &frame_fence_),
            "create frame fence"
        );
    }

    if (bytes == 0 || staging_capacity_ >= bytes) {
        return;
    }

    // 扩容：旧缓冲可能仍被上一帧使用，先等设备空闲再释放。
    vkDeviceWaitIdle(device);
    if (staging_buffer_ != VK_NULL_HANDLE) {
        if (staging_mapped_ != nullptr) {
            vkUnmapMemory(device, staging_memory_);
            staging_mapped_ = nullptr;
        }
        vkDestroyBuffer(device, staging_buffer_, nullptr);
        staging_buffer_ = VK_NULL_HANDLE;
        vkFreeMemory(device, staging_memory_, nullptr);
        staging_memory_ = VK_NULL_HANDLE;
        staging_capacity_ = 0;
    }

    VkBufferCreateInfo buf_info{};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = bytes;
    buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check_vk(
        vkCreateBuffer(device, &buf_info, nullptr, &staging_buffer_),
        "create staging buffer"
    );

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, staging_buffer_, &req);
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(context_->physical_device(), &mem_props);
    std::uint32_t mem_type = UINT32_MAX;
    for (std::uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((req.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags &
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (mem_props.memoryTypes[i].propertyFlags &
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            mem_type = i;
            break;
        }
    }
    if (mem_type == UINT32_MAX) {
        throw std::runtime_error("no host-visible coherent memory for staging");
    }

    VkMemoryAllocateInfo mem_alloc{};
    mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mem_alloc.allocationSize = req.size;
    mem_alloc.memoryTypeIndex = mem_type;
    check_vk(
        vkAllocateMemory(device, &mem_alloc, nullptr, &staging_memory_),
        "allocate staging memory"
    );
    check_vk(
        vkBindBufferMemory(device, staging_buffer_, staging_memory_, 0),
        "bind staging memory"
    );
    check_vk(
        vkMapMemory(device, staging_memory_, 0, bytes, 0, &staging_mapped_),
        "map staging memory"
    );
    staging_capacity_ = bytes;
}

void HeadlessRenderKernel::destroy_frame_resources() noexcept {
    if (!context_) {
        return;
    }
    const VkDevice device = context_->device();
    if (staging_mapped_ != nullptr) {
        vkUnmapMemory(device, staging_memory_);
        staging_mapped_ = nullptr;
    }
    if (staging_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, staging_buffer_, nullptr);
        staging_buffer_ = VK_NULL_HANDLE;
    }
    if (staging_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, staging_memory_, nullptr);
        staging_memory_ = VK_NULL_HANDLE;
    }
    staging_capacity_ = 0;
    if (frame_fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(device, frame_fence_, nullptr);
        frame_fence_ = VK_NULL_HANDLE;
    }
    if (frame_command_ != VK_NULL_HANDLE && command_pool_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device, command_pool_, 1, &frame_command_);
        frame_command_ = VK_NULL_HANDLE;
    }
}

void HeadlessRenderKernel::destroy() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    destroy_gpu_resources();
    dataset_ = {};
    has_dataset_ = false;
    point_count_ = 0;
    last_rgba_.clear();
    last_w_ = last_h_ = 0;
}

int HeadlessRenderKernel::load_dataset(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_) {
        set_error("engine not created");
        return -2;
    }
    try {
        auto fs_path = std::filesystem::u8path(path);
        if (!std::filesystem::exists(fs_path)) {
            set_error("dataset not found: " + path);
            return -2;
        }
        // bundle 目录 → source.gs3d
        std::filesystem::path bundle_directory;
        if (std::filesystem::is_directory(fs_path)) {
            bundle_directory = fs_path;
            fs_path = fs_path / "source.gs3d";
            if (!std::filesystem::exists(fs_path)) {
                set_error("bundle missing source.gs3d");
                return -2;
            }
        }

        // sidecar 与数据集同目录（bundle 为 lod.gs3dlod / tiles.gs3dtiles*）。
        const std::filesystem::path dataset_path = fs_path;
        const std::filesystem::path bundle_dir = bundle_directory;
        auto loaded = gs3d::data::Gs3dDatasetLoader::load(fs_path);
        if (!loaded.has_point_data() || loaded.empty()) {
            set_error("dataset has no point data");
            return -3;
        }
        dataset_ = std::move(loaded);
        point_count_ = dataset_.point_count();
        has_dataset_ = true;

        dataset_path_ = dataset_path;
        bundle_dir_ = bundle_dir;
        open_sidecars_locked(dataset_path, bundle_dir);

        const int lod_rc = rebuild_lod_locked();
        if (lod_rc != 0) {
            has_dataset_ = false;
            return lod_rc;
        }

        reset_camera_to_bounds_locked();
        last_error_.clear();
        return 0;
    } catch (const std::exception& ex) {
        set_error(ex.what());
        has_dataset_ = false;
        return -3;
    }
}

void HeadlessRenderKernel::open_sidecars_locked(
    const std::filesystem::path& dataset_path,
    const std::filesystem::path& bundle_dir
) {
    tile_reader_.reset();
    tile_cache_.reset();
    tile_gpu_.destroy();
    tile_index_view_ = {};
    tile_needed_ids_.clear();
    tile_drawable_ids_.clear();
    tile_clip_active_ = false;
    tile_all_resident_ = false;
    tile_drawn_points_ = 0;
    tile_uploaded_bytes_frame_ = 0;

    // 即使暂时关闭流式，也解析 sidecar：诊断/选取元数据仍要可见。
    // bundle 目录用固定文件名；普通文件按同名前缀查找 sidecar。
    std::filesystem::path index_path;
    std::filesystem::path data_path;
    if (bundle_dir.empty()) {
        const auto parent = dataset_path.parent_path();
        const auto stem = dataset_path.stem().string();
        index_path = parent / (stem + ".gs3dtiles.index");
        data_path = parent / (stem + ".gs3dtiles");
    } else {
        index_path = bundle_dir / "tiles.gs3dtiles.index";
        data_path = bundle_dir / "tiles.gs3dtiles";
    }

    if (!std::filesystem::exists(index_path) ||
        !std::filesystem::exists(data_path)) {
        // 无 tile sidecar：退化为纯 LOD（当前行为）。
        return;
    }

    try {
        tile_reader_ = std::make_unique<gs3d::data::Gs3dTileReader>(
            gs3d::data::Gs3dTileReader::open(
                index_path,
                data_path,
                dataset_.header()
            )
        );
    } catch (const std::exception& ex) {
        gs3d::util::log::warning()
            << "[HeadlessRenderKernel] tile sidecar unusable: " << ex.what()
            << '\n';
        tile_reader_.reset();
        return;
    }

    if (!tile_reader_->valid()) {
        tile_reader_.reset();
        return;
    }

    tile_index_view_ = gs3d::data::make_tile_index_view(*tile_reader_);
    tile_cache_ = std::make_unique<gs3d::app::TilePointCache>(
        config_.tile_cpu_cache_bytes
    );

    gs3d::render::TileSelectionConfig selection_config;
    selection_config.min_tile_pixel_size = config_.min_tile_pixel_size;
    selection_config.height_source = static_cast<std::uint32_t>(height_source_);
    selection_config.height_mult = height_mult_;
    tile_selection_.set_config(selection_config);
    tile_selection_.reset();

    gs3d::util::log::info()
        << "[HeadlessRenderKernel] tiles: count=" << tile_reader_->tile_count()
        << " points=" << tile_reader_->total_point_count()
        << " sse_px=" << config_.min_tile_pixel_size
        << " upload_budget=" << config_.tile_upload_budget_bytes << '\n';
}

int HeadlessRenderKernel::generate_synthetic_grid(std::uint64_t point_count) {
    // 基准/压力测试用的确定性地形：网格边长取平方根，点数为完全平方。
    const std::uint64_t requested = std::max<std::uint64_t>(4, point_count);
    const int grid_size = std::max(
        2,
        static_cast<int>(std::llround(std::sqrt(static_cast<double>(requested))))
    );
    const float range = 20.0f;

    std::vector<gs3d::data::Gs3dPoint> points;
    points.reserve(static_cast<std::size_t>(grid_size) * grid_size);

    float min_x = 1e9f, max_x = -1e9f;
    float min_y = 1e9f, max_y = -1e9f;
    float min_z = 1e9f, max_z = -1e9f;

    for (int i = 0; i < grid_size; ++i) {
        const float x = -range + (2.0f * range * i) / (grid_size - 1);
        for (int j = 0; j < grid_size; ++j) {
            const float y = -range + (2.0f * range * j) / (grid_size - 1);
            const float r = std::sqrt(x * x + y * y);
            const float z = 6.0f * std::sin(r) / (r * 0.4f + 1.0f);

            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
            min_z = std::min(min_z, z);
            max_z = std::max(max_z, z);

            gs3d::data::Gs3dPoint p{};
            p.x = x;
            p.y = y;
            p.z = z;
            p.value = z;
            points.push_back(p);
        }
    }

    gs3d::data::Gs3dHeader header{};
    std::memcpy(header.magic, gs3d::data::GS3D_MAGIC, 4);
    header.version = gs3d::data::GS3D_VERSION;
    header.header_size = gs3d::data::GS3D_HEADER_V2_SIZE;
    header.point_data_offset = header.header_size;
    header.point_count = points.size();
    header.bbox_min_x = min_x; header.bbox_min_y = min_y; header.bbox_min_z = min_z;
    header.bbox_max_x = max_x; header.bbox_max_y = max_y; header.bbox_max_z = max_z;
    header.value_min = min_z;
    header.value_max = max_z;

    dataset_ = gs3d::data::Gs3dDataset(header, std::move(points), "built-in://synthetic");
    point_count_ = dataset_.point_count();
    has_dataset_ = true;
    return 0;
}

int HeadlessRenderKernel::load_sample() {
    return load_synthetic(10'000);
}

int HeadlessRenderKernel::load_synthetic(std::uint64_t point_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_) {
        set_error("load_synthetic: engine not created");
        return -2;
    }
    try {
        dataset_path_.clear();
        bundle_dir_.clear();
        const int rc = generate_synthetic_grid(point_count);
        if (rc != 0) {
            has_dataset_ = false;
            return rc;
        }
        const int lod_rc = rebuild_lod_locked();
        if (lod_rc != 0) {
            has_dataset_ = false;
            return lod_rc;
        }
        reset_camera_to_bounds_locked();
        last_error_.clear();
        return 0;
    } catch (const std::exception& ex) {
        set_error(ex.what());
        has_dataset_ = false;
        return -3;
    }
}

void HeadlessRenderKernel::set_point_budget(std::uint64_t max_points) {
    std::lock_guard<std::mutex> lock(mutex_);
    point_budget_ = max_points;
}

std::uint64_t HeadlessRenderKernel::effective_point_budget() const {
    if (point_budget_ != 0) {
        return point_budget_;
    }
    // 自动预算：约每 6 个像素一个点（点尺寸 1–2 px 时视觉上已饱和），
    // 并夹在 [64k, 4M]，避免小视口过度降采样、大视口无上限。
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(config_.width) * config_.height;
    const std::uint64_t budget = pixels / 6u;
    return std::clamp<std::uint64_t>(budget, 64u * 1024u, 4u * 1024u * 1024u);
}

std::size_t HeadlessRenderKernel::choose_lod_index() const {
    const std::size_t count = lod_.level_count();
    if (count <= 1) {
        return 0;
    }
    const std::uint64_t budget = effective_point_budget();
    for (std::size_t index = 0; index < count; ++index) {
        if (lod_.level(index).gpu_point_count <= budget) {
            return index;
        }
    }
    return count - 1;
}

int HeadlessRenderKernel::rebuild_lod_locked() {
    lod_.destroy();
    lod_chain_.clear();
    drawn_level_ = 0;
    drawn_point_count_ = 0;
    gpu_point_count_ = 0;
    estimated_spacing_ = 0.0f;
    lod_build_ms_ = 0.0;
    lod_upload_ms_ = 0.0;

    const auto& points = dataset_.points();
    if (points.empty()) {
        return -2;
    }

    /*
     * 1. 粗尺度层级：优先用预处理产出的 .gs3dlod（Potree 式分层由写作端决定，
     *    层数/体素可复现），缺失时才在运行时按点间距现场建链。
     */
    lod_dataset_.reset();
    std::filesystem::path lod_path;
    if (!bundle_dir_.empty()) {
        lod_path = bundle_dir_ / "lod.gs3dlod";
    } else if (!dataset_path_.empty()) {
        lod_path = dataset_path_.parent_path() /
                   (dataset_path_.stem().string() + ".gs3dlod");
    }

    if (!config_.disable_lod && !lod_path.empty() &&
        std::filesystem::exists(lod_path)) {
        try {
            auto read = gs3d::data::Gs3dLodReader::read(lod_path, dataset_.header());
            if (read.stats.success && !read.dataset.empty()) {
                for (const auto& level : read.dataset.levels()) {
                    LodLevel gpu_level;
                    gpu_level.points = level.points;
                    gpu_level.voxel_size = level.voxel_size;
                    gpu_level.source_point_count = level.source_point_count;
                    lod_chain_.push_back(std::move(gpu_level));
                }
                estimated_spacing_ = lod_chain_.empty()
                    ? 0.0f
                    : lod_chain_.front().voxel_size / 2.0f;
                gs3d::util::log::info()
                    << "[HeadlessRenderKernel] lod sidecar: levels="
                    << lod_chain_.size() << "\n";
                lod_dataset_ = std::move(read.dataset);
            }
        } catch (const std::exception& ex) {
            gs3d::util::log::warning()
                << "[HeadlessRenderKernel] lod sidecar unusable: " << ex.what()
                << "\n";
            lod_chain_.clear();
        }
    }

    if (!config_.disable_lod && lod_chain_.empty()) {
        LodBuildConfig lod_config;
        lod_config.min_points = std::max<std::uint64_t>(
            LodBuildConfig{}.min_points,
            effective_point_budget() / 2u
        );
        LodChain chain = LodBuilder::build(points, lod_config);
        estimated_spacing_ = chain.estimated_spacing;
        lod_build_ms_ = chain.build_ms;
        lod_chain_ = std::move(chain.levels);
    }

    // 2. 组装 GPU 层级：跳过超过 max_gpu_points 的层，避免一次性显存峰值。
    gs3d::render::PointCloudLodSource source;
    const std::uint64_t max_gpu_points = config_.max_gpu_points;
    const auto push_level = [&source, max_gpu_points](
                                const std::vector<gs3d::data::Gs3dPoint>& level_points,
                                float voxel_size,
                                std::uint64_t source_count,
                                std::uint32_t index) {
        if (level_points.empty()) {
            return;
        }
        const std::uint64_t count = level_points.size();
        if (max_gpu_points != 0 && count > max_gpu_points) {
            return;
        }
        gs3d::render::PointCloudLodSourceLevel level;
        level.name = "lod" + std::to_string(index);
        level.voxel_mode_name = "voxel";
        level.level_index = index;
        level.source_point_count = source_count;
        level.target_point_count = count;
        level.voxel_size = voxel_size;
        level.points = gs3d::data::make_point_data_view(level_points);
        source.levels.push_back(std::move(level));
    };

    push_level(points, 0.0f, static_cast<std::uint64_t>(points.size()), 0);
    for (std::size_t index = 0; index < lod_chain_.size(); ++index) {
        const auto& level = lod_chain_[index];
        push_level(
            level.points,
            level.voxel_size,
            level.source_point_count,
            static_cast<std::uint32_t>(index + 1)
        );
    }

    if (source.levels.empty()) {
        set_error("no LOD level fits the GPU point budget");
        return -3;
    }

    const auto upload_started = std::chrono::steady_clock::now();
    lod_.upload(*context_, command_pool_, context_->graphics_queue(), source);
    lod_upload_ms_ = std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - upload_started
                     ).count();

    for (std::size_t index = 0; index < lod_.level_count(); ++index) {
        gpu_point_count_ += lod_.level(index).gpu_point_count;
    }

    drawn_level_ = choose_lod_index();
    drawn_point_count_ = lod_.level(drawn_level_).gpu_point_count;

    // 规模诊断：真机 logcat 可直接读到建链/上传成本与选层结果。
    gs3d::util::log::info()
        << "[HeadlessRenderKernel] LOD source=" << point_count_
        << " gpu_levels=" << lod_.level_count()
        << " gpu_points=" << gpu_point_count_
        << " spacing=" << estimated_spacing_
        << " build=" << lod_build_ms_ << "ms"
        << " upload=" << lod_upload_ms_ << "ms"
        << " draw_level=" << drawn_level_
        << " draw_points=" << drawn_point_count_ << "\n";
    return 0;
}

void HeadlessRenderKernel::unload_dataset() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (context_) {
        vkDeviceWaitIdle(context_->device());
        lod_.destroy();
    }
    lod_chain_.clear();
    drawn_point_count_ = 0;
    gpu_point_count_ = 0;
    tile_reader_.reset();
    tile_cache_.reset();
    tile_gpu_.destroy();
    tile_index_view_ = {};
    tile_needed_ids_.clear();
    tile_drawable_ids_.clear();
    tile_drawn_points_ = 0;
    dataset_ = {};
    has_dataset_ = false;
    point_count_ = 0;
}

int HeadlessRenderKernel::resize(std::uint32_t width, std::uint32_t height) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_ || width == 0 || height == 0) return -1;
    if (width == config_.width && height == config_.height) return 0;
    try {
        vkDeviceWaitIdle(context_->device());
        framebuffer_.resize_after_device_idle(VkExtent2D{width, height});
        config_.width = width;
        config_.height = height;
        camera_.set_viewport(width, height);
        return 0;
    } catch (const std::exception& ex) {
        set_error(ex.what());
        return -3;
    }
}

void HeadlessRenderKernel::set_camera_look_at(
    float pos_x, float pos_y, float pos_z,
    float tgt_x, float tgt_y, float tgt_z,
    float up_x, float up_y, float up_z
) {
    std::lock_guard<std::mutex> lock(mutex_);
    camera_.set_position({pos_x, pos_y, pos_z});
    camera_.set_target({tgt_x, tgt_y, tgt_z});
    camera_.set_up({up_x, up_y, up_z});
    camera_.look_at({pos_x, pos_y, pos_z}, {tgt_x, tgt_y, tgt_z}, {up_x, up_y, up_z});
}

void HeadlessRenderKernel::reset_camera_to_bounds() {
    std::lock_guard<std::mutex> lock(mutex_);
    reset_camera_to_bounds_locked();
}

void HeadlessRenderKernel::reset_camera_to_bounds_locked() {
    // 调用方需已持有 mutex_（由 load_dataset 等路径调用）
    if (!has_dataset_) return;
    const gs3d::camera::CameraBounds bounds{
        {dataset_.bbox_min_x(), dataset_.bbox_min_y(), dataset_.bbox_min_z()},
        {dataset_.bbox_max_x(), dataset_.bbox_max_y(), dataset_.bbox_max_z()},
    };
    camera_controller_.set_bounds(bounds);
    camera_controller_.clear_orbit_pivot();
    camera_.set_viewport(config_.width, config_.height);
    camera_.set_perspective(45.0f, 1.0f, 100000.0f);
    // fit_bounds 保留当前朝向；真正的复位必须恢复规范朝向。
    camera_controller_.reset_view(camera_);
    update_push_from_camera();
}

int HeadlessRenderKernel::camera_input(
    float rotate_dx, float rotate_dy,
    float pan_dx, float pan_dy, float scroll_y
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_ || !has_dataset_) {
        set_error("camera_input: no dataset");
        return -3;
    }
    const bool rotate = (rotate_dx != 0.0f || rotate_dy != 0.0f);
    const bool pan = (pan_dx != 0.0f || pan_dy != 0.0f);
    if (rotate && pan) {
        set_error("camera_input: rotate and pan are exclusive");
        return -2;
    }
    gs3d::camera::CameraInput input{};
    input.viewport_width = config_.width;
    input.viewport_height = config_.height;
    if (rotate) {
        input.rotate = true;
        input.delta_x = rotate_dx;
        input.delta_y = rotate_dy;
    } else if (pan) {
        input.pan = true;
        input.delta_x = pan_dx;
        input.delta_y = pan_dy;
    }
    input.scroll_y = scroll_y;
    input.mouse_position_valid = false;
    const bool changed = camera_controller_.update(camera_, input);
    (void)changed;
    update_push_from_camera();
    return 0;
}

void HeadlessRenderKernel::update_tile_streaming_locked() {
    tile_uploaded_bytes_frame_ = 0;
    tile_drawn_points_ = 0;
    tile_drawable_ids_.clear();
    tile_clip_active_ = false;
    tile_all_resident_ = false;
    tile_pins_.clear();

    if (!tile_reader_ || !tile_reader_->valid() || !tile_cache_) {
        tile_needed_ids_.clear();
        return;
    }
    if (!config_.tile_streaming) {
        // 流式未启用（见 Config::tile_streaming）：只保留 sidecar 元数据。
        tile_needed_ids_.clear();
        return;
    }

    // 1. 选取：视锥剔除 + 屏幕空间误差（投影宽度 ≥ min_tile_pixel_size 才要全精度）。
    tile_selection_.set_config([&] {
        gs3d::render::TileSelectionConfig cfg;
        cfg.min_tile_pixel_size = config_.min_tile_pixel_size;
        cfg.height_source = static_cast<std::uint32_t>(height_source_);
        cfg.height_mult = height_mult_;
        return cfg;
    }());
    const auto selection = tile_selection_.update(camera_, tile_index_view_);
    tile_needed_ids_ = selection.enabled ? selection.tile_ids
                                         : std::vector<std::uint64_t>{};

    // 2. 按帧限流读取缺失瓦片（先读后传；已驻留的不重复读）。
    std::vector<std::pair<std::uint64_t, gs3d::core::PointDataView>> uploads;
    uploads.reserve(tile_needed_ids_.size());
    tile_pins_.reserve(tile_needed_ids_.size());
    std::uint32_t reads = 0;

    for (const auto tile_id : tile_needed_ids_) {
        auto cached = tile_cache_->get(tile_id);
        if (!cached && !tile_gpu_.has_resident_tile(tile_id) &&
            reads < config_.tile_max_reads_per_frame) {
            try {
                auto block = tile_reader_->read_tile_points_with_ids(tile_id);
                auto points = std::make_shared<gs3d::app::TilePoints>();
                points->points = std::move(block.points);
                points->point_ids = std::move(block.point_ids);
                cached = points;
                tile_cache_->put(tile_id, points);
                ++reads;
            } catch (const std::exception& ex) {
                set_error(std::string("tile read failed: ") + ex.what());
                continue;
            }
        }
        if (!cached) {
            continue;
        }
        tile_pins_.push_back(cached);
        uploads.emplace_back(
            tile_id,
            gs3d::data::make_point_data_view(
                cached->points,
                cached->point_ids.empty() ? nullptr : cached->point_ids.data()
            )
        );
    }

    // 3. 预算内上传（选择变化时不清空已驻留集合：新瓦片边到边替换）。
    try {
        const auto sync = tile_gpu_.sync_from_cached_tiles(
            *context_,
            command_pool_,
            context_->graphics_queue(),
            uploads,
            tile_needed_ids_,
            config_.tile_upload_budget_bytes
        );
        tile_uploaded_bytes_frame_ = sync.uploaded_bytes;
    } catch (const std::exception& ex) {
        set_error(std::string("tile upload failed: ") + ex.what());
        return;
    }

    // 4. 可绘制集 + 裁剪盒（全部所需瓦片驻留时才允许裁剪 LOD）。
    //
    // 可绘制集必须每帧按「当前选择」重建：此前只追加不清空，于是随着相机
    // 移动会累积历次选择的旧瓦片，而裁剪盒只覆盖当前所需瓦片——旧瓦片照画、
    // 其区域的 LOD 却被裁掉，屏幕上就出现一块块的拼块接缝（真机可见）。
    // 防闪烁靠选择集自身的滞回，而不是靠累积陈旧可绘制项。
    tile_drawable_ids_.clear();
    tile_all_resident_ = !tile_needed_ids_.empty();
    bool any = false;
    float min_x = 1e30f, min_y = 1e30f, min_z = 1e30f;
    float max_x = -1e30f, max_y = -1e30f, max_z = -1e30f;

    const auto& records = tile_reader_->records();
    for (const auto tile_id : tile_needed_ids_) {
        if (!tile_gpu_.has_resident_tile(tile_id)) {
            tile_all_resident_ = false;
            continue;
        }
        tile_drawable_ids_.push_back(tile_id);
        if (tile_id < records.size()) {
            const auto& record = records[static_cast<std::size_t>(tile_id)];
            min_x = std::min(min_x, record.bbox_min_x);
            min_y = std::min(min_y, record.bbox_min_y);
            min_z = std::min(min_z, record.bbox_min_z);
            max_x = std::max(max_x, record.bbox_max_x);
            max_y = std::max(max_y, record.bbox_max_y);
            max_z = std::max(max_z, record.bbox_max_z);
            any = true;
        }
    }

    tile_clip_active_ = tile_all_resident_ && any;
    if (any) {
        tile_clip_bounds_ = gs3d::core::Bounds3f{
            min_x, min_y, min_z, max_x, max_y, max_z
        };
    }
}


void HeadlessRenderKernel::create_present_resources_locked() {
    present_surface_ = std::make_unique<gs3d::render::PresentSurface>(
        *context_,
        gs3d::render::PresentSurface::Config{
            config_.width,
            config_.height,
            true,
            VK_FORMAT_B8G8R8A8_UNORM,
        }
    );

    gs3d::render::PointPipelineConfig pipe_cfg;
    if (!config_.shader_dir.empty()) {
        pipe_cfg.vertex_shader_path =
            std::filesystem::path(config_.shader_dir) / "point.vert.spv";
        pipe_cfg.fragment_shader_path =
            std::filesystem::path(config_.shader_dir) / "point.frag.spv";
    }
    // 交换链 render pass 只有颜色 + 深度：附件数必须跟着改。
    pipe_cfg.color_attachment_count = 1;
    present_pipeline_ = std::make_unique<gs3d::render::PointPipeline>(
        *context_, present_surface_->render_pass(), pipe_cfg
    );

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = command_pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    check_vk(
        vkAllocateCommandBuffers(context_->device(), &alloc, &present_command_),
        "alloc present command buffer"
    );

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    check_vk(
        vkCreateFence(context_->device(), &fence_info, nullptr, &present_fence_),
        "create present fence"
    );
}

void HeadlessRenderKernel::destroy_present_resources_locked() noexcept {
    present_surface_.reset();
    present_pipeline_.reset();
    if (context_ != nullptr) {
        if (present_fence_ != VK_NULL_HANDLE) {
            vkDestroyFence(context_->device(), present_fence_, nullptr);
        }
        if (present_command_ != VK_NULL_HANDLE && command_pool_ != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(
                context_->device(), command_pool_, 1, &present_command_);
        }
    }
    present_fence_ = VK_NULL_HANDLE;
    present_command_ = VK_NULL_HANDLE;
}

int HeadlessRenderKernel::attach_surface(
    const gs3d::render::VulkanSurfaceProvider& provider,
    std::uint32_t width,
    std::uint32_t height
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!provider.valid()) {
        set_error("attach_surface: provider has no create_surface callback");
        return -1;
    }
    if (width == 0 || height == 0) {
        set_error("attach_surface: zero-sized surface");
        return -1;
    }

    surface_provider_ = provider;
    config_.width = width;
    config_.height = height;

    const bool had_dataset = has_dataset_;
    gs3d::util::log::info()
        << "[HeadlessRenderKernel] attach_surface " << width << 'x' << height
        << " had_dataset=" << (had_dataset ? 1 : 0) << '\n';
    destroy_gpu_resources();

    const int rc = create_resources_locked();
    if (rc != 0) {
        surface_provider_ = {};
        return rc;
    }
    if (had_dataset) {
        const int rebuild_rc = rebuild_lod_locked();
        if (rebuild_rc != 0) {
            gs3d::util::log::error()
                << "[HeadlessRenderKernel] attach_surface: LOD rebuild failed rc="
                << rebuild_rc << " err=" << last_error_ << '\n';
            return -3;
        }
    }

    gs3d::util::log::info()
        << "[HeadlessRenderKernel] surface attached: " << config_.width << 'x'
        << config_.height << '\n';
    last_error_.clear();
    return 0;
}

void HeadlessRenderKernel::detach_surface() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!surface_provider_.valid()) {
        return;
    }
    surface_provider_ = {};
    const bool had_dataset = has_dataset_;
    destroy_gpu_resources();
    if (create_resources_locked() == 0 && had_dataset) {
        rebuild_lod_locked();
    }
}

bool HeadlessRenderKernel::has_surface() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return present_surface_ != nullptr && present_surface_->valid();
}

int HeadlessRenderKernel::present_frame() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_ || !present_surface_ || !present_pipeline_ ||
        !has_dataset_ || lod_.empty()) {
        set_error("present prerequisites missing");
        return -2;
    }

    bool submitted = false;
    try {
        gs3d::render::PresentSurface::Frame frame{};
        if (!present_surface_->begin_frame(frame)) {
            return -2;  // 交换链刚重建：跳过本帧
        }

        update_push_from_camera();
        update_tile_streaming_locked();

        drawn_level_ = choose_lod_index();
        const gs3d::render::PointCloudGpu& cloud = lod_.gpu_cloud(drawn_level_);
        drawn_point_count_ = cloud.point_count();

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check_vk(
            vkBeginCommandBuffer(present_command_, &begin), "begin present cmd");

        VkClearValue clears[2]{};
        clears[0].color = {{clear_r_, clear_g_, clear_b_, clear_a_}};
        clears[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo pass{};
        pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass.renderPass = present_surface_->render_pass();
        pass.framebuffer = frame.framebuffer;
        pass.renderArea.extent = frame.extent;
        pass.clearValueCount = 2;
        pass.pClearValues = clears;
        vkCmdBeginRenderPass(
            present_command_, &pass, VK_SUBPASS_CONTENTS_INLINE);

        // 合成顺序与离屏路径一致：coarsest 兜底 → 裁剪后的选中层 → 瓦片全精度。
        if (lod_.level_count() > 1) {
            const auto& coarsest = lod_.lowest_detail().gpu_cloud;
            if (!tile_clip_active_ ||
                coarsest.point_count() != cloud.point_count()) {
                present_pipeline_->draw(
                    present_command_, coarsest, frame.extent, push_);
            }
        }

        gs3d::render::PointPushConstants lod_push = push_;
        if (tile_clip_active_) {
            lod_push.flags |= gs3d::render::PointFlags::kSpatialClip;
            lod_push.clip_min[0] = tile_clip_bounds_.min_x;
            lod_push.clip_min[1] = tile_clip_bounds_.min_y;
            lod_push.clip_min[2] = tile_clip_bounds_.min_z;
            lod_push.clip_max[0] = tile_clip_bounds_.max_x;
            lod_push.clip_max[1] = tile_clip_bounds_.max_y;
            lod_push.clip_max[2] = tile_clip_bounds_.max_z;
        }
        present_pipeline_->draw(present_command_, cloud, frame.extent, lod_push);

        if (!tile_drawable_ids_.empty()) {
            present_pipeline_->bind_for_viewport(present_command_, frame.extent);
            for (const auto tile_id : tile_drawable_ids_) {
                const auto& tile_cloud = tile_gpu_.gpu_cloud_for_tile(tile_id);
                // 瓦片永远用无裁剪 push（kSpatialClip 会丢弃 bbox 内部）。
                present_pipeline_->draw_per_tile(
                    present_command_, tile_cloud, push_);
                tile_drawn_points_ += tile_cloud.point_count();
            }
        }

        vkCmdEndRenderPass(present_command_);
        check_vk(vkEndCommandBuffer(present_command_), "end present cmd");

        check_vk(
            vkResetFences(context_->device(), 1, &present_fence_),
            "reset present fence"
        );
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &present_command_;
        check_vk(
            vkQueueSubmit(context_->graphics_queue(), 1, &submit, present_fence_),
            "submit present cmd"
        );
        submitted = true;
        check_vk(
            vkWaitForFences(
                context_->device(), 1, &present_fence_, VK_TRUE, UINT64_MAX),
            "wait present"
        );
        submitted = false;

        present_surface_->end_frame(frame);
        track_frame_timing_locked();
        last_error_.clear();
        return 0;
    } catch (const std::exception& ex) {
        if (submitted && context_ && present_fence_ != VK_NULL_HANDLE) {
            vkWaitForFences(
                context_->device(), 1, &present_fence_, VK_TRUE, UINT64_MAX);
        }
        set_error(ex.what());
        return -3;
    }
}

void HeadlessRenderKernel::update_push_from_camera() {
    const auto mvp = camera_.view_projection_matrix();
    std::memcpy(push_.mvp, mvp.data(), sizeof(float) * 16);
}

void HeadlessRenderKernel::set_point_size(float size) {
    std::lock_guard<std::mutex> lock(mutex_);
    point_size_ = std::max(0.5f, size);
    push_.point_size = point_size_;
}

void HeadlessRenderKernel::set_colormap_index(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    colormap_index_ = std::clamp(index, 0, 8);
    const auto clamped = static_cast<std::uint32_t>(colormap_index_);
    push_.flags = (push_.flags & ~gs3d::render::PointFlags::kColormapMask) |
                  ((clamped & 0x7Fu) << 1);
}

void HeadlessRenderKernel::set_value_clip(bool enabled, float min_v, float max_v) {
    std::lock_guard<std::mutex> lock(mutex_);
    value_clip_enabled_ = enabled ? 1 : 0;
    value_clip_min_ = min_v;
    value_clip_max_ = max_v;
    if (enabled) {
        push_.flags |= gs3d::render::PointFlags::kValueClip;
    } else {
        push_.flags &= ~gs3d::render::PointFlags::kValueClip;
    }
    // value 通道在 shader 中按 color_min/range 归一；clip 用归一区间。
    const float range = (max_v > min_v) ? (max_v - min_v) : 1.0f;
    // 与 Viewer 对齐：clip_min.w / clip_max.w 存归一后的 value clip
    push_.clip_min[3] = 0.0f;
    push_.clip_max[3] = 1.0f;
    push_.color_min = min_v;
    push_.color_range = range;
}

void HeadlessRenderKernel::set_point_shape(int shape) {
    std::lock_guard<std::mutex> lock(mutex_);
    point_shape_ = std::clamp(shape, 0, 3);
    const auto s = static_cast<std::uint32_t>(point_shape_);
    push_.flags = (push_.flags & ~gs3d::render::PointFlags::kPointShapeMask) |
                  (s << gs3d::render::PointFlags::kPointShapeShift);
}

void HeadlessRenderKernel::set_height_source(int source) {
    std::lock_guard<std::mutex> lock(mutex_);
    height_source_ = source == 1 ? 1 : 0;
    push_.height_source = static_cast<std::uint32_t>(height_source_);
}

void HeadlessRenderKernel::set_color_source(int source) {
    std::lock_guard<std::mutex> lock(mutex_);
    color_source_ = source == 1 ? 1 : 0;
    push_.color_source = static_cast<std::uint32_t>(color_source_);
}

void HeadlessRenderKernel::set_height_exaggeration(float mult) {
    std::lock_guard<std::mutex> lock(mutex_);
    height_mult_ = std::clamp(mult, 0.01f, 100.0f);
    push_.height_mult = height_mult_;
}

void HeadlessRenderKernel::set_background(float r, float g, float b, float a) {
    std::lock_guard<std::mutex> lock(mutex_);
    clear_r_ = std::clamp(r, 0.0f, 1.0f);
    clear_g_ = std::clamp(g, 0.0f, 1.0f);
    clear_b_ = std::clamp(b, 0.0f, 1.0f);
    clear_a_ = std::clamp(a, 0.0f, 1.0f);
    has_custom_clear_ = true;
    gs3d::render::ClearColor cc;
    cc.r = clear_r_;
    cc.g = clear_g_;
    cc.b = clear_b_;
    cc.a = clear_a_;
    framebuffer_.set_clear_color(cc);
}

int HeadlessRenderKernel::export_dataset(
    const std::string& path,
    const std::string& format
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_dataset_) {
        set_error("export: no dataset");
        return -3;
    }
    try {
        const auto fs_path = std::filesystem::u8path(path);
        const auto parent = fs_path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        const bool csv = format == "csv" || format == "CSV";
        std::ofstream out(fs_path, std::ios::binary);
        if (!out) {
            set_error("export: cannot open " + path);
            return -2;
        }
        const auto& pts = dataset_.points();
        const std::size_t n = pts.size();
        if (csv) {
            out << "x,y,z,value\n";
            out.setf(std::ios::fixed);
            out.precision(6);
            for (std::size_t i = 0; i < n; ++i) {
                const auto& p = pts[i];
                out << p.x << ',' << p.y << ',' << p.z << ',' << p.value
                    << '\n';
            }
        } else {
            // 简化 PLY（ascii）：与预处理导出字段对齐
            out << "ply\nformat ascii 1.0\n"
                << "element vertex " << n << "\n"
                << "property float x\nproperty float y\nproperty float z\n"
                << "property float value\nend_header\n";
            out.setf(std::ios::fixed);
            out.precision(6);
            for (std::size_t i = 0; i < n; ++i) {
                const auto& p = pts[i];
                out << p.x << ' ' << p.y << ' ' << p.z << ' ' << p.value
                    << '\n';
            }
        }
        return 0;
    } catch (const std::exception& ex) {
        set_error(ex.what());
        return -3;
    }
}

int HeadlessRenderKernel::render_rgba(
    std::uint8_t* out_rgba,
    std::uint32_t max_bytes,
    std::uint32_t* out_w,
    std::uint32_t* out_h
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_ || !pipeline_ || !has_dataset_ || lod_.empty()) {
        set_error("render prerequisites missing");
        return -2;
    }
    // 提交后若中途抛异常，必须先把栅栏等回来再退出：下一帧要复用同一条
    // 命令缓冲与栅栏，留着仍在执行的 GPU 工作会直接冲突。
    bool submitted = false;
    try {
        const auto extent = framebuffer_.extent();
        const std::uint32_t w = extent.width;
        const std::uint32_t h = extent.height;
        const std::size_t bytes = static_cast<std::size_t>(w) * h * 4u;
        if (out_w) *out_w = w;
        if (out_h) *out_h = h;
        if (out_rgba == nullptr || max_bytes < bytes) {
            set_error("output buffer too small");
            return -1;
        }

        ensure_frame_resources(bytes);
        const VkDevice device = context_->device();

        update_push_from_camera();

        // 命令池带 RESET_COMMAND_BUFFER_BIT：vkBeginCommandBuffer 隐式重置，
        // 命令缓冲跨帧复用，不再每帧分配/释放。
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check_vk(vkBeginCommandBuffer(frame_command_, &begin), "begin cmd");

        // 每帧按点预算选层：绘制量与会话数据量解耦（千万级数据的关键）。
        drawn_level_ = choose_lod_index();
        const gs3d::render::PointCloudGpu& cloud = lod_.gpu_cloud(drawn_level_);
        drawn_point_count_ = cloud.point_count();

        /*
         * 合成顺序（与旧 Viewer 一致）：
         *   1) 最粗一层兜底 —— 任何时刻不留空洞；
         *   2) 选中 LOD 层 —— 全套所需 tile 驻留时按 tile 并集包围盒裁剪，
         *      否则宁可重叠也不裁剪（避免出现洞）；
         *   3) 细尺度 tile —— 全精度原始点叠加在上面。
         */
        update_tile_streaming_locked();

        framebuffer_.render(frame_command_, [&](VkCommandBuffer c) {
            const bool has_tiles = !tile_drawable_ids_.empty();
            if (lod_.level_count() > 1) {
                const auto& coarsest = lod_.lowest_detail().gpu_cloud;
                if (!tile_clip_active_ ||
                    coarsest.point_count() != cloud.point_count()) {
                    pipeline_->draw(c, coarsest, extent, push_);
                }
            }

            gs3d::render::PointPushConstants lod_push = push_;
            if (tile_clip_active_) {
                lod_push.flags |= gs3d::render::PointFlags::kSpatialClip;
                lod_push.clip_min[0] = tile_clip_bounds_.min_x;
                lod_push.clip_min[1] = tile_clip_bounds_.min_y;
                lod_push.clip_min[2] = tile_clip_bounds_.min_z;
                lod_push.clip_max[0] = tile_clip_bounds_.max_x;
                lod_push.clip_max[1] = tile_clip_bounds_.max_y;
                lod_push.clip_max[2] = tile_clip_bounds_.max_z;
            }
            pipeline_->draw(c, cloud, extent, lod_push);

            if (has_tiles) {
                pipeline_->bind_for_viewport(c, extent);
                for (const auto tile_id : tile_drawable_ids_) {
                    const auto& tile_cloud = tile_gpu_.gpu_cloud_for_tile(tile_id);
                    /*
                     * 瓦片必须用「无裁剪」的 push constants（push_），不能用 lod_push：
                     * kSpatialClip 的语义是丢弃 bbox 内部，而瓦片的每个点都在
                     * 自己的并集 bbox 内 —— 误用会把叠加层全部裁掉，
                     * 加上 LOD 也在该区域被裁剪，画面直接变全黑（已发生过一次）。
                     */
                    pipeline_->draw_per_tile(c, tile_cloud, push_);
                    tile_drawn_points_ += tile_cloud.point_count();
                }
            }
        });

        // 离屏 pass 结束于只读布局，回读前必须显式转换为传输源。
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = framebuffer_.color_image();
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            frame_command_,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier
        );

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = VkExtent3D{w, h, 1};
        vkCmdCopyImageToBuffer(
            frame_command_,
            framebuffer_.color_image(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            staging_buffer_,
            1,
            &region
        );

        // 保持 OffscreenFramebuffer 对外的只读布局约定。
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(frame_command_, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkMemoryBarrier host_barrier{};
        host_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        host_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(frame_command_, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &host_barrier, 0, nullptr, 0, nullptr);

        check_vk(vkEndCommandBuffer(frame_command_), "end cmd");

        check_vk(vkResetFences(device, 1, &frame_fence_), "reset frame fence");
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &frame_command_;
        check_vk(
            vkQueueSubmit(context_->graphics_queue(), 1, &submit, frame_fence_),
            "submit"
        );
        submitted = true;
        check_vk(
            vkWaitForFences(device, 1, &frame_fence_, VK_TRUE, UINT64_MAX),
            "wait readback"
        );
        submitted = false;

        // staging 缓冲常驻映射：省掉每帧 map/unmap 的驱动往返。
        std::memcpy(out_rgba, staging_mapped_, bytes);

        last_rgba_.assign(out_rgba, out_rgba + bytes);
        last_w_ = w;
        last_h_ = h;
        track_frame_timing_locked();

        last_error_.clear();
        return 0;
    } catch (const std::exception& ex) {
        if (submitted && context_ && frame_fence_ != VK_NULL_HANDLE) {
            vkWaitForFences(
                context_->device(), 1, &frame_fence_, VK_TRUE, UINT64_MAX
            );
        }
        set_error(ex.what());
        return -3;
    }
}

int HeadlessRenderKernel::encode_last_frame_png(
    std::uint8_t* out,
    std::uint32_t max_bytes,
    std::uint32_t* out_len
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (out == nullptr || out_len == nullptr) return -1;
    if (last_rgba_.empty() || last_w_ == 0 || last_h_ == 0) {
        set_error("png encode: no frame rendered");
        return -2;
    }
    struct PngBuffer {
        std::uint8_t* base;
        std::uint32_t cap;
        std::uint32_t len;
        bool overflow;
    } buf{out, max_bytes, 0, false};
    const auto writer = [](void* ctx, void* data, int size) {
        auto* b = static_cast<PngBuffer*>(ctx);
        if (size <= 0) return;
        if (b->overflow ||
            static_cast<std::uint32_t>(size) > b->cap - b->len) {
            b->overflow = true;
            return;
        }
        std::memcpy(b->base + b->len, data, static_cast<std::size_t>(size));
        b->len += static_cast<std::uint32_t>(size);
    };
    const int ok = stbi_write_png_to_func(
        writer, &buf, static_cast<int>(last_w_), static_cast<int>(last_h_),
        4, last_rgba_.data(), static_cast<int>(last_w_) * 4
    );
    if (!ok || buf.overflow) {
        set_error("png encode failed or buffer too small");
        return buf.overflow ? -1 : -3;
    }
    *out_len = buf.len;
    last_error_.clear();
    return 0;
}

int HeadlessRenderKernel::pick_point(
    float screen_x, float screen_y, float tolerance_px,
    float* out_point_xyzv
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (out_point_xyzv == nullptr) return -1;
    if (!has_dataset_) {
        set_error("pick: no dataset");
        return -3;
    }
    const auto& pts = dataset_.points();
    if (pts.empty()) return 0;

    const gs3d::camera::Viewport vp{
        config_.width == 0 ? 1u : config_.width,
        config_.height == 0 ? 1u : config_.height,
    };
    const auto mvp = camera_.view_projection_matrix();
    const float tol = tolerance_px > 0.0f ? tolerance_px : 20.0f;
    const float tol_sq = tol * tol;

    float best_sq = tol_sq + 1.0f; // 必须严格落在容差圆内
    int best_idx = -1;

    for (std::size_t i = 0; i < pts.size(); ++i) {
        const auto& p = pts[i];
        const auto sp = gs3d::camera::MouseRay::world_to_screen(
            mvp, p.x, p.y, p.z,
            static_cast<float>(vp.width), static_cast<float>(vp.height)
        );
        if (!sp.has_value()) continue;
        const float dx = sp->x - screen_x;
        const float dy = sp->y - screen_y;
        const float dsq = dx * dx + dy * dy;
        if (dsq <= tol_sq && dsq < best_sq) {
            best_sq = dsq;
            best_idx = static_cast<int>(i);
        }
    }

    if (best_idx < 0) return 0; // 未命中：容差圆内无点
    const auto& hit = pts[static_cast<std::size_t>(best_idx)];
    out_point_xyzv[0] = hit.x;
    out_point_xyzv[1] = hit.y;
    out_point_xyzv[2] = hit.z;
    out_point_xyzv[3] = hit.value;
    last_error_.clear();
    return 1;
}

int HeadlessRenderKernel::gizmo_axis(int axis) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_dataset_) {
        set_error("gizmo: no dataset");
        return -3;
    }
    if (axis < 0 || axis > 5) return -1;

    const gs3d::camera::CameraBounds bounds{
        {dataset_.bbox_min_x(), dataset_.bbox_min_y(), dataset_.bbox_min_z()},
        {dataset_.bbox_max_x(), dataset_.bbox_max_y(), dataset_.bbox_max_z()},
    };
    const float cx = (bounds.min.x + bounds.max.x) * 0.5f;
    const float cy = (bounds.min.y + bounds.max.y) * 0.5f;
    const float cz = (bounds.min.z + bounds.max.z) * 0.5f;
    const float span = std::max({
        bounds.max.x - bounds.min.x,
        bounds.max.y - bounds.min.y,
        bounds.max.z - bounds.min.z,
        1.0f,
    });
    const float dist = span * 1.8f;

    float px = cx, py = cy, pz = cz + dist;
    float up_x = 0.0f, up_y = 0.0f, up_z = 1.0f;
    switch (axis) {
        case 0: px = cx - dist; py = cy; pz = cz; break; // -X
        case 1: px = cx + dist; py = cy; pz = cz; break; // +X
        case 2: px = cx; py = cy - dist; pz = cz; break; // -Y
        case 3: px = cx; py = cy + dist; pz = cz; break; // +Y
        case 4: // +Z top：视线沿 -Z，up 必须用 Y 避免奇异
            px = cx; py = cy - dist * 0.001f; pz = cz + dist;
            up_x = 0.0f; up_y = 1.0f; up_z = 0.0f;
            break;
        case 5: // -Z bottom
            px = cx; py = cy - dist * 0.001f; pz = cz - dist;
            up_x = 0.0f; up_y = 1.0f; up_z = 0.0f;
            break;
        default: break;
    }
    camera_.set_position({px, py, pz});
    camera_.set_target({cx, cy, cz});
    camera_.set_up({up_x, up_y, up_z});
    camera_.look_at({px, py, pz}, {cx, cy, cz}, {up_x, up_y, up_z});
    update_push_from_camera();
    last_error_.clear();
    return 0;
}

int HeadlessRenderKernel::clear_tile_cache() {
    std::lock_guard<std::mutex> lock(mutex_);
    last_rgba_.clear();
    last_w_ = last_h_ = 0;
    last_frame_tick_ = 0;
    estimated_fps_ = 0.0f;
    if (context_ && has_dataset_) {
        try {
            vkDeviceWaitIdle(context_->device());
            const int rc = rebuild_lod_locked();
            if (rc != 0) {
                return rc;
            }
        } catch (const std::exception& ex) {
            set_error(ex.what());
            return -3;
        }
    }
    last_error_.clear();
    return 0;
}

HeadlessRenderKernel::CameraState HeadlessRenderKernel::camera_state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    CameraState st;
    const auto& pos = camera_.position();
    const auto& tgt = camera_.target();
    st.position[0] = pos.x;
    st.position[1] = pos.y;
    st.position[2] = pos.z;
    st.target[0] = tgt.x;
    st.target[1] = tgt.y;
    st.target[2] = tgt.z;
    st.distance = camera_.distance();
    const float dx = tgt.x - pos.x;
    const float dy = tgt.y - pos.y;
    const float dz = tgt.z - pos.z;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 1e-6f) {
        st.elevation_deg = std::asin(std::clamp(dz / len, -1.0f, 1.0f)) *
                           180.0f / 3.14159265358979323846f;
        st.azimuth_deg = std::atan2(dy, dx) * 180.0f / 3.14159265358979323846f;
    }
    st.zoom = st.distance > 1e-3f ? (span_or_default() / st.distance) : 1.0f;
    if (has_dataset_) {
        const float cx = (dataset_.bbox_min_x() + dataset_.bbox_max_x()) * 0.5f;
        const float cy = (dataset_.bbox_min_y() + dataset_.bbox_max_y()) * 0.5f;
        st.pan[0] = tgt.x - cx;
        st.pan[1] = tgt.y - cy;
    }
    return st;
}

HeadlessRenderKernel::RenderState HeadlessRenderKernel::render_state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    RenderState st;
    st.point_size = point_size_;
    st.colormap_index = colormap_index_;
    st.point_shape = point_shape_;
    st.height_source = height_source_;
    st.color_source = color_source_;
    st.height_mult = height_mult_;
    st.value_clip_enabled = value_clip_enabled_;
    st.value_clip_min = value_clip_min_;
    st.value_clip_max = value_clip_max_;
    return st;
}

HeadlessRenderKernel::ViewportState HeadlessRenderKernel::viewport_state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    ViewportState st;
    st.width = config_.width;
    st.height = config_.height;
    st.frame_ready = !last_rgba_.empty();
    st.fps = estimated_fps_;
    return st;
}

HeadlessRenderKernel::GpuDiagnostics HeadlessRenderKernel::gpu_diagnostics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    GpuDiagnostics diag;
    diag.profile = gs3d::render::kVulkanProfileName;
    if (!context_) {
        return diag;
    }

    const auto& gpus = context_->gpu_list();
    const std::size_t index = context_->active_gpu_index();
    if (index < gpus.size()) {
        const auto& gpu = gpus[index];
        diag.name = gpu.name;
        diag.type_label = gs3d::render::gpu_type_label(gpu.device_type);
        diag.uuid = gpu.device_uuid;
        diag.api_version = format_api_version(gpu.api_version);
        diag.driver_version = gpu.driver_version;
        diag.memory_bytes = gpu.device_local_memory_bytes;
    } else {
        diag.name = context_->physical_device_name();
    }
    diag.selection = context_->selection_summary();
    return diag;
}

HeadlessRenderKernel::LodState HeadlessRenderKernel::lod_state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    LodState state;
    state.source_points = point_count_;
    state.drawn_points = drawn_point_count_;
    state.budget = effective_point_budget();
    state.level_index = static_cast<int>(drawn_level_);
    state.level_count = static_cast<int>(
        std::max<std::size_t>(1, lod_.level_count())
    );
    state.gpu_points = gpu_point_count_;
    state.estimated_spacing = estimated_spacing_;
    state.build_ms = lod_build_ms_;
    state.upload_ms = lod_upload_ms_;
    state.automatic_budget = point_budget_ == 0;
    state.tiles_available = tile_reader_ != nullptr && tile_reader_->valid();
    if (state.tiles_available) {
        state.tile_count = tile_reader_->tile_count();
        state.tile_needed = tile_needed_ids_.size();
        state.tile_resident = tile_gpu_.tile_count();
        state.tile_drawn_points = tile_drawn_points_;
        state.tile_uploaded_bytes_frame = tile_uploaded_bytes_frame_;
        state.tile_clip_active = tile_clip_active_;
        state.tiles_fully_resident =
            !tile_needed_ids_.empty() &&
            tile_drawable_ids_.size() == tile_needed_ids_.size();
    }
    return state;
}

float HeadlessRenderKernel::span_or_default() const {
    if (!has_dataset_) return 100.0f;
    const float dx = dataset_.bbox_max_x() - dataset_.bbox_min_x();
    const float dy = dataset_.bbox_max_y() - dataset_.bbox_min_y();
    const float dz = dataset_.bbox_max_z() - dataset_.bbox_min_z();
    return std::max({dx, dy, dz, 1.0f});
}

void HeadlessRenderKernel::track_frame_timing_locked() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    if (last_frame_tick_ > 0) {
        const float dt = static_cast<float>(ms - last_frame_tick_);
        if (dt > 0.5f) {
            const float inst = 1000.0f / dt;
            estimated_fps_ = estimated_fps_ <= 0.0f
                                 ? inst
                                 : estimated_fps_ * 0.8f + inst * 0.2f;
        }
    }
    last_frame_tick_ = ms;
}

}  // namespace gs3d::ffi
