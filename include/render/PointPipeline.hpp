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

    float value_min   = 0.0f;  // offset  64
    float value_range = 1.0f;  // offset  68
    float point_size  = 1.0f;  // offset  72

    /*
     * clip_mode:
     *   0 = no clipping
     *   1 = discard points whose world-space XYZ is INSIDE [clip_min, clip_max]
     *       (used for LOD draw to suppress the region covered by full-res tiles)
     */
    float clip_mode = 0.0f;    // offset  76

    /*
     * clip_min[4] / clip_max[4]: vec3 + 1 float padding.
     * Matches std430 vec3 layout: vec3 has base alignment 16, so each takes 16 bytes.
     * [0]=x  [1]=y  [2]=z  [3]=padding (must be written but ignored in shader)
     */
    float clip_min[4] = {};    // offset  80, size 16
    float clip_max[4] = {};    // offset  96, size 16
};                             // total: 112 bytes

static_assert(
    sizeof(PointPushConstants) == 112,
    "PointPushConstants must be 112 bytes"
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