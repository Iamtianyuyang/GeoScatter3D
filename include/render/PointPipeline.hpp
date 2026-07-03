#pragma once

#include "render/PointCloudGpu.hpp"
#include "render/VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <filesystem>

namespace gs3d::render {

struct PointPushConstants {
    float mvp[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };                          // offset   0, size 64

    /*
     * clip_min[4] / clip_max[4]: vec3 + 1 float padding.
     * Moved before channel params to keep 16-byte alignment.
     *   .xyz = spatial clip bbox (world-space bounds)
     *   .w   = value clip range (normalized [0,1]):
     *          clip_min.w = value_clip_min, clip_max.w = value_clip_max
     */
    float clip_min[4] = {};     // offset  64, size 16
    float clip_max[4] = {};     // offset  80, size 16

    // ---- 颜色通道 (offset 96) ----
    float color_min   = 0.0f;   // min of the selected color attribute
    float color_range = 1.0f;   // range of the selected color attribute

    // ---- 高度通道 (offset 104) ----
    // height = height_offset + raw_value * height_mult
    // CPU 端预计算：Z 属性 → offset=0, mult=1；非空间属性 → 线性映射到高程范围
    float height_offset = 0.0f;
    float height_mult   = 1.0f;

    float point_size = 1.0f;   // offset 112

    /*
     * height_source / color_source: AttrPhysicalSource 枚举值。
     * shader 不关心是 "fold" 还是 "elevation"，
     * 只根据 source 值决定从 in_position.z 还是 in_value 读取。
     * 将来加 Attr2=2 时 shader 加一个 else-if 分支即可。
     */
    std::uint32_t height_source = 0;  // offset 116, 默认 Z (高程)
    std::uint32_t color_source  = 1;  // offset 120, 默认 Value (fold)

    /*
     * flags: bit-packed render control.
     *   bit 0     = spatial_clip_enable (1=discard inside clip bbox)
     *   bits 1–7  = colormap_index (0–127)
     *   bit 8     = value_clip_enable (1=discard outside [value_clip_min, value_clip_max])
     *
     * Colormap indices:
     *   0 = Geo     (blue-cyan-green-yellow-red)
     *   1 = Viridis (perceptually uniform)
     *   2 = Jet     (classic rainbow)
     *   3 = Gray    (grayscale)
     *   4 = Thermal (black-red-yellow-white)
     *   5 = Coolwarm (blue-white-red, diverging)
     *   6 = Turbo
     *   7 = Plasma
     */
    std::uint32_t flags = 0;    // offset 124
};                                // total: 128 bytes

static_assert(
    sizeof(PointPushConstants) == 128,
    "PointPushConstants must be 128 bytes"
);
static_assert(offsetof(PointPushConstants, clip_min)  == 64,
    "PointPushConstants: clip_min must be at offset 64");
static_assert(offsetof(PointPushConstants, clip_max)  == 80,
    "PointPushConstants: clip_max must be at offset 80");
static_assert(offsetof(PointPushConstants, flags) == 124,
    "PointPushConstants: flags must be at offset 124");

// Bit masks for the flags field.
namespace PointFlags {
inline constexpr std::uint32_t kSpatialClip  = 1u << 0;
inline constexpr std::uint32_t kColormapMask = 0x7Fu << 1;  // bits 1–7
inline constexpr std::uint32_t kValueClip    = 1u << 8;
} // namespace PointFlags

struct PointPipelineConfig {
    std::filesystem::path vertex_shader_path =
        "assets/shaders/point.vert.spv";

    std::filesystem::path fragment_shader_path =
        "assets/shaders/point.frag.spv";
};

class PointPipeline {
public:
    PointPipeline(
        const VulkanContext& context,
        VkRenderPass render_pass,
        const PointPipelineConfig& config = {}
    );

    ~PointPipeline();

    PointPipeline(const PointPipeline&) = delete;
    PointPipeline& operator=(const PointPipeline&) = delete;

    PointPipeline(PointPipeline&&) = delete;
    PointPipeline& operator=(PointPipeline&&) = delete;

    void draw(
        VkCommandBuffer command_buffer,
        const PointCloudGpu& point_cloud,
        VkExtent2D extent,
        const PointPushConstants& push_constants
    ) const;

    // Bind pipeline + viewport + scissor once, then call draw_per_tile() per tile.
    // Avoids redundant state binding in tile loops (20+ tiles per viewport).
    void bind_for_viewport(
        VkCommandBuffer command_buffer,
        VkExtent2D extent
    ) const;

    void draw_per_tile(
        VkCommandBuffer command_buffer,
        const PointCloudGpu& point_cloud,
        const PointPushConstants& push_constants
    ) const;

    [[nodiscard]]
    VkPipeline pipeline() const noexcept;

    [[nodiscard]]
    VkPipelineLayout pipeline_layout() const noexcept;

private:
    const VulkanContext& context_;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

private:
    void create_graphics_pipeline(
        VkRenderPass render_pass,
        const PointPipelineConfig& config
    );

    [[nodiscard]]
    VkShaderModule create_shader_module(
        const std::filesystem::path& path
    ) const;

    [[nodiscard]]
    static std::vector<char> read_binary_file(
        const std::filesystem::path& path
    );
};

} // namespace gs3d::render