#pragma once

#include "data/Gs3dDataset.hpp"
#include "data/Gs3dFormat.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gs3d::data {

enum class Gs3dLodVoxelMode {
    /*
     * 适合地形、测线、地震散点、elevation/fold 这种 2.5D 数据。
     * 只按 x/y 建 voxel，z 不参与分箱。
     */
    XY,

    /*
     * 适合真正填满三维空间的体数据。
     * 按 x/y/z 三个方向建 voxel。
     */
    XYZ
};

struct Gs3dLodLevel {
    std::string name;

    std::uint32_t level_index = 0;
    std::uint64_t source_point_count = 0;
    std::uint64_t target_point_count = 0;

    float voxel_size = 0.0f;

    Gs3dLodVoxelMode voxel_mode = Gs3dLodVoxelMode::XY;

    double build_seconds = 0.0;

    std::vector<Gs3dPoint> points;

    [[nodiscard]]
    std::uint64_t point_count() const noexcept;

    [[nodiscard]]
    std::uint64_t point_bytes() const noexcept;

    [[nodiscard]]
    bool empty() const noexcept;
};

struct Gs3dLodBuildConfig {
    /*
     * Potree 式自动分层：从最精细层目标点数反推 voxel_size_0，然后每层
     * voxel_size ×= growth_factor，直到点数低于 min_points_per_level 为止。
     * 层数由数据空间分布自然决定，不写死。
     *
     * 默认值针对 ~33M 点 XY 地震工区数据调优：
     *   finest_target_points = 2M → voxel_size_0 ≈ 5.7m
     *   growth_factor = √2 ≈ 1.414 → XY 面积约翻倍，点数约减半
     *   min_points   = 100K → 预期分 ~5 层，最粗 ~125K 点
     */

    /*
     * 最精细层目标点数。
     * 用于反推锚定 voxel_size_0，不要求精确等于。
     */
    std::uint64_t finest_target_points = 2'000'000ull;

    /*
     * voxel_size 倍增系数。
     * XY 数据推荐 1.414 (√2)，每层点数约减半。
     * XYZ 体积数据推荐 2.0。
     */
    float growth_factor = 1.414f;

    /*
     * 最粗层点数下限。低于此值停止分层。
     */
    std::uint64_t min_points_per_level = 100'000ull;

    /*
     * 默认使用 XY。
     *
     * 对你的 x/y/fold/elevation 数据，XY 比 XYZ 更合理。
     */
    Gs3dLodVoxelMode voxel_mode = Gs3dLodVoxelMode::XY;

    /*
     * 是否复制一份全分辨率点云作为 LOD0。
     *
     * 默认 false，避免在已有 Gs3dDataset 的基础上再复制 528MB+ 数据。
     * 全量模式后续继续使用原始 Gs3dDataset / PointCloudGpu。
     */
    bool include_full_resolution_level = false;

    /*
     * voxel_size 修正系数。
     *
     * > 1.0: voxel 更大，生成点更少。
     * < 1.0: voxel 更小，生成点更多。
     */
    float voxel_scale = 1.0f;

    bool verbose = true;
};

class Gs3dLodDataset {
public:
    Gs3dLodDataset() = default;

    [[nodiscard]]
    static Gs3dLodDataset build(
        const Gs3dDataset& dataset,
        const Gs3dLodBuildConfig& config = {}
    );

    [[nodiscard]]
    bool empty() const noexcept;

    [[nodiscard]]
    std::size_t level_count() const noexcept;

    [[nodiscard]]
    const Gs3dLodLevel& level(std::size_t index) const;

    [[nodiscard]]
    const std::vector<Gs3dLodLevel>& levels() const noexcept;

    [[nodiscard]]
    const Gs3dHeader& source_header() const noexcept;

    [[nodiscard]]
    const std::filesystem::path& source_path() const noexcept;

    [[nodiscard]]
    std::string summary() const;

    [[nodiscard]]
    static const char* voxel_mode_name(Gs3dLodVoxelMode mode) noexcept;

    [[nodiscard]]
    static Gs3dLodDataset from_levels(
        const Gs3dHeader& source_header,
        const std::filesystem::path& source_path,
        std::vector<Gs3dLodLevel> levels
    );
    
private:
    Gs3dHeader source_header_{};
    std::filesystem::path source_path_;
    std::vector<Gs3dLodLevel> levels_;

private:
    void add_level(Gs3dLodLevel level);
};

} // namespace gs3d::data