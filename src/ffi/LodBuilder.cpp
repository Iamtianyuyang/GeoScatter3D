#include "ffi/LodBuilder.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace gs3d::ffi {

namespace {

constexpr float kMinVoxelSize = 1e-6f;

struct Bounds {
    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float min_z = std::numeric_limits<float>::max();
    bool valid = false;
};

Bounds compute_bounds(const std::vector<gs3d::data::Gs3dPoint>& points) {
    Bounds bounds;
    for (const auto& point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            !std::isfinite(point.z)) {
            continue;
        }
        bounds.min_x = std::min(bounds.min_x, point.x);
        bounds.min_y = std::min(bounds.min_y, point.y);
        bounds.min_z = std::min(bounds.min_z, point.z);
        bounds.valid = true;
    }
    return bounds;
}

/*
 * 体素键：以 bbox 最小角为原点，按体素边长量化到 21 位/轴并打包。
 * 21 位 = 每轴 209 万格，远超单机数据规模；越界时夹取，避免键碰撞引发的
 * 不均匀降采样（宁可合并到边界格，也不产生错误的空间分布）。
 */
inline std::uint64_t voxel_key(
    float x, float y, float z,
    float cell,
    const Bounds& bounds
) {
    constexpr std::uint64_t kAxisMask = (1ull << 21) - 1;
    // kAxisMask 会被 std::min 按引用使用（odr-use），lambda 必须显式捕获——
    // MSVC 容忍缺捕获，clang 会直接报错（Android 构建曾因此失败）。
    const auto axis_index = [cell, &bounds, kAxisMask](float value, float origin) {
        const double index = std::floor(
            static_cast<double>(value - origin) / static_cast<double>(cell)
        );
        if (!(index > 0.0)) {
            return std::uint64_t{0};
        }
        const auto clamped = static_cast<std::uint64_t>(index);
        return std::min<std::uint64_t>(clamped, kAxisMask);
    };
    const std::uint64_t ix = axis_index(x, bounds.min_x);
    const std::uint64_t iy = axis_index(y, bounds.min_y);
    const std::uint64_t iz = axis_index(z, bounds.min_z);
    return (ix << 42) | (iy << 21) | iz;
}

}  // namespace

float LodBuilder::estimate_spacing(
    const std::vector<gs3d::data::Gs3dPoint>& points,
    std::size_t sample_size
) {
    if (points.size() < 2 || sample_size < 2) {
        return 0.0f;
    }

    /*
     * 采样必须保持「空间局部性」，不能用等距步长扫描：
     * 规则栅格按行主序存储时，每隔 k 个取一个点会得到一个人造稀疏格点
     * （相邻采样点在空间上相距 k 倍间距），估出的间距会偏大几个数量级，
     * 直接导致降采样过度（真机实测 100 万点被降到 725 点）。
     * 因此改为取若干段连续块：栅格数据块内相邻点即真实邻点；
     * 乱序数据下连续块仍代表局部密度量级。
     */
    constexpr std::size_t kBlocks = 8;
    const std::size_t per_block = std::max<std::size_t>(2, sample_size / kBlocks);
    const std::size_t block_stride =
        std::max<std::size_t>(per_block, points.size() / kBlocks);

    std::vector<const gs3d::data::Gs3dPoint*> sample;
    sample.reserve(per_block * kBlocks);
    for (std::size_t block = 0; block < kBlocks; ++block) {
        const std::size_t begin = std::min(
            points.size() - 1,
            block * block_stride
        );
        const std::size_t end = std::min(points.size(), begin + per_block);
        for (std::size_t i = begin; i < end; ++i) {
            sample.push_back(&points[i]);
        }
    }
    if (sample.size() < 2) {
        return 0.0f;
    }

    double total = 0.0;
    std::size_t counted = 0;
    for (const auto* a : sample) {
        double best = std::numeric_limits<double>::max();
        for (const auto* b : sample) {
            if (a == b) {
                continue;
            }
            const double dx = static_cast<double>(a->x) - b->x;
            const double dy = static_cast<double>(a->y) - b->y;
            const double dz = static_cast<double>(a->z) - b->z;
            const double squared = dx * dx + dy * dy + dz * dz;
            best = std::min(best, squared);
        }
        if (best < std::numeric_limits<double>::max() && best > 0.0) {
            total += std::sqrt(best);
            ++counted;
        }
    }
    if (counted == 0) {
        return 0.0f;
    }
    return static_cast<float>(total / static_cast<double>(counted));
}

std::vector<gs3d::data::Gs3dPoint> LodBuilder::decimate_voxel(
    const std::vector<gs3d::data::Gs3dPoint>& points,
    float voxel_size,
    std::uint64_t* occupied_cells
) {
    if (occupied_cells != nullptr) {
        *occupied_cells = 0;
    }
    if (points.empty() || !(voxel_size > kMinVoxelSize)) {
        return points;
    }

    const Bounds bounds = compute_bounds(points);
    if (!bounds.valid) {
        return points;
    }

    /*
     * 体素内取质心，而不是「保留第一个点」。
     *
     * 保留首点会把点吸附到体素栅格的单元角落上：在斜面/曲面上，代表点会聚成
     * 规整的栅格点阵，屏幕上就是一块块的方格（真机 LOD 模式可见的接缝感）。
     * 质心化后点落回单元中心，各层都是同一套栅格、只有疏密差异，表面恢复平滑。
     * 其余属性（颜色等）沿用该单元遇到的第一个点。
     *
     * 用 index_of + 并行累加数组保持输出顺序与输入顺序一致（可复现），
     * 避免直接遍历 unordered_map 带来的不确定顺序。
     */
    std::unordered_map<std::uint64_t, std::size_t> index_of;
    index_of.reserve(points.size() / 2 + 16);

    struct Accumulator {
        double sx = 0.0;
        double sy = 0.0;
        double sz = 0.0;
        std::uint64_t count = 0;
    };

    std::vector<gs3d::data::Gs3dPoint> output;
    std::vector<Accumulator> accumulators;
    output.reserve(points.size() / 2 + 16);
    accumulators.reserve(points.size() / 2 + 16);

    for (const auto& point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            !std::isfinite(point.z)) {
            continue;
        }
        const std::uint64_t key =
            voxel_key(point.x, point.y, point.z, voxel_size, bounds);
        const auto [it, inserted] =
            index_of.try_emplace(key, output.size());
        if (inserted) {
            output.push_back(point);
            accumulators.push_back(Accumulator{});
        }
        Accumulator& accumulator = accumulators[it->second];
        accumulator.sx += static_cast<double>(point.x);
        accumulator.sy += static_cast<double>(point.y);
        accumulator.sz += static_cast<double>(point.z);
        ++accumulator.count;
    }

    for (std::size_t index = 0; index < output.size(); ++index) {
        const Accumulator& accumulator = accumulators[index];
        if (accumulator.count <= 1) {
            continue;
        }
        const double inverse = 1.0 / static_cast<double>(accumulator.count);
        output[index].x = static_cast<float>(accumulator.sx * inverse);
        output[index].y = static_cast<float>(accumulator.sy * inverse);
        output[index].z = static_cast<float>(accumulator.sz * inverse);
    }

    if (occupied_cells != nullptr) {
        *occupied_cells = output.size();
    }
    return output;
}

LodChain LodBuilder::build(
    const std::vector<gs3d::data::Gs3dPoint>& points,
    const LodBuildConfig& config
) {
    const auto started = std::chrono::steady_clock::now();
    LodChain chain;
    if (points.size() < 2) {
        return chain;
    }

    chain.estimated_spacing = estimate_spacing(points, config.spacing_sample);
    if (!(chain.estimated_spacing > kMinVoxelSize)) {
        // 点重合或退化：不做降采样（画面上本来就只有一个点）。
        return chain;
    }
    const float spacing = chain.estimated_spacing;
    float effective_spacing = spacing;

    const std::vector<gs3d::data::Gs3dPoint>* source = &points;
    for (std::size_t level = 0; level < config.max_levels; ++level) {
        if (source->size() <= config.min_points) {
            break;
        }
        // 体素边长按 2 的幂增长：level1=2×间距, level2=4×间距 …
        float voxel = effective_spacing * static_cast<float>(1u << (level + 1));
        std::vector<gs3d::data::Gs3dPoint> decimated;
        for (int attempt = 0; attempt < 4; ++attempt) {
            decimated = decimate_voxel(*source, voxel);
            /*
             * 自纠正：若一次降到远低于预算的量级（典型原因：间距估计偏大，
             * 或数据分布不均匀），逐步减半体素边长重试，而不是直接把
             * 可用层跳过——否则链会断在“全量”与“几乎没有点”之间。
             */
            const std::uint64_t floor_points =
                std::max<std::uint64_t>(config.min_points / 4, 1000);
            if (decimated.size() >= floor_points) {
                break;
            }
            if (voxel <= effective_spacing) {
                break;
            }
            voxel *= 0.5f;
            // 用纠正后的体素反推更可信的间距，保持后续层等比。
            effective_spacing = voxel / static_cast<float>(1u << (level + 1));
        }
        if (decimated.empty()) {
            break;
        }
        if (static_cast<double>(decimated.size()) >
            static_cast<double>(source->size()) * config.min_reduction) {
            // 降采样失效（数据在该分辨率下已无冗余）：停止建链。
            break;
        }

        LodLevel lod_level;
        lod_level.voxel_size = voxel;
        lod_level.source_point_count =
            static_cast<std::uint64_t>(source->size());
        lod_level.points = std::move(decimated);
        chain.levels.push_back(std::move(lod_level));
        // 必须在 push_back 之后取元素地址：先前写法指向被 move 的局部对象，
        // 下一级会把空集当成输入，链在第一级就断掉（真机表现为只有 2 层）。
        source = &chain.levels.back().points;
    }

    chain.build_ms = std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - started
                     ).count();
    return chain;
}

}  // namespace gs3d::ffi
