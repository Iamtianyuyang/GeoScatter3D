#include "render/PointPipeline.hpp"

#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace gs3d::render {

namespace {

void check_vk(VkResult result, const char* message) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(message);
    }
}

} // namespace

PointPipeline::PointPipeline(
    const VulkanContext& context,
    VkRenderPass render_pass,
    const PointPipelineConfig& config
)
    : context_(context)
{
    create_graphics_pipeline(render_pass, config);
}

PointPipeline::~PointPipeline() {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_.device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }

    if (pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context_.device(), pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }
}

void PointPipeline::draw(
    VkCommandBuffer command_buffer,
    const PointCloudGpu& point_cloud,
    VkExtent2D extent,
    const PointPushConstants& push_constants
) const {
    bind_for_viewport(command_buffer, extent);
    draw_per_tile(command_buffer, point_cloud, push_constants);
}

void PointPipeline::bind_for_viewport(
    VkCommandBuffer command_buffer,
    VkExtent2D extent
) const {
    vkCmdBindPipeline(
        command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_
    );

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
}

void PointPipeline::draw_per_tile(
    VkCommandBuffer command_buffer,
    const PointCloudGpu& point_cloud,
    const PointPushConstants& push_constants
) const {
    if (!point_cloud.valid()) {
        return;
    }

    vkCmdPushConstants(
        command_buffer,
        pipeline_layout_,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(PointPushConstants),
        &push_constants
    );

    VkBuffer vertex_buffers[] = {
        point_cloud.vertex_buffer()
    };

    VkDeviceSize offsets[] = {
        0
    };

    vkCmdBindVertexBuffers(
        command_buffer,
        0,
        1,
        vertex_buffers,
        offsets
    );

    vkCmdDraw(
        command_buffer,
        static_cast<std::uint32_t>(point_cloud.point_count()),
        1,
        0,
        0
    );
}

VkPipeline PointPipeline::pipeline() const noexcept {
    return pipeline_;
}

VkPipelineLayout PointPipeline::pipeline_layout() const noexcept {
    return pipeline_layout_;
}

void PointPipeline::create_graphics_pipeline(
    VkRenderPass render_pass,
    const PointPipelineConfig& config
) {
    const VkShaderModule vert_shader_module =
        create_shader_module(config.vertex_shader_path);

    const VkShaderModule frag_shader_module =
        create_shader_module(config.fragment_shader_path);

    VkPipelineShaderStageCreateInfo vert_stage_info{};
    vert_stage_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage_info.module = vert_shader_module;
    vert_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo frag_stage_info{};
    frag_stage_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage_info.module = frag_shader_module;
    frag_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        vert_stage_info,
        frag_stage_info
    };

    VkVertexInputBindingDescription binding_description{};
    binding_description.binding = 0;
    binding_description.stride = sizeof(PointVertex);
    binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attribute_descriptions[2]{};

    attribute_descriptions[0].binding = 0;
    attribute_descriptions[0].location = 0;
    attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descriptions[0].offset =
        static_cast<std::uint32_t>(
            offsetof(PointVertex, x)
        );

    attribute_descriptions[1].binding = 0;
    attribute_descriptions[1].location = 1;
    attribute_descriptions[1].format = VK_FORMAT_R32_SFLOAT;
    attribute_descriptions[1].offset =
        static_cast<std::uint32_t>(
            offsetof(PointVertex, value)
        );

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_description;
    vertex_input_info.vertexAttributeDescriptionCount = 2;
    vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = nullptr;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = nullptr;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    VkPushConstantRange push_constant_range{};
    push_constant_range.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT |
        VK_SHADER_STAGE_FRAGMENT_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PointPushConstants);

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 0;
    pipeline_layout_info.pSetLayouts = nullptr;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_constant_range;

    check_vk(
        vkCreatePipelineLayout(
            context_.device(),
            &pipeline_layout_info,
            nullptr,
            &pipeline_layout_
        ),
        "PointPipeline: failed to create pipeline layout"
    );

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType =
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;

    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable = VK_FALSE;
    pipeline_info.pDepthStencilState = &depth_stencil;

    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;

    pipeline_info.layout = pipeline_layout_;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;

    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    check_vk(
        vkCreateGraphicsPipelines(
            context_.device(),
            VK_NULL_HANDLE,
            1,
            &pipeline_info,
            nullptr,
            &pipeline_
        ),
        "PointPipeline: failed to create graphics pipeline"
    );

    vkDestroyShaderModule(context_.device(), frag_shader_module, nullptr);
    vkDestroyShaderModule(context_.device(), vert_shader_module, nullptr);
}

VkShaderModule PointPipeline::create_shader_module(
    const std::filesystem::path& path
) const {
    const auto code = read_binary_file(path);

    if (code.empty()) {
        throw std::runtime_error(
            "PointPipeline: shader file is empty: " + path.string()
        );
    }

    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size();
    create_info.pCode =
        reinterpret_cast<const std::uint32_t*>(code.data());

    VkShaderModule shader_module = VK_NULL_HANDLE;

    check_vk(
        vkCreateShaderModule(
            context_.device(),
            &create_info,
            nullptr,
            &shader_module
        ),
        "PointPipeline: failed to create shader module"
    );

    return shader_module;
}

std::vector<char> PointPipeline::read_binary_file(
    const std::filesystem::path& path
) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error(
            "PointPipeline: failed to open shader file: " + path.string()
        );
    }

    const auto file_size = file.tellg();

    if (file_size <= 0) {
        throw std::runtime_error(
            "PointPipeline: invalid shader file size: " + path.string()
        );
    }

    std::vector<char> buffer(static_cast<std::size_t>(file_size));

    file.seekg(0);
    file.read(buffer.data(), file_size);

    if (!file.good()) {
        throw std::runtime_error(
            "PointPipeline: failed to read shader file: " + path.string()
        );
    }

    return buffer;
}

} // namespace gs3d::render
