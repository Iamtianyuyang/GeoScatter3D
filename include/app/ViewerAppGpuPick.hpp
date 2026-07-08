#pragma once

#include "app/ViewerAppInternal.hpp"

#include "render/OffscreenFramebuffer.hpp"
#include "render/VulkanBuffer.hpp"
#include "render/VulkanContext.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gs3d::app {

class GpuPickReadback {
public:
    GpuPickReadback(
        const gs3d::render::VulkanContext& context,
        std::uint32_t frames_in_flight,
        std::size_t viewport_count
    )
        : context_(&context)
    {
        slots_.resize(frames_in_flight);
        for (auto& frame_slots : slots_) {
            frame_slots.resize(viewport_count);
        }
        for (auto& frame_slots : slots_) {
            for (auto& slot : frame_slots) {
                slot.id_buffer.create(
                    context,
                    sizeof(std::uint32_t) * kMaxPickPixels,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                );
                slot.depth_buffer.create(
                    context,
                    sizeof(float) * kMaxPickPixels,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                );
            }
        }
    }

    void record_request(
        VkCommandBuffer command_buffer,
        std::uint32_t frame_slot,
        const gs3d::render::OffscreenFramebuffer& framebuffer,
        const GpuPickRequest& request
    ) {
        if (frame_slot >= slots_.size() ||
            request.viewport_index < 0 ||
            static_cast<std::size_t>(request.viewport_index) >=
                slots_[frame_slot].size()) {
            return;
        }

        auto& slot =
            slots_[frame_slot][static_cast<std::size_t>(request.viewport_index)];
        slot.pending = {};

        if (!request.valid()) {
            return;
        }

        const auto extent = framebuffer.extent();
        if (extent.width == 0 || extent.height == 0) {
            return;
        }

        const int center_x = std::clamp(
            static_cast<int>(std::floor(request.mouse_x)),
            0,
            static_cast<int>(extent.width) - 1
        );
        // mouse_y is already in Vulkan framebuffer coordinates (top-left
        // origin, y down), matching both the viewport transform and
        // vkCmdCopyImageToBuffer's image layout. No Y-flip needed.
        const int center_y = std::clamp(
            static_cast<int>(std::floor(request.mouse_y)),
            0,
            static_cast<int>(extent.height) - 1
        );

        const int pick_radius_px = std::clamp(
            static_cast<int>(request.pick_radius_px),
            0,
            kPickRadiusPx
        );
        const std::uint32_t left =
            static_cast<std::uint32_t>(std::max(0, center_x - pick_radius_px));
        const std::uint32_t top =
            static_cast<std::uint32_t>(std::max(0, center_y - pick_radius_px));
        const std::uint32_t right =
            static_cast<std::uint32_t>(
                std::min(
                    static_cast<int>(extent.width) - 1,
                    center_x + pick_radius_px
                )
            );
        const std::uint32_t bottom =
            static_cast<std::uint32_t>(
                std::min(
                    static_cast<int>(extent.height) - 1,
                    center_y + pick_radius_px
                )
            );

        slot.pending.request = request;
        slot.pending.x = left;
        slot.pending.y = top;
        slot.pending.width = right - left + 1;
        slot.pending.height = bottom - top + 1;
        slot.pending.center_local_x =
            static_cast<std::uint32_t>(center_x) - left;
        slot.pending.center_local_y =
            static_cast<std::uint32_t>(center_y) - top;
        slot.pending.pending = true;

        VkBufferImageCopy copy_region{};
        copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_region.imageSubresource.mipLevel = 0;
        copy_region.imageSubresource.baseArrayLayer = 0;
        copy_region.imageSubresource.layerCount = 1;
        copy_region.imageOffset = {
            static_cast<std::int32_t>(slot.pending.x),
            static_cast<std::int32_t>(slot.pending.y),
            0
        };
        copy_region.imageExtent = {
            slot.pending.width,
            slot.pending.height,
            1
        };

        vkCmdCopyImageToBuffer(
            command_buffer,
            framebuffer.pick_image(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            slot.id_buffer.handle(),
            1,
            &copy_region
        );

        vkCmdCopyImageToBuffer(
            command_buffer,
            framebuffer.pick_depth_image(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            slot.depth_buffer.handle(),
            1,
            &copy_region
        );
    }

    [[nodiscard]]
    std::vector<GpuPickResult> collect_ready_frame(std::uint32_t frame_slot) {
        std::vector<GpuPickResult> results;
        if (context_ == nullptr || frame_slot >= slots_.size()) {
            return results;
        }

        for (auto& slot : slots_[frame_slot]) {
            if (!slot.pending.pending) {
                continue;
            }

            const auto pixel_count =
                slot.pending.width * slot.pending.height;
            if (pixel_count == 0 || pixel_count > kMaxPickPixels) {
                slot.pending = {};
                continue;
            }

            void* id_memory = nullptr;
            void* depth_memory = nullptr;
            vkMapMemory(
                context_->device(),
                slot.id_buffer.memory(),
                0,
                sizeof(std::uint32_t) * pixel_count,
                0,
                &id_memory
            );
            vkMapMemory(
                context_->device(),
                slot.depth_buffer.memory(),
                0,
                sizeof(float) * pixel_count,
                0,
                &depth_memory
            );

            const auto* point_ids =
                static_cast<const std::uint32_t*>(id_memory);
            const auto* depths =
                static_cast<const float*>(depth_memory);

            GpuPickResult result;
            result.request = slot.pending.request;
            result.depth = 1.0f;
            std::uint32_t best_distance_sq =
                std::numeric_limits<std::uint32_t>::max();
            for (std::uint32_t y = 0; y < slot.pending.height; ++y) {
                for (std::uint32_t x = 0; x < slot.pending.width; ++x) {
                    const std::uint32_t i =
                        y * slot.pending.width + x;
                    const auto point_id = point_ids[i];
                    if (point_id == 0) {
                        continue;
                    }

                    const float depth = depths[i];
                    const int dx =
                        static_cast<int>(x) -
                        static_cast<int>(slot.pending.center_local_x);
                    const int dy =
                        static_cast<int>(y) -
                        static_cast<int>(slot.pending.center_local_y);
                    const auto distance_sq =
                        static_cast<std::uint32_t>(dx * dx + dy * dy);

                    if (!result.has_hit ||
                        distance_sq < best_distance_sq ||
                        (distance_sq == best_distance_sq &&
                         depth < result.depth)) {
                        result.has_hit = true;
                        result.point_id = point_id;
                        result.depth = depth;
                        best_distance_sq = distance_sq;
                    }
                }
            }

            vkUnmapMemory(context_->device(), slot.depth_buffer.memory());
            vkUnmapMemory(context_->device(), slot.id_buffer.memory());
            slot.pending = {};
            results.push_back(result);
        }

        return results;
    }

private:
    // Buffers are sized for the largest hover aperture we allow.
    // Each request can choose a smaller point-size-aware radius.
    static constexpr int kPickRadiusPx =
        static_cast<int>(kMaxGpuPickRadiusPx);
    // Must hold (2 * kPickRadiusPx + 1)^2 = 121 pixels; 144 leaves
    // headroom for any future 12x12 bump without re-touching this.
    static constexpr std::uint32_t kMaxPickPixels = 144;

    struct PendingState {
        GpuPickRequest request{};
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t center_local_x = 0;
        std::uint32_t center_local_y = 0;
        bool pending = false;
    };

    struct SlotState {
        gs3d::render::VulkanBuffer id_buffer{};
        gs3d::render::VulkanBuffer depth_buffer{};
        PendingState pending{};
    };

    const gs3d::render::VulkanContext* context_ = nullptr;
    std::vector<std::vector<SlotState>> slots_{};
};

class PickDebugFrameDumper {
public:
    PickDebugFrameDumper(
        const gs3d::render::VulkanContext& context,
        std::uint32_t frames_in_flight
    )
        : context_(&context)
    {
        slots_.resize(frames_in_flight);
    }

    [[nodiscard]]
    bool record_request(
        VkCommandBuffer command_buffer,
        std::uint32_t frame_slot,
        const gs3d::render::OffscreenFramebuffer& framebuffer,
        const PickDebugDumpMetadata& metadata
    ) {
        if (context_ == nullptr || frame_slot >= slots_.size()) {
            return false;
        }

        const auto extent = framebuffer.extent();
        if (extent.width == 0 || extent.height == 0) {
            return false;
        }

        auto& slot = slots_[frame_slot];
        const VkDeviceSize color_bytes =
            static_cast<VkDeviceSize>(extent.width) *
            extent.height *
            4u;
        const VkDeviceSize id_bytes =
            static_cast<VkDeviceSize>(extent.width) *
            extent.height *
            sizeof(std::uint32_t);
        ensure_buffer_size(slot.color_buffer, color_bytes);
        ensure_buffer_size(slot.id_buffer, id_bytes);

        transition_color_image_for_dump(
            command_buffer,
            framebuffer.color_image(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT
        );

        VkBufferImageCopy copy_region{};
        copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_region.imageSubresource.mipLevel = 0;
        copy_region.imageSubresource.baseArrayLayer = 0;
        copy_region.imageSubresource.layerCount = 1;
        copy_region.imageOffset = {0, 0, 0};
        copy_region.imageExtent = {
            extent.width,
            extent.height,
            1
        };

        vkCmdCopyImageToBuffer(
            command_buffer,
            framebuffer.color_image(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            slot.color_buffer.handle(),
            1,
            &copy_region
        );
        vkCmdCopyImageToBuffer(
            command_buffer,
            framebuffer.pick_image(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            slot.id_buffer.handle(),
            1,
            &copy_region
        );

        transition_color_image_for_dump(
            command_buffer,
            framebuffer.color_image(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        );

        slot.pending = true;
        slot.metadata = metadata;
        slot.width = extent.width;
        slot.height = extent.height;
        slot.color_format = framebuffer.color_format();
        return true;
    }

    [[nodiscard]]
    std::optional<PickDebugDumpFrame> collect_ready_frame(
        std::uint32_t frame_slot
    ) {
        if (context_ == nullptr || frame_slot >= slots_.size()) {
            return std::nullopt;
        }

        auto& slot = slots_[frame_slot];
        if (!slot.pending ||
            slot.width == 0 ||
            slot.height == 0 ||
            !slot.color_buffer.valid() ||
            !slot.id_buffer.valid()) {
            return std::nullopt;
        }

        void* color_memory = nullptr;
        void* id_memory = nullptr;
        const VkDeviceSize color_bytes =
            static_cast<VkDeviceSize>(slot.width) *
            slot.height *
            4u;
        const VkDeviceSize id_bytes =
            static_cast<VkDeviceSize>(slot.width) *
            slot.height *
            sizeof(std::uint32_t);

        vkMapMemory(
            context_->device(),
            slot.color_buffer.memory(),
            0,
            color_bytes,
            0,
            &color_memory
        );
        vkMapMemory(
            context_->device(),
            slot.id_buffer.memory(),
            0,
            id_bytes,
            0,
            &id_memory
        );

        PickDebugDumpFrame dump;
        dump.metadata = slot.metadata;
        dump.color_format = slot.color_format;
        dump.width = slot.width;
        dump.height = slot.height;
        dump.color_pixels.resize(static_cast<std::size_t>(color_bytes));
        dump.pick_ids.resize(
            static_cast<std::size_t>(slot.width) * slot.height
        );
        std::memcpy(
            dump.color_pixels.data(),
            color_memory,
            static_cast<std::size_t>(color_bytes)
        );
        std::memcpy(
            dump.pick_ids.data(),
            id_memory,
            static_cast<std::size_t>(id_bytes)
        );

        vkUnmapMemory(context_->device(), slot.id_buffer.memory());
        vkUnmapMemory(context_->device(), slot.color_buffer.memory());
        slot.pending = false;
        return dump;
    }

private:
    struct SlotState {
        gs3d::render::VulkanBuffer color_buffer{};
        gs3d::render::VulkanBuffer id_buffer{};
        PickDebugDumpMetadata metadata{};
        VkFormat color_format = VK_FORMAT_UNDEFINED;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool pending = false;
    };

    void ensure_buffer_size(
        gs3d::render::VulkanBuffer& buffer,
        VkDeviceSize size
    ) {
        if (buffer.valid() && buffer.size() == size) {
            return;
        }
        buffer.destroy();
        buffer.create(
            *context_,
            size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
    }

    static void transition_color_image_for_dump(
        VkCommandBuffer command_buffer,
        VkImage image,
        VkImageLayout old_layout,
        VkImageLayout new_layout,
        VkAccessFlags src_access_mask,
        VkAccessFlags dst_access_mask,
        VkPipelineStageFlags src_stage_mask,
        VkPipelineStageFlags dst_stage_mask
    ) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = src_access_mask;
        barrier.dstAccessMask = dst_access_mask;
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            command_buffer,
            src_stage_mask,
            dst_stage_mask,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );
    }

    const gs3d::render::VulkanContext* context_ = nullptr;
    std::vector<SlotState> slots_{};
};

} // namespace gs3d::app
