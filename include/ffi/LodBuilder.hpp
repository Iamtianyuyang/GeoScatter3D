#pragma once

#include "data/Gs3dFormat.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gs3d::ffi {

/*
 * 大规模点云的 LOD 链构建（千万级数据的第一步）。
 *
 * 目标：让每帧绘制量与会话数据量解耦——10M 源点不可能在平板 GPU 上逐点绘制，
 * 因此按几何分辨率逐级降采样，运行时按「点预算」选择层级。
 *
 * 约定：
 * - level 0 恒为原始数据，由调用方持有（kernel 只保存视图），本类只产出更粗的层；
 * - 每层的体素边长 = 估计点间距 × 2^level，保证规则栅格与散乱点云都能均匀降采样；
 * - 降采样保留每格首个点（确定性、无随机、可重现）；
 * - 全部为纯 CPU 逻辑，不依赖 Vulkan，可独立单测。
 */
struct LodLevel {
    std::vector<gs3d::data::Gs3dPoint> points;
    float voxel_size = 0.0f;
    /// 本层输入点数（level 0 的输入即为其 source_point_count）。
    std::uint64_t source_point_count = 0;
};

struct LodBuildConfig {
    /// 最粗层降到该点数以下即停止（默认 15 万，约等于平板一屏的可绘预算）。
    std::uint64_t min_points = 150'000;
    /// 层数上限（含最细的粗化层）。
    std::size_t max_levels = 6;
    /// 估计点间距时的采样点数（O(sample²) 距离计算）。
    std::size_t spacing_sample = 1024;
    /// 某层相对输入的点数保留比高于该值即认为降采样失效，停止建链。
    double min_reduction = 0.6;
};

struct LodChain {
    /// 由细到粗：levels[0] ≈ 源点 / 4，levels.back() 为最粗层。
    std::vector<LodLevel> levels;
    float estimated_spacing = 0.0f;
    /// 建链耗时（诊断用）。
    double build_ms = 0.0;

    [[nodiscard]] bool empty() const noexcept { return levels.empty(); }
};

class LodBuilder {
public:
    /*
     * 估计平均最近邻点间距：对等距抽样得到的 sample 个点两两求最近距离并取均值。
     * 点太少或退化（重合）时返回 0，调用方应据此跳过 LOD 建链。
     */
    [[nodiscard]]
    static float estimate_spacing(
        const std::vector<gs3d::data::Gs3dPoint>& points,
        std::size_t sample_size
    );

    /*
     * 体素降采样：体素内保留首个点。occupied_cells 非空时回填占用格数。
     * 返回点数 ≤ 输入点数；体素边长非正时原样返回。
     */
    [[nodiscard]]
    static std::vector<gs3d::data::Gs3dPoint> decimate_voxel(
        const std::vector<gs3d::data::Gs3dPoint>& points,
        float voxel_size,
        std::uint64_t* occupied_cells = nullptr
    );

    /// 构建完整 LOD 链（不含 level 0）。
    [[nodiscard]]
    static LodChain build(
        const std::vector<gs3d::data::Gs3dPoint>& points,
        const LodBuildConfig& config = {}
    );
};

}  // namespace gs3d::ffi
