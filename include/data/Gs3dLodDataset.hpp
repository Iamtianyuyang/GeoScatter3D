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
     * 目标点数。
     *
     * 这里不强制精确等于目标点数。
     * 实际点数由 voxel_size 和数据空间分布决定。
     */
    std::vector<std::uint64_t> target_point_counts{
        3'000'000ull,
        1'000'000ull,
        300'000ull
    };

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