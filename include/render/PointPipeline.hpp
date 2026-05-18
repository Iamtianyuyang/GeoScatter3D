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
    };

    float value_min = 0.0f;
    float value_range = 1.0f;
    float point_size = 1.0f;
    float padding = 0.0f;
};

static_assert(
    sizeof(PointPushConstants) == 80,
    "PointPushConstants must be 80 bytes"
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