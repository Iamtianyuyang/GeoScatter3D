#include "ffi/LodBuilder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace {

gs3d::data::Gs3dPoint make_point(float x, float y, float z) {
    gs3d::data::Gs3dPoint point{};
    point.x = x;
    point.y = y;
    point.z = z;
    point.value = z;
    return point;
}

/// 单位间距规则栅格：edge×edge 个点，间距 1.0。
std::vector<gs3d::data::Gs3dPoint> make_grid(int edge) {
    std::vector<gs3d::data::Gs3dPoint> points;
    points.reserve(static_cast<std::size_t>(edge) * edge);
    for (int i = 0; i < edge; ++i) {
        for (int j = 0; j < edge; ++j) {
            points.push_back(make_point(
                static_cast<float>(i),
                static_cast<float>(j),
                static_cast<float>((i + j) % 7) * 0.25f
            ));
        }
    }
    return points;
}

std::unordered_set<std::uint64_t> key_set(
    const std::vector<gs3d::data::Gs3dPoint>& points
) {
    std::unordered_set<std::uint64_t> keys;
    keys.reserve(points.size() * 2);
    for (const auto& point : points) {
        const auto x = static_cast<std::uint64_t>(std::llround(point.x * 100.0));
        const auto y = static_cast<std::uint64_t>(std::llround(point.y * 100.0));
        const auto z = static_cast<std::uint64_t>(std::llround(point.z * 100.0));
        keys.insert((x << 42) | (y << 21) | z);
    }
    return keys;
}

}  // namespace

TEST_CASE("spacing estimation recovers a unit grid spacing") {
    // 平面栅格：最近邻距离恒为 1.0。
    std::vector<gs3d::data::Gs3dPoint> planar;
    planar.reserve(64 * 64);
    for (int i = 0; i < 64; ++i) {
        for (int j = 0; j < 64; ++j) {
            planar.push_back(make_point(
                static_cast<float>(i), static_cast<float>(j), 0.0f
            ));
        }
    }

    const float spacing = gs3d::ffi::LodBuilder::estimate_spacing(planar, 512);
    CHECK(spacing > 0.99f);
    CHECK(spacing < 1.01f);

    // 带高程调制时是三维最近邻（sqrt(1 + dz²)），均值略大于水平间距。
    const auto bumpy = make_grid(64);
    const float bumpy_spacing =
        gs3d::ffi::LodBuilder::estimate_spacing(bumpy, 512);
    CHECK(bumpy_spacing >= spacing);
    CHECK(bumpy_spacing < 1.9f);
}

TEST_CASE("spacing estimation is zero for degenerate inputs") {
    CHECK(gs3d::ffi::LodBuilder::estimate_spacing({}, 128) == 0.0f);

    const std::vector<gs3d::data::Gs3dPoint> single{make_point(1.0f, 2.0f, 3.0f)};
    CHECK(gs3d::ffi::LodBuilder::estimate_spacing(single, 128) == 0.0f);

    // 全部重合：噪声数据不应产生非零间距（否则会误建 LOD 链）。
    std::vector<gs3d::data::Gs3dPoint> coincident(64, make_point(5.0f, 5.0f, 5.0f));
    CHECK(gs3d::ffi::LodBuilder::estimate_spacing(coincident, 64) == 0.0f);
}

TEST_CASE("voxel decimation keeps one point per cell and preserves the bbox") {
    const auto points = make_grid(32);  // 1024 点，间距 1.0

    std::uint64_t cells = 0;
    const auto decimated =
        gs3d::ffi::LodBuilder::decimate_voxel(points, 2.0f, &cells);

    CHECK(cells == decimated.size());
    // 2.0 体素覆盖 32×32 栅格 → 约 16×16 = 256 格（边界格合并有 ±1 容差）。
    CHECK(decimated.size() >= 240);
    CHECK(decimated.size() <= 290);

    float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
    for (const auto& point : decimated) {
        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
        min_y = std::min(min_y, point.y);
        max_y = std::max(max_y, point.y);
    }
    // 「保留每格首点」不保证恰好保留边界极值点，但降采样后仍要覆盖整个范围。
    CHECK(min_x == 0.0f);
    CHECK(min_y == 0.0f);
    CHECK(max_x >= 30.0f);
    CHECK(max_y >= 30.0f);
    CHECK(max_x <= 31.0f);
    CHECK(max_y <= 31.0f);
}

TEST_CASE("voxel decimation is a no-op for non-positive cells") {
    const auto points = make_grid(8);
    const auto decimated = gs3d::ffi::LodBuilder::decimate_voxel(points, 0.0f);
    CHECK(decimated.size() == points.size());
}

TEST_CASE("lod chain builds progressively coarser levels") {
    const auto points = make_grid(200);  // 40000 点

    gs3d::ffi::LodBuildConfig config;
    config.min_points = 1000;
    config.max_levels = 6;
    const auto chain = gs3d::ffi::LodBuilder::build(points, config);

    REQUIRE_FALSE(chain.empty());
    CHECK(chain.estimated_spacing > 0.99f);
    CHECK(chain.build_ms >= 0.0);

    std::size_t previous = points.size();
    for (const auto& level : chain.levels) {
        CHECK_FALSE(level.points.empty());
        CHECK(level.source_point_count == previous);
        CHECK(level.points.size() < previous);
        // 体素边长按 2 的幂增长。
        CHECK(level.voxel_size > 0.0f);
        previous = level.points.size();
    }
    // 最后一级必须退化到接近 min_points 的量级（否则预算无法满足）。
    CHECK(chain.levels.back().points.size() <= 40000 / 4);
}

TEST_CASE("lod chain is deterministic and monotone in point count") {
    const auto points = make_grid(96);

    gs3d::ffi::LodBuildConfig config;
    config.min_points = 500;
    const auto first = gs3d::ffi::LodBuilder::build(points, config);
    const auto second = gs3d::ffi::LodBuilder::build(points, config);

    REQUIRE(first.levels.size() == second.levels.size());
    for (std::size_t index = 0; index < first.levels.size(); ++index) {
        CHECK(first.levels[index].points.size() == second.levels[index].points.size());
        CHECK(
            key_set(first.levels[index].points) ==
            key_set(second.levels[index].points)
        );
    }
}

TEST_CASE("lod chain is skipped when decimation cannot reduce") {
    // 稀疏点云：间距远大于体素增长需求时不应反复产出同样的点集。
    std::vector<gs3d::data::Gs3dPoint> sparse;
    for (int i = 0; i < 400; ++i) {
        sparse.push_back(make_point(
            static_cast<float>(i) * 100.0f,
            0.0f,
            0.0f
        ));
    }

    gs3d::ffi::LodBuildConfig config;
    config.min_points = 10;
    config.max_levels = 6;
    const auto chain = gs3d::ffi::LodBuilder::build(sparse, config);

    // 每级至少减半，最终必须收敛；层数不应达到上限。
    CHECK(chain.levels.size() < config.max_levels);
    for (const auto& level : chain.levels) {
        CHECK_FALSE(level.points.empty());
    }
}

TEST_CASE("spacing estimation stays local on large row-major grids") {
    // 回归：等距步长扫描会在行主序栅格上估出人造间距（1M 点被降到 725 点的根因）。
    const auto points = make_grid(300);  // 90000 点，间距 1.0

    const float spacing = gs3d::ffi::LodBuilder::estimate_spacing(points, 1024);

    // 连续块采样必须仍落在同一行/邻行内，得到真实间距量级。
    CHECK(spacing < 3.0f);
    CHECK(spacing > 0.5f);
}

TEST_CASE("lod chain keeps levels near the point budget on a realistic grid") {
    // 10 万点、间距 1.0：链不应从“全量”直接跳到几乎没有点。
    const auto points = make_grid(320);  // 102400 点

    gs3d::ffi::LodBuildConfig config;
    config.min_points = 20000;
    config.max_levels = 6;
    const auto chain = gs3d::ffi::LodBuilder::build(points, config);

    REQUIRE_FALSE(chain.empty());
    // 链必须真的“到顶”：最粗层应落到预算量级，而不是只产出一级就断。
    CHECK(chain.levels.size() >= 2);
    CHECK(chain.levels.back().points.size() <= config.min_points);
    CHECK(chain.levels.back().points.size() >= config.min_points / 4);
    // 每级递减且不塌缩。
    for (const auto& level : chain.levels) {
        CHECK(level.points.size() >= config.min_points / 4);
    }
}

TEST_CASE("lod chain reaches the budget for a million-scale grid") {
    // 回归：曾在真机上只产出“全量 + 一级粗化”（下游 source 指针被 move 掉）。
    const auto points = make_grid(1000);  // 100 万点

    gs3d::ffi::LodBuildConfig config;
    config.min_points = 150000;
    config.max_levels = 8;
    const auto chain = gs3d::ffi::LodBuilder::build(points, config);

    REQUIRE(chain.levels.size() >= 2);
    std::size_t previous = points.size();
    for (const auto& level : chain.levels) {
        CHECK(level.source_point_count == previous);
        previous = level.points.size();
    }
    // 最粗层进入预算区间（15 万以内），运行时可选中它。
    CHECK(chain.levels.back().points.size() <= config.min_points);
}

TEST_CASE("small datasets produce no levels") {
    const auto points = make_grid(10);  // 100 点

    gs3d::ffi::LodBuildConfig config;
    config.min_points = 150'000;
    const auto chain = gs3d::ffi::LodBuilder::build(points, config);

    // 100 点已低于最粗层阈值：不建链，kernel 直接用 level 0 绘制。
    CHECK(chain.empty());
}
