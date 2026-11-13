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
 * 体素键：以给定锚点为原点，按体素边长量化到 21 位/轴并打包。
 * 21 位 = 每轴 209 万格，远超单机数据规模；越界时夹取，避免键碰撞引发的
 * 不均匀降采样（宁可合并到边界格，也不产生错误的空间分布）。
 *
 * 锚点由调用方固定传入（而不是每次重算 bbox），是层间点阵对齐的前提。
 */
inline std::uint64_t voxel_key(
    float x, float y, float z,
    float cell,
    const VoxelGridOrigin& origin
) {
    constexpr std::uint64_t kAxisMask = (1ull << 21) - 1;
    // kAxisMask 会被 std::min 按引用使用（odr-use），lambda 必须显式捕获——
    // MSVC 容忍缺捕获，clang 会直接报错（Android 构建曾因此失败）。
    const auto axis_index = [cell, kAxisMask](float value, float anchor) {
        const double index = std::floor(
            static_cast<double>(value - anchor) / static_cast<double>(cell)
        );
        if (!(index > 0.0)) {
            return std::uint64_t{0};
        }
        const auto clamped = static_cast<std::uint64_t>(index);
        return std::min<std::uint64_t>(clamped, kAxisMask);
    };
    const std::uint64_t ix = axis_index(x, origin.x);
    const std::uint64_t iy = axis_index(y, origin.y);
    const std::uint64_t iz = axis_index(z, origin.z);
    return (ix << 42) | (iy << 21) | iz;
}

/*
 * 64 位混洗（murmur3 尾混）：把键的每一位都摊到低位上。
 *
 * 必要性：抽稀空间哈希的槽位取键的低位，而格键是「21 位/轴」打包的（低位全是
 * z 下标）。曲面数据的 z 高度分片极少，直接用打包键取低位会让几十万个点挤在
 * 极少数槽位里，线性探测退化成 O(n²)——宿主实测 25 万点要 17 s、100 万点直接
 * 跑不完。
 *
 * 槽位用「乘法散列取高位」（key × 黄金比常数，取乘积高位）而不是完整混洗：
 * 每个点要做 27 次槽位计算，一次乘法就能把三个轴的信息都摊到高位，
 * 而 murmur 尾混要 6 次运算；键本身仍原样存在表里做精确比较（无碰撞误判）。
 */
constexpr std::uint64_t kGoldenRatio64 = 0x9E3779B97F4A7C15ull;

inline std::size_t slot_of(std::uint64_t key, int slot_bits) {
    return static_cast<std::size_t>(key * kGoldenRatio64) >>
           (64 - slot_bits);
}

}  // namespace

std::vector<gs3d::data::Gs3dPoint> LodBuilder::dedupe_min_distance(
    const std::vector<gs3d::data::Gs3dPoint>& points,
    float min_distance
) {
    if (points.size() < 2 || !(min_distance > kMinVoxelSize)) {
        return points;
    }

    const double radius = static_cast<double>(min_distance);
    const double radius_sq = radius * radius;

    /*
     * 扁平空间哈希（开放寻址 + 每点同格链）。
     *
     * 不能用 unordered_map<key, vector<uint32_t>>：真机 10M 数据上这一步要
     * 5.0 s（2.47M 代表点 × 27 格查找，每次查找都是节点分配 + 指针追逐，
     * LOD 建链 1.42 s → 6.41 s）。扁平表把查找降成几次数组访问；只保留
     * 同格链（next_of）而不开 vector，内存也从每个格一份分配变成两块大数组。
     *
     * 表大小取 2 的幂且 ≥ 点数（负载因子 ≲ 0.6，线性探测均摊 ~1.5 次）。
     */
    std::size_t table_size = 1;
    int slot_bits = 0;
    while (table_size < points.size()) {
        table_size <<= 1;
        ++slot_bits;
    }
    const std::size_t table_mask = table_size - 1;
    // 坐标 → 格下标用乘法（每点 3 次）而不是除法：2.5M 代表点上差别可观。
    const double inverse_radius = 1.0 / radius;
    constexpr std::uint32_t kNoPoint = 0xFFFFFFFFu;
    // 格键：21 位/轴 + 偏置（允许负下标）。坐标超出 ±100 万格时按位夹取到边界
    // （与 voxel_key 同一取舍：宁可合并极端边界格，也不额外分配一套键类型）。
    const auto cell_key = [](std::int64_t ix, std::int64_t iy, std::int64_t iz) {
        constexpr std::int64_t kBias = 1 << 20;
        constexpr std::uint64_t kMask = (1ull << 21) - 1;
        return ((static_cast<std::uint64_t>(ix + kBias) & kMask) << 42) |
               ((static_cast<std::uint64_t>(iy + kBias) & kMask) << 21) |
               (static_cast<std::uint64_t>(iz + kBias) & kMask);
    };
    const auto cell_of = [radius, inverse_radius](float value) {
        static_cast<void>(radius);
        return static_cast<std::int64_t>(
            std::floor(static_cast<double>(value) * inverse_radius)
        );
    };

    std::vector<std::uint64_t> slot_key(table_size, 0);
    std::vector<std::uint32_t> slot_head(table_size, kNoPoint);
    std::vector<std::uint32_t> next_of;
    next_of.reserve(points.size());
    std::vector<gs3d::data::Gs3dPoint> kept;
    kept.reserve(points.size());

    for (const auto& point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            !std::isfinite(point.z)) {
            continue;
        }
        const std::int64_t ix = cell_of(point.x);
        const std::int64_t iy = cell_of(point.y);
        const std::int64_t iz = cell_of(point.z);

        bool too_close = false;
        for (std::int64_t dx = -1; dx <= 1 && !too_close; ++dx) {
            for (std::int64_t dy = -1; dy <= 1 && !too_close; ++dy) {
                for (std::int64_t dz = -1; dz <= 1 && !too_close; ++dz) {
                    const std::uint64_t key =
                        cell_key(ix + dx, iy + dy, iz + dz);
                    std::size_t slot = slot_of(key, slot_bits);
                    while (slot_head[slot] != kNoPoint) {
                        if (slot_key[slot] == key) {
                            for (std::uint32_t index = slot_head[slot];
                                 index != kNoPoint;
                                 index = next_of[index]) {
                                const gs3d::data::Gs3dPoint& other =
                                    kept[index];
                                const double ddx =
                                    static_cast<double>(point.x) - other.x;
                                const double ddy =
                                    static_cast<double>(point.y) - other.y;
                                const double ddz =
                                    static_cast<double>(point.z) - other.z;
                                if (ddx * ddx + ddy * ddy + ddz * ddz <
                                    radius_sq) {
                                    too_close = true;
                                    break;
                                }
                            }
                            break;  // 该格已处理（未命中则继续探测）
                        }
                        slot = (slot + 1) & table_mask;
                    }
                }
            }
        }
        if (too_close) {
            continue;
        }
        const std::uint64_t key = cell_key(ix, iy, iz);
        std::size_t slot = slot_of(key, slot_bits);
        while (slot_head[slot] != kNoPoint && slot_key[slot] != key) {
            slot = (slot + 1) & table_mask;
        }
        slot_key[slot] = key;
        // 单层代表点规模在百万量级（10M 源数据 4:1 后 ≈ 250 万），uint32 足够。
        next_of.push_back(slot_head[slot]);
        slot_head[slot] = static_cast<std::uint32_t>(kept.size());
        kept.push_back(point);
    }
    return kept;
}

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
    /*
     * 连续块的估计只在「输入顺序≈空间相邻」时成立。真实 50M 数据按列主序导出时，
     * 相邻两点在 x 上隔着整行（实测估出 537,970，而真实点距只有个位数），
     * 建链会把整份数据压成一层、屏幕上只剩几万点。这里用 bbox 最大面的密度
     * 给一个与输入顺序无关的上界：面密度 sqrt(area/n) 对铺满一个面的点云
     * 接近真实点距，且不会小于真实值的一半（体积分布时也只是偏小，
     * 结果是链多一层，不会把层压塌）。
     */
    double min_x = sample.front()->x, max_x = min_x;
    double min_y = sample.front()->y, max_y = min_y;
    double min_z = sample.front()->z, max_z = min_z;
    for (const auto& point : points) {
        min_x = std::min(min_x, static_cast<double>(point.x));
        max_x = std::max(max_x, static_cast<double>(point.x));
        min_y = std::min(min_y, static_cast<double>(point.y));
        max_y = std::max(max_y, static_cast<double>(point.y));
        min_z = std::min(min_z, static_cast<double>(point.z));
        max_z = std::max(max_z, static_cast<double>(point.z));
    }
    const double dx = max_x - min_x;
    const double dy = max_y - min_y;
    const double dz = max_z - min_z;
    const double face = std::max({dx * dy, dx * dz, dy * dz});
    const double density = face > 0.0
        ? std::sqrt(face / static_cast<double>(points.size()))
        : 0.0;
    const double estimate = total / static_cast<double>(counted);
    if (density > 0.0) {
        return static_cast<float>(std::min(estimate, density * 2.0));
    }
    return static_cast<float>(estimate);
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
    const VoxelGridOrigin origin{bounds.min_x, bounds.min_y, bounds.min_z};
    return decimate_voxel(points, voxel_size, occupied_cells, origin);
}

std::vector<gs3d::data::Gs3dPoint> LodBuilder::decimate_voxel(
    const std::vector<gs3d::data::Gs3dPoint>& points,
    float voxel_size,
    std::uint64_t* occupied_cells,
    const VoxelGridOrigin& origin
) {
    if (occupied_cells != nullptr) {
        *occupied_cells = 0;
    }
    if (points.empty() || !(voxel_size > kMinVoxelSize)) {
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
     * 单元划分只用调用方给的 origin（不重算 bbox）：同一条 LOD 链的所有层共用
     * 同一锚点，层内单元边界落在 origin + k × voxel 上，层间才能对齐。
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
            voxel_key(point.x, point.y, point.z, voxel_size, origin);
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

    if (output.empty()) {
        // 所有点都非有限：不做降采样（与单层入口一致），而不是产出一个空层。
        return points;
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

    // 栅格锚点全局唯一：取首层（即源数据）的 bbox 最小角，所有层共用。
    // 若每层各自重算 bbox，降采样后的范围收缩会让原点逐层漂移，层间错位。
    const Bounds global_bounds = compute_bounds(points);
    chain.grid_origin = VoxelGridOrigin{
        global_bounds.min_x, global_bounds.min_y, global_bounds.min_z
    };

    const std::vector<gs3d::data::Gs3dPoint>* source = &points;
    for (std::size_t level = 0; level < config.max_levels; ++level) {
        if (source->size() <= config.min_points) {
            break;
        }
        // 体素边长按 2 的幂增长：level1=2×间距, level2=4×间距 …
        // 自纠正分支会用 voxel/=2 反推 effective_spacing，故层间比值恒为 2。
        float voxel = effective_spacing * static_cast<float>(1u << (level + 1));
        std::vector<gs3d::data::Gs3dPoint> decimated;
        for (int attempt = 0; attempt < 4; ++attempt) {
            decimated = decimate_voxel(
                *source, voxel, nullptr, chain.grid_origin
            );
            /*
             * 体素质心之后再抽稀一次（见 min_distance_factor）：
             * 2.5D 曲面被 z 边界切碎的同一列会吐出多个靠得很近的代表点，
             * 屏幕上就是 1-2-1-2 的疏密交替。必须放在下面的预算/自纠正判断
             * 之前 —— floor 检查认的是最终点数。
             *
             * 永远不会被绘制的层跳过抽稀（见 min_distance_max_points）。
             */
            if (config.min_distance_factor > 0.0f &&
                (config.min_distance_max_points == 0 ||
                 static_cast<std::uint64_t>(decimated.size()) <=
                     config.min_distance_max_points)) {
                decimated = dedupe_min_distance(
                    decimated, voxel * config.min_distance_factor
                );
            }
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
