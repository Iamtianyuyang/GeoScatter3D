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

    float value_min   = 0.0f;  // offset  64  min of currently active attribute
    float value_range = 1.0f;  // offset  68  range of currently active attribute
    float point_size  = 1.0f;  // offset  72

    /*
     * clip_mode:
     *   0 = no clipping
     *   1 = discard points whose world-space XYZ is INSIDE [clip_min, clip_max]
     */
    float clip_mode = 0.0f;    // offset  76

    /*
     * clip_min[4] / clip_max[4]: vec3 + 1 float padding.
     * Using vec4 (not vec3) in GLSL to guarantee 16-byte alignment and
     * eliminate std430 ambiguity between C++ and GLSL layouts.
     * [0]=x  [1]=y  [2]=z  [3]=padding
     */
    float clip_min[4] = {};    // offset  80, size 16
    float clip_max[4] = {};    // offset  96, size 16

    /*
     * attr_index: which point attribute to use for color mapping.
     *   0 = value field  (amplitude / imported attribute)
     *   1 = z coordinate (elevation / depth)
     * Switching is zero-cost: only this push constant changes, no GPU re-upload.
     * Mirrors Potree's activeAttributeName + CloudCompare scalar field selection.
     */
    std::uint32_t attr_index = 0; // offset 112
    float _pad[3] = {};           // offset 116, pad to 128 bytes
};                                // total: 128 bytes

static_assert(
    sizeof(PointPushConstants) == 128,
    "PointPushConstants must be 128 bytes"
);

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