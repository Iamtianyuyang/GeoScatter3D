#include "ffi/HeadlessRenderKernel.hpp"

#include "camera/CameraController.hpp"
#include "camera/MouseRay.hpp"
#include "data/PointDataAdapters.hpp"
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

// 回读的临时对象必须在每个异常出口释放；提交后先等待设备停止使用它们。
struct ReadbackResources {
    VkDevice device;
    VkCommandPool pool;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void* mapped = nullptr;
    bool pending = false;

    ReadbackResources(VkDevice d, VkCommandPool p) : device(d), pool(p) {}
    ReadbackResources(const ReadbackResources&) = delete;
    ReadbackResources& operator=(const ReadbackResources&) = delete;
    ~ReadbackResources() {
        if (pending) vkDeviceWaitIdle(device);
        if (mapped) vkUnmapMemory(device, memory);
        if (fence) vkDestroyFence(device, fence, nullptr);
        if (command) vkFreeCommandBuffers(device, pool, 1, &command);
        if (buffer) vkDestroyBuffer(device, buffer, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
    }
};

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
    try {
        destroy_gpu_resources();
        config_ = config;
        if (config_.width == 0 || config_.height == 0) {
            set_error("invalid viewport size");
            return -1;
        }

        gs3d::render::VulkanContextConfig ctx_cfg;
        ctx_cfg.headless = true;
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

        camera_.set_viewport(config_.width, config_.height);
        camera_.set_perspective(45.0f, 1.0f, 100000.0f);
        last_error_.clear();
        return 0;
    } catch (const std::exception& ex) {
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
    cloud_ = gs3d::render::PointCloudGpu{};
    pipeline_.reset();
    framebuffer_.destroy();
    if (context_ && command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_->device(), command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }
    context_.reset();
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
        if (std::filesystem::is_directory(fs_path)) {
            fs_path = fs_path / "source.gs3d";
            if (!std::filesystem::exists(fs_path)) {
                set_error("bundle missing source.gs3d");
                return -2;
            }
        }

        auto loaded = gs3d::data::Gs3dDatasetLoader::load(fs_path);
        if (!loaded.has_point_data() || loaded.empty()) {
            set_error("dataset has no point data");
            return -3;
        }
        dataset_ = std::move(loaded);
        point_count_ = dataset_.point_count();
        has_dataset_ = true;

        const auto view = gs3d::data::make_point_data_view(dataset_.points());
        cloud_.upload(*context_, command_pool_, context_->graphics_queue(), view);

        reset_camera_to_bounds_locked();
        last_error_.clear();
        return 0;
    } catch (const std::exception& ex) {
        set_error(ex.what());
        has_dataset_ = false;
        return -3;
    }
}

int HeadlessRenderKernel::load_sample() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_) {
        set_error("load_sample: engine not created");
        return -3;
    }
    try {
        const int grid_size = 100;
        const float range = 20.0f;
        std::vector<gs3d::data::Gs3dPoint> pts;
        pts.reserve(grid_size * grid_size);

        float min_x = 1e9f, max_x = -1e9f;
        float min_y = 1e9f, max_y = -1e9f;
        float min_z = 1e9f, max_z = -1e9f;
        float min_val = 1e9f, max_val = -1e9f;

        for (int i = 0; i < grid_size; ++i) {
            const float x = -range + (2.0f * range * i) / (grid_size - 1);
            for (int j = 0; j < grid_size; ++j) {
                const float y = -range + (2.0f * range * j) / (grid_size - 1);
                const float r = std::sqrt(x * x + y * y);
                const float z = 6.0f * std::sin(r) / (r * 0.4f + 1.0f);
                const float val = z;

                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
                min_z = std::min(min_z, z);
                max_z = std::max(max_z, z);
                min_val = std::min(min_val, val);
                max_val = std::max(max_val, val);

                gs3d::data::Gs3dPoint p{};
                p.x = x;
                p.y = y;
                p.z = z;
                p.value = val;
                pts.push_back(p);
            }
        }

        gs3d::data::Gs3dHeader header{};
        std::memcpy(header.magic, gs3d::data::GS3D_MAGIC, 4);
        header.version = gs3d::data::GS3D_VERSION;
        header.header_size = gs3d::data::GS3D_HEADER_V2_SIZE;
        header.point_data_offset = header.header_size;
        header.point_count = pts.size();
        header.bbox_min_x = min_x; header.bbox_min_y = min_y; header.bbox_min_z = min_z;
        header.bbox_max_x = max_x; header.bbox_max_y = max_y; header.bbox_max_z = max_z;
        header.value_min = min_val;
        header.value_max = max_val;

        dataset_ = gs3d::data::Gs3dDataset(header, std::move(pts), "built-in://sample");
        point_count_ = dataset_.point_count();
        has_dataset_ = true;

        const auto view = gs3d::data::make_point_data_view(dataset_.points());
        cloud_.upload(*context_, command_pool_, context_->graphics_queue(), view);

        reset_camera_to_bounds_locked();
        last_error_.clear();
        return 0;
    } catch (const std::exception& ex) {
        set_error(ex.what());
        has_dataset_ = false;
        return -3;
    }
}

void HeadlessRenderKernel::unload_dataset() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (context_) {
        vkDeviceWaitIdle(context_->device());
        cloud_ = gs3d::render::PointCloudGpu{};
    }
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
    if (!context_ || !pipeline_ || !has_dataset_) {
        set_error("render prerequisites missing");
        return -2;
    }
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

        update_push_from_camera();

        ReadbackResources resources(context_->device(), command_pool_);
        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = command_pool_;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        auto& cmd = resources.command;
        check_vk(vkAllocateCommandBuffers(context_->device(), &alloc, &cmd),
                 "alloc cmd");

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check_vk(vkBeginCommandBuffer(cmd, &begin), "begin cmd");

        framebuffer_.render(cmd, [&](VkCommandBuffer c) {
            pipeline_->draw(c, cloud_, extent, push_);
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
            cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier
        );

        // 临时 staging buffer
        VkBufferCreateInfo buf_info{};
        buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buf_info.size = bytes;
        buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        auto& staging = resources.buffer;
        check_vk(vkCreateBuffer(context_->device(), &buf_info, nullptr, &staging),
                 "create staging");

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(context_->device(), staging, &req);
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
            set_error("no host-visible memory for staging");
            return -3;
        }
        VkMemoryAllocateInfo mem_alloc{};
        mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mem_alloc.allocationSize = req.size;
        mem_alloc.memoryTypeIndex = mem_type;
        auto& staging_mem = resources.memory;
        if (vkAllocateMemory(context_->device(), &mem_alloc, nullptr, &staging_mem) !=
            VK_SUCCESS) {
            set_error("staging alloc failed");
            return -3;
        }
        check_vk(vkBindBufferMemory(context_->device(), staging, staging_mem, 0),
                 "bind staging");

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = VkExtent3D{w, h, 1};
        vkCmdCopyImageToBuffer(
            cmd,
            framebuffer_.color_image(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            staging,
            1,
            &region
        );

        // 保持 OffscreenFramebuffer 对外的只读布局约定。
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkMemoryBarrier host_barrier{};
        host_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        host_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &host_barrier, 0, nullptr, 0, nullptr);

        check_vk(vkEndCommandBuffer(cmd), "end cmd");

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        auto& fence = resources.fence;
        check_vk(vkCreateFence(context_->device(), &fence_info, nullptr, &fence),
                 "fence");
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        check_vk(vkQueueSubmit(context_->graphics_queue(), 1, &submit, fence),
                 "submit");
        resources.pending = true;
        check_vk(vkWaitForFences(context_->device(), 1, &fence, VK_TRUE, UINT64_MAX),
                 "wait readback");
        resources.pending = false;

        auto& mapped = resources.mapped;
        check_vk(vkMapMemory(context_->device(), staging_mem, 0, bytes, 0, &mapped),
                 "map");
        std::memcpy(out_rgba, mapped, bytes);
        vkUnmapMemory(context_->device(), staging_mem);
        mapped = nullptr;

        last_rgba_.assign(out_rgba, out_rgba + bytes);
        last_w_ = w;
        last_h_ = h;
        track_frame_timing_locked();

        last_error_.clear();
        return 0;
    } catch (const std::exception& ex) {
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
            cloud_ = gs3d::render::PointCloudGpu{};
            const auto view = gs3d::data::make_point_data_view(dataset_.points());
            cloud_.upload(*context_, command_pool_, context_->graphics_queue(), view);
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
