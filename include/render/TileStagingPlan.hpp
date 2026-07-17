#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace gs3d::render {

/*
 * 共享 staging 批量上传的「打包计划」——纯函数，无 Vulkan 依赖，可单测。
 *
 * 背景：全量预加载会把上万块瓦片搬进显存。旧实现为每块瓦片单独分配一个
 * host-visible staging buffer + device-local vertex buffer，30k 瓦片 ≈ 6 万
 * 次 Vulkan 分配，且 staging 从不释放（约 1GB 主机内存常驻）。
 *
 * 新实现让一个可复用的 staging buffer 承载一整批瓦片：本规划器按输入顺序
 * 贪心地把瓦片打包进「本帧上传预算」，给出每块瓦片在共享 staging 中的偏移，
 * 上传阶段一次拷贝、一次提交即可。语义与旧的逐块 FrameUploadBudget 一致：
 * 按顺序尝试，装不下当前预算的瓦片跳过（后面更小的仍可能装下）。
 */

struct TileStagingPlanEntry {
    // 候选数组（传入的 candidate_bytes）中的下标。
    std::size_t candidate_index = 0;
    // 该瓦片数据在共享 staging buffer 中的字节偏移。
    std::uint64_t offset = 0;
    // 该瓦片的字节数（= 偏移步进）。
    std::uint64_t bytes = 0;
};

struct TileStagingPlan {
    std::vector<TileStagingPlanEntry> entries;
    // 本批需要的 staging 容量 = 选中瓦片字节数之和（也是最后一个 offset+bytes）。
    std::uint64_t total_bytes = 0;
};

/*
 * candidate_bytes: 待上传瓦片（已排除已驻留者）的字节数，按期望上传顺序。
 * max_upload_bytes: 本帧上传字节预算；默认无上限（一次全传）。
 *
 * 返回选中的瓦片及其在共享 staging 中的紧凑偏移。跳过 0 字节的候选
 * （空瓦片不产生上传）。
 */
[[nodiscard]]
inline TileStagingPlan plan_tile_staging_batch(
    const std::vector<std::uint64_t>& candidate_bytes,
    std::uint64_t max_upload_bytes =
        std::numeric_limits<std::uint64_t>::max()
) {
    constexpr std::uint64_t kUnbounded =
        std::numeric_limits<std::uint64_t>::max();

    TileStagingPlan plan;
    plan.entries.reserve(candidate_bytes.size());

    std::uint64_t reserved = 0;
    for (std::size_t i = 0; i < candidate_bytes.size(); ++i) {
        const std::uint64_t bytes = candidate_bytes[i];
        if (bytes == 0) {
            continue;
        }

        // 单块就超过总预算：与旧 FrameUploadBudget 一致，永远装不下，跳过。
        if (max_upload_bytes != kUnbounded && bytes > max_upload_bytes) {
            continue;
        }
        // 剩余预算不足以再放下这一块：跳过（后面更小的块仍可能装下）。
        if (max_upload_bytes != kUnbounded &&
            bytes > max_upload_bytes - reserved) {
            continue;
        }

        plan.entries.push_back(TileStagingPlanEntry{
            i,
            reserved,
            bytes
        });
        reserved += bytes;
    }

    plan.total_bytes = reserved;
    return plan;
}

} // namespace gs3d::render
