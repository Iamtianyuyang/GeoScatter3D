#pragma once

#include "data/format/Gs3dFormat.hpp"

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
 * - **各层共用同一个栅格锚点**（数据集全局 bbox 最小角，见 [VoxelGridOrigin]），
 *   因此粗层单元恒为细层单元的 2^n 倍并集，层间点阵不会错位；
 * - 降采样输出每格代表点的质心（确定性、无随机、可重现）；
 *   取首点会把代表点吸附到体素单元角落，在斜面/曲面上聚成规整栅格点阵（真机 LOD
 *   模式可见的方块接缝），故改为质心；颜色等其余属性沿用该格首个点；
 * - 体素质心之后再对代表点做一次**最小距离（蓝噪声）抽稀**（见
 *   [LodBuilder::dedupe_min_distance] 与 [LodBuildConfig::min_distance_factor]）：
 *   2.5D 曲面被各向同性体素的 z 边界切碎时，同一列会吐出多个靠得很近的代表点，
 *   屏幕上就是 1-2-1-2 的疏密交替（真机基线：紧邻点占比 23%、最近邻 CV 0.236）；
 * - 全部为纯 CPU 逻辑，不依赖 Vulkan，可独立单测。
 */

/*
 * 体素栅格锚点（= 数据集全局 bbox 的最小角）。
 *
 * 降采样后每层的输入范围都会收缩（代表点落在单元内部），如果每层都用自己
 * 的 bbox 去锚定栅格，原点就会逐层漂移：实测抖动散乱数据上单层漂移可达半个
 * 体素（x/y/z 均如此），层与层之间因此错位、疏密不匀。固定锚点后层内单元边界
 * 恒落在 origin + k × voxel 上，配合 voxel_{n+1} = 2 × voxel_n 即得层间对齐。
 */
struct VoxelGridOrigin {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
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
    /*
     * 代表点最小距离抽稀半径（× 该层体素边长）；0 = 关闭抽稀。
     *
     * 为什么必须 < 1：体素栅格的理想最近邻距离 ≈ 体素边长，半径取 1.0 会把
     * 规则栅格的输出直接砍掉一半（实测 out 72569 → 34617）。
     *
     * 取值依据（真机 bench 数据 / Python 移植扫描，见 tmp/lod-ab/p12_*.py）：
     *   半径   紧邻占比(23.3% 基线)  保留点数
     *   0.5×   19.3%               93.7%
     *   0.6×    8.3%               87.3%
     *   0.7×    1.7%               84.8%   ← 取此值
     *   0.8×    0.0%               79.8%
     *   1.0×    0.0%               47.7%（过度抽稀，等于砍半）
     * 规则体栅格（60³）与随机体数据上 0.7× 一点不丢（100%），因此不会伤害真 3D 数据。
     */
    float min_distance_factor = 0.7f;
    /*
     * 超过该点数的层不做最小距离抽稀（0 = 不限制）。
     *
     * 依据：kernel 选层时（`choose_lod_index`）不会画超过 `lod_point_cap` 的层，
     * 对它们抽稀只白白花建链时间与显存（10M 源数据的 chain[0] 有 247~293 万
     * 代表点，占整个抽稀成本的 ~80%，而它永远不会被绘制）。
     * kernel 传入的就是自己的 `Config::lod_point_cap`（0 = 不限 → 全部抽稀）。
     */
    std::uint64_t min_distance_max_points = 0;
};

struct LodChain {
    /// 由细到粗：levels[0] ≈ 源点 / 4，levels.back() 为最粗层。
    std::vector<LodLevel> levels;
    float estimated_spacing = 0.0f;
    /// 建链时各层共用的栅格锚点（数据集 bbox 最小角）；空链时为全 0。
    VoxelGridOrigin grid_origin{};
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
     * 体素降采样：每格输出一个代表点，位置取该格有效点的质心，
     * 其余属性（value 等）沿用该格首个点。occupied_cells 非空时回填输出点数。
     * 返回点数 ≤ 输入点数；体素边长非正时原样返回。
     *
     * 栅格锚点取「本次输入」的 bbox 最小角——只适用于单层/独立调用；
     * 建 LOD 链必须改用下面的重载并给所有层传同一个锚点。
     */
    [[nodiscard]]
    static std::vector<gs3d::data::Gs3dPoint> decimate_voxel(
        const std::vector<gs3d::data::Gs3dPoint>& points,
        float voxel_size,
        std::uint64_t* occupied_cells = nullptr
    );

    /// 同上，但栅格锚点由调用方给出（LOD 链各层必须传同一个 origin）。
    [[nodiscard]]
    static std::vector<gs3d::data::Gs3dPoint> decimate_voxel(
        const std::vector<gs3d::data::Gs3dPoint>& points,
        float voxel_size,
        std::uint64_t* occupied_cells,
        const VoxelGridOrigin& origin
    );

    /// 构建完整 LOD 链（不含 level 0）。
    [[nodiscard]]
    static LodChain build(
        const std::vector<gs3d::data::Gs3dPoint>& points,
        const LodBuildConfig& config = {}
    );

    /*
     * 最小距离抽稀（确定性贪心，按输入顺序）：保留任意两个保留点距离 ≥ min_distance，
     * 即「蓝噪声」子采样。输出顺序 = 输入顺序的子序列，因此可重现。
     *
     * 用在体素质心之后：各向同性 3D 体素降采样 2.5D 曲面时，同一 x/y 单元会被 z
     * 边界切成多个 cell（真机 bench 数据实测 1.22×），于是同一列吐出多个彼此
     * 很近的代表点 —— 实测紧邻点占比 23%、最近邻 CV 0.236（屏幕上就是疏密交替
     * 的等距方块）。质心化只能把代表点拉回单元中心，消不掉这种「同列重复」；
     * 而「体素边长对齐数据格距整数倍」在本机复现中反而更差（切得更碎，
     * 72569 → 76064 点，紧邻占比 26.7%）。真正有效且通用的是最小距离抽稀。
     * 成本：只在代表点上做（30 万量级），比在源点上做便宜两个数量级。
     * 非有限点不参与输出（与 [decimate_voxel] 一致）；min_distance 非正时原样返回。
     */
    [[nodiscard]]
    static std::vector<gs3d::data::Gs3dPoint> dedupe_min_distance(
        const std::vector<gs3d::data::Gs3dPoint>& points,
        float min_distance
    );
};

}  // namespace gs3d::ffi
