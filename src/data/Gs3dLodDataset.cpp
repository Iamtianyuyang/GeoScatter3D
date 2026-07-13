#include "data/Gs3dLodDataset.hpp"
#include "util/Log.hpp"
#include "util/ThreadPool.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <future>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace gs3d::data {

namespace {

struct VoxelKey {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;

    [[nodiscard]]
    bool operator==(const VoxelKey& other) const noexcept {
        return x == other.x &&
               y == other.y &&
               z == other.z;
    }
};

struct VoxelKeyHash {
    [[nodiscard]]
    std::size_t operator()(const VoxelKey& key) const noexcept {
        std::uint64_t h = 1469598103934665603ull;

        const auto mix = [&](std::uint64_t v) {
            h ^= v;
            h *= 1099511628211ull;
        };

        mix(static_cast<std::uint64_t>(key.x));
        mix(static_cast<std::uint64_t>(key.y));
        mix(static_cast<std::uint64_t>(key.z));

        return static_cast<std::size_t>(h);
    }
};

[[nodiscard]]
std::uint32_t resolve_lod_build_threads(
    std::size_t level_task_count
) noexcept {
    if (level_task_count <= 1) {
        return 1;
    }

    const auto hw = std::thread::hardware_concurrency();
    if (hw <= 1) {
        return 1;
    }

    return std::min<std::uint32_t>(
        static_cast<std::uint32_t>(level_task_count),
        std::min<std::uint32_t>(hw, 8)
    );
}

[[nodiscard]]
float safe_extent(float min_value, float max_value) noexcept {
    const float extent = max_value - min_value;
    return std::max(extent, 1.0e-6f);
}

[[nodiscard]]
float estimate_voxel_size_xy(
    const Gs3dDataset& dataset,
    std::uint64_t target_point_count,
    float voxel_scale
) {
    if (target_point_count == 0) {
        throw std::runtime_error(
            "Gs3dLodDataset: target_point_count must be greater than zero"
        );
    }

    const float extent_x =
        safe_extent(dataset.bbox_min_x(), dataset.bbox_max_x());

    const float extent_y =
        safe_extent(dataset.bbox_min_y(), dataset.bbox_max_y());

    const double area =
        static_cast<double>(extent_x) *
        static_cast<double>(extent_y);

    const double voxel_area =
        area / static_cast<double>(target_point_count);

    float voxel_size =
        static_cast<float>(std::sqrt(std::max(voxel_area, 1.0e-12)));

    voxel_size *= std::max(voxel_scale, 1.0e-6f);

    return std::max(voxel_size, 1.0e-6f);
}

[[nodiscard]]
float estimate_voxel_size_xyz(
    const Gs3dDataset& dataset,
    std::uint64_t target_point_count,
    float voxel_scale
) {
    if (target_point_count == 0) {
        throw std::runtime_error(
            "Gs3dLodDataset: target_point_count must be greater than zero"
        );
    }

    const float extent_x =
        safe_extent(dataset.bbox_min_x(), dataset.bbox_max_x());

    const float extent_y =
        safe_extent(dataset.bbox_min_y(), dataset.bbox_max_y());

    const float extent_z =
        safe_extent(dataset.bbox_min_z(), dataset.bbox_max_z());

    const double volume =
        static_cast<double>(extent_x) *
        static_cast<double>(extent_y) *
        static_cast<double>(extent_z);

    const double voxel_volume =
        volume / static_cast<double>(target_point_count);

    float voxel_size =
        static_cast<float>(std::cbrt(std::max(voxel_volume, 1.0e-12)));

    voxel_size *= std::max(voxel_scale, 1.0e-6f);

    return std::max(voxel_size, 1.0e-6f);
}

[[nodiscard]]
float estimate_voxel_size(
    const Gs3dDataset& dataset,
    std::uint64_t target_point_count,
    float voxel_scale,
    Gs3dLodVoxelMode voxel_mode
) {
    if (voxel_mode == Gs3dLodVoxelMode::XYZ) {
        return estimate_voxel_size_xyz(
            dataset,
            target_point_count,
            voxel_scale
        );
    }

    return estimate_voxel_size_xy(
        dataset,
        target_point_count,
        voxel_scale
    );
}

[[nodiscard]]
VoxelKey make_voxel_key_xy(
    const Gs3dPoint& point,
    const Gs3dDataset& dataset,
    float voxel_size
) noexcept {
    const float inv_voxel = 1.0f / voxel_size;

    const auto ix = static_cast<std::int64_t>(
        std::floor((point.x - dataset.bbox_min_x()) * inv_voxel)
    );

    const auto iy = static_cast<std::int64_t>(
        std::floor((point.y - dataset.bbox_min_y()) * inv_voxel)
    );

    return {ix, iy, 0};
}

[[nodiscard]]
VoxelKey make_voxel_key_xyz(
    const Gs3dPoint& point,
    const Gs3dDataset& dataset,
    float voxel_size
) noexcept {
    const float inv_voxel = 1.0f / voxel_size;

    const auto ix = static_cast<std::int64_t>(
        std::floor((point.x - dataset.bbox_min_x()) * inv_voxel)
    );

    const auto iy = static_cast<std::int64_t>(
        std::floor((point.y - dataset.bbox_min_y()) * inv_voxel)
    );

    const auto iz = static_cast<std::int64_t>(
        std::floor((point.z - dataset.bbox_min_z()) * inv_voxel)
    );

    return {ix, iy, iz};
}

[[nodiscard]]
VoxelKey make_voxel_key(
    const Gs3dPoint& point,
    const Gs3dDataset& dataset,
    float voxel_size,
    Gs3dLodVoxelMode voxel_mode
) noexcept {
    if (voxel_mode == Gs3dLodVoxelMode::XYZ) {
        return make_voxel_key_xyz(
            point,
            dataset,
            voxel_size
        );
    }

    return make_voxel_key_xy(
        point,
        dataset,
        voxel_size
    );
}

[[nodiscard]]
Gs3dLodLevel make_empty_level(
    const Gs3dDataset& dataset,
    std::uint32_t level_index,
    std::uint64_t target_point_count,
    float voxel_size,
    Gs3dLodVoxelMode voxel_mode
) {
    Gs3dLodLevel level;
    level.level_index = level_index;
    level.name = "lod_" + std::to_string(level_index);
    level.source_point_count = dataset.point_count();
    level.target_point_count = target_point_count;
    level.voxel_size = voxel_size;
    level.voxel_mode = voxel_mode;
    return level;
}

[[nodiscard]]
Gs3dLodLevel build_voxel_level_serial(
    const Gs3dDataset& dataset,
    std::uint32_t level_index,
    std::uint64_t target_point_count,
    float voxel_scale,
    Gs3dLodVoxelMode voxel_mode,
    float explicit_voxel_size = 0.0f
) {
    const auto start_time =
        std::chrono::steady_clock::now();

    const float voxel_size =
        explicit_voxel_size > 0.0f
            ? explicit_voxel_size
            : estimate_voxel_size(
                dataset,
                target_point_count,
                voxel_scale,
                voxel_mode
            );

    Gs3dLodLevel level =
        make_empty_level(
            dataset,
            level_index,
            target_point_count,
            voxel_size,
            voxel_mode
        );

    const std::size_t reserve_count =
        static_cast<std::size_t>(
            std::min<std::uint64_t>(
                target_point_count,
                dataset.point_count()
            )
        );

    level.points.reserve(reserve_count);

    std::unordered_set<VoxelKey, VoxelKeyHash> occupied_voxels;
    occupied_voxels.reserve(
        static_cast<std::size_t>(
            std::min<std::uint64_t>(
                target_point_count * 2ull,
                dataset.point_count()
            )
        )
    );

    const auto& source_points = dataset.points();

    for (const auto& point : source_points) {
        const VoxelKey key =
            make_voxel_key(
                point,
                dataset,
                voxel_size,
                voxel_mode
            );

        const auto [_, inserted] =
            occupied_voxels.insert(key);

        if (inserted) {
            level.points.push_back(point);
        }
    }

    const auto end_time =
        std::chrono::steady_clock::now();

    level.build_seconds =
        std::chrono::duration<double>(
            end_time - start_time
        ).count();

    return level;
}

struct ChunkFirstPoint {
    const Gs3dPoint* point = nullptr;
    VoxelKey key{};
};

struct ChunkFirstPointResult {
    std::vector<ChunkFirstPoint> ordered_points;
};

[[nodiscard]]
Gs3dLodLevel build_voxel_level_parallel(
    const Gs3dDataset& dataset,
    std::uint32_t level_index,
    std::uint64_t target_point_count,
    float voxel_scale,
    Gs3dLodVoxelMode voxel_mode,
    float explicit_voxel_size = 0.0f
) {
    const auto start_time =
        std::chrono::steady_clock::now();

    const float voxel_size =
        explicit_voxel_size > 0.0f
            ? explicit_voxel_size
            : estimate_voxel_size(
                dataset,
                target_point_count,
                voxel_scale,
                voxel_mode
            );

    Gs3dLodLevel level =
        make_empty_level(
            dataset,
            level_index,
            target_point_count,
            voxel_size,
            voxel_mode
        );

    const auto& source_points = dataset.points();
    const std::size_t point_count = source_points.size();
    const std::uint32_t thread_count =
        resolve_lod_build_threads(point_count);

    if (thread_count <= 1 || point_count < 100'000) {
        return build_voxel_level_serial(
            dataset,
            level_index,
            target_point_count,
            voxel_scale,
            voxel_mode,
            explicit_voxel_size
        );
    }

    const std::size_t reserve_count =
        static_cast<std::size_t>(
            std::min<std::uint64_t>(
                target_point_count,
                dataset.point_count()
            )
        );
    level.points.reserve(reserve_count);

    const std::size_t chunk_size =
        (point_count + static_cast<std::size_t>(thread_count) - 1u) /
        static_cast<std::size_t>(thread_count);

    gs3d::util::ThreadPool pool(thread_count);
    std::vector<std::future<ChunkFirstPointResult>> futures;
    futures.reserve(thread_count);

    for (std::uint32_t task_index = 0; task_index < thread_count; ++task_index) {
        const std::size_t begin =
            static_cast<std::size_t>(task_index) * chunk_size;
        if (begin >= point_count) {
            break;
        }

        const std::size_t end =
            std::min(begin + chunk_size, point_count);

        futures.push_back(pool.submit([&, begin, end] {
            ChunkFirstPointResult result;
            result.ordered_points.reserve(
                static_cast<std::size_t>(
                    std::max<std::uint64_t>(
                        1ull,
                        std::min<std::uint64_t>(
                            target_point_count,
                            static_cast<std::uint64_t>(end - begin)
                        ) * 2ull
                    )
                )
            );
            std::unordered_set<VoxelKey, VoxelKeyHash> local_occupied_voxels;
            local_occupied_voxels.reserve(
                result.ordered_points.capacity() * 2u
            );

            for (std::size_t i = begin; i < end; ++i) {
                const auto& point = source_points[i];
                const VoxelKey key =
                    make_voxel_key(
                        point,
                        dataset,
                        voxel_size,
                        voxel_mode
                    );

                const auto [_, inserted] =
                    local_occupied_voxels.insert(key);
                if (inserted) {
                    result.ordered_points.push_back(
                        ChunkFirstPoint{&point, key}
                    );
                }
            }

            return result;
        }));
    }

    std::unordered_set<VoxelKey, VoxelKeyHash> occupied_voxels;
    occupied_voxels.reserve(
        static_cast<std::size_t>(
            std::min<std::uint64_t>(
                target_point_count * 2ull,
                dataset.point_count()
            )
        )
    );

    for (auto& future : futures) {
        auto chunk_result = future.get();
        for (const auto& first_point : chunk_result.ordered_points) {
            auto [it, inserted] =
                occupied_voxels.insert(first_point.key);
            if (inserted) {
                level.points.push_back(*first_point.point);
            }
        }
    }

    const auto end_time =
        std::chrono::steady_clock::now();

    level.build_seconds =
        std::chrono::duration<double>(
            end_time - start_time
        ).count();

    return level;
}

[[nodiscard]]
Gs3dLodLevel build_voxel_level(
    const Gs3dDataset& dataset,
    std::uint32_t level_index,
    std::uint64_t target_point_count,
    float voxel_scale,
    Gs3dLodVoxelMode voxel_mode,
    float explicit_voxel_size = 0.0f
) {
    return build_voxel_level_parallel(
        dataset,
        level_index,
        target_point_count,
        voxel_scale,
        voxel_mode,
        explicit_voxel_size
    );
}

[[nodiscard]]
Gs3dLodLevel build_full_copy_level(
    const Gs3dDataset& dataset
) {
    const auto start_time =
        std::chrono::steady_clock::now();

    Gs3dLodLevel level;
    level.name = "full";
    level.level_index = 0;
    level.source_point_count = dataset.point_count();
    level.target_point_count = dataset.point_count();
    level.voxel_size = 0.0f;
    level.voxel_mode = Gs3dLodVoxelMode::XYZ;
    level.points = dataset.points();

    const auto end_time =
        std::chrono::steady_clock::now();

    level.build_seconds =
        std::chrono::duration<double>(
            end_time - start_time
        ).count();

    return level;
}

} // namespace

std::uint64_t Gs3dLodLevel::point_count() const noexcept {
    return static_cast<std::uint64_t>(points.size());
}

std::uint64_t Gs3dLodLevel::point_bytes() const noexcept {
    return static_cast<std::uint64_t>(points.size()) *
           static_cast<std::uint64_t>(sizeof(Gs3dPoint));
}

bool Gs3dLodLevel::empty() const noexcept {
    return points.empty();
}

Gs3dLodDataset Gs3dLodDataset::build(
    const Gs3dDataset& dataset,
    const Gs3dLodBuildConfig& config
) {
    if (!dataset.is_consistent()) {
        throw std::runtime_error(
            "Gs3dLodDataset: source dataset is inconsistent"
        );
    }

    if (dataset.empty()) {
        throw std::runtime_error(
            "Gs3dLodDataset: source dataset is empty"
        );
    }

    if (!dataset.has_point_data()) {
        throw std::runtime_error(
            "Gs3dLodDataset: source dataset has no loaded point data"
        );
    }

    if (config.finest_target_points == 0 &&
        !config.include_full_resolution_level) {
        throw std::runtime_error(
            "Gs3dLodDataset: no LOD levels requested"
        );
    }

    Gs3dLodDataset lod_dataset;
    lod_dataset.source_header_ = dataset.header();
    lod_dataset.source_path_ = dataset.source_path();

    if (config.include_full_resolution_level) {
        auto level =
            build_full_copy_level(dataset);

        if (config.verbose) {
            gs3d::util::log::info() << "[LOD] level "
                      << level.level_index
                      << " full: points="
                      << level.point_count()
                      << ", bytes="
                      << level.point_bytes()
                      << ", build_seconds="
                      << level.build_seconds
                      << '\n';
        }

        lod_dataset.add_level(std::move(level));
    }

    if (config.finest_target_points == 0) {
        return lod_dataset;
    }

    /*
     * Potree 式自动分层：
     *   voxel_size_0 = sqrt(extent_x * extent_y / finest_target_points)
     *                  * voxel_scale
     *   每层 voxel_size ×= growth_factor
     *   停止条件：点数 < min_points_per_level
     */
    const float extent_x =
        safe_extent(dataset.bbox_min_x(), dataset.bbox_max_x());
    const float extent_y =
        safe_extent(dataset.bbox_min_y(), dataset.bbox_max_y());
    const float max_extent = std::max(extent_x, extent_y);

    const float base_voxel_size =
        estimate_voxel_size(
            dataset,
            config.finest_target_points,
            config.voxel_scale,
            config.voxel_mode
        );

    if (config.verbose) {
        gs3d::util::log::info() << "[LOD] auto-layer: finest_target="
                  << config.finest_target_points
                  << ", growth=" << config.growth_factor
                  << ", min_points=" << config.min_points_per_level
                  << ", base_voxel_size=" << base_voxel_size
                  << ", max_extent=" << max_extent
                  << '\n';
    }

    std::vector<Gs3dLodLevel> built_levels;
    std::uint32_t level_index =
        config.include_full_resolution_level ? 1u : 0u;
    float voxel_size = base_voxel_size;

    // 估算面积（用于每层 target 的计算，仅 XY 模式有意义）
    const double area =
        static_cast<double>(extent_x) *
        static_cast<double>(extent_y);
    // XYZ 模式用体积
    const float extent_z =
        safe_extent(dataset.bbox_min_z(), dataset.bbox_max_z());
    const double volume =
        static_cast<double>(extent_x) *
        static_cast<double>(extent_y) *
        static_cast<double>(extent_z);

    while (true) {
        // 估算当前 voxel_size 下的期望点数（用于 reserve + metadata）
        std::uint64_t expected_points;
        if (config.voxel_mode == Gs3dLodVoxelMode::XYZ) {
            const double vs3 =
                static_cast<double>(voxel_size) *
                static_cast<double>(voxel_size) *
                static_cast<double>(voxel_size);
            expected_points = static_cast<std::uint64_t>(
                std::min(volume / std::max(vs3, 1.0e-12),
                         static_cast<double>(dataset.point_count()))
            );
        } else {
            const double vs2 =
                static_cast<double>(voxel_size) *
                static_cast<double>(voxel_size);
            expected_points = static_cast<std::uint64_t>(
                std::min(area / std::max(vs2, 1.0e-12),
                         static_cast<double>(dataset.point_count()))
            );
        }
        if (expected_points == 0) expected_points = 1;

        // Build one level at the current voxel_size.
        auto level =
            build_voxel_level(
                dataset,
                level_index,
                expected_points,
                config.voxel_scale,
                config.voxel_mode,
                voxel_size
            );

        const auto actual_points = level.point_count();

        if (config.verbose) {
            gs3d::util::log::info() << "[LOD] level "
                      << level.level_index
                      << ": mode="
                      << voxel_mode_name(level.voxel_mode)
                      << ", target="
                      << level.target_point_count
                      << ", actual="
                      << actual_points
                      << ", voxel_size="
                      << level.voxel_size
                      << ", bytes="
                      << level.point_bytes()
                      << ", build_seconds="
                      << level.build_seconds
                      << '\n';
        }

        if (actual_points < config.min_points_per_level) {
            if (config.verbose) {
                gs3d::util::log::info() << "[LOD] auto-layer: stopping at level "
                          << level_index
                          << " (points=" << actual_points
                          << " < min=" << config.min_points_per_level
                          << ")\n";
            }
            break;
        }

        built_levels.push_back(std::move(level));

        voxel_size *= config.growth_factor;
        if (voxel_size > max_extent * 2.0f) {
            if (config.verbose) {
                gs3d::util::log::info() << "[LOD] auto-layer: stopping — "
                          << "voxel_size=" << voxel_size
                          << " exceeds 2× max_extent=" << max_extent
                          << '\n';
            }
            break;
        }

        ++level_index;
    }

    if (config.verbose) {
        gs3d::util::log::info() << "[LOD] auto-layer: built "
                  << built_levels.size()
                  << " levels\n";
    }

    for (auto& level : built_levels) {
        lod_dataset.add_level(std::move(level));
    }

    return lod_dataset;
}

bool Gs3dLodDataset::empty() const noexcept {
    return levels_.empty();
}

Gs3dLodDataset Gs3dLodDataset::from_levels(
    const Gs3dHeader& source_header,
    const std::filesystem::path& source_path,
    std::vector<Gs3dLodLevel> levels
) {
    if (levels.empty()) {
        throw std::runtime_error(
            "Gs3dLodDataset: from_levels received empty levels"
        );
    }

    Gs3dLodDataset dataset;
    dataset.source_header_ = source_header;
    dataset.source_path_ = source_path;
    dataset.levels_ = std::move(levels);

    return dataset;
}
std::size_t Gs3dLodDataset::level_count() const noexcept {
    return levels_.size();
}

const Gs3dLodLevel& Gs3dLodDataset::level(
    std::size_t index
) const {
    if (index >= levels_.size()) {
        throw std::out_of_range(
            "Gs3dLodDataset: level index out of range"
        );
    }

    return levels_[index];
}

const std::vector<Gs3dLodLevel>&
Gs3dLodDataset::levels() const noexcept {
    return levels_;
}

const Gs3dHeader& Gs3dLodDataset::source_header() const noexcept {
    return source_header_;
}

const std::filesystem::path&
Gs3dLodDataset::source_path() const noexcept {
    return source_path_;
}

std::string Gs3dLodDataset::summary() const {
    std::ostringstream oss;

    oss << "Gs3dLodDataset\n";
    oss << "  source_path: "
        << source_path_.string()
        << '\n';

    oss << "  source_point_count: "
        << source_header_.point_count
        << '\n';

    oss << "  level_count: "
        << level_count()
        << '\n';

    for (const auto& level : levels_) {
        oss << "  level "
            << level.level_index
            << " ["
            << level.name
            << "]"
            << ": mode="
            << voxel_mode_name(level.voxel_mode)
            << ", points="
            << level.point_count()
            << ", target="
            << level.target_point_count
            << ", voxel_size="
            << level.voxel_size
            << ", bytes="
            << level.point_bytes()
            << ", build_seconds="
            << level.build_seconds
            << '\n';
    }

    return oss.str();
}

const char* Gs3dLodDataset::voxel_mode_name(
    Gs3dLodVoxelMode mode
) noexcept {
    switch (mode) {
    case Gs3dLodVoxelMode::XY:
        return "XY";
    case Gs3dLodVoxelMode::XYZ:
        return "XYZ";
    default:
        return "Unknown";
    }
}

void Gs3dLodDataset::add_level(
    Gs3dLodLevel level
) {
    levels_.push_back(std::move(level));
}

} // namespace gs3d::data
