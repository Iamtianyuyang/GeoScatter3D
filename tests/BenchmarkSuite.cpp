#include "app/AppConfig.hpp"
#include "app/ViewerApp.hpp"
#include "camera/MouseRay.hpp"
#include "core/PointData.hpp"
#include "data/Gs3dDataset.hpp"
#include "data/Gs3dFormat.hpp"
#include "data/Gs3dLodReader.hpp"
#include "data/Gs3dLodTargets.hpp"
#include "data/Gs3dReader.hpp"
#include "data/Gs3dTileReader.hpp"
#include "data/PointDataAdapters.hpp"
#include "data/TileDataAdapters.hpp"
#include "preprocess/Gs3dLodWriter.hpp"
#include "preprocess/Gs3dTileWriter.hpp"
#include "render/TileSelection.hpp"
#include "util/PercentileStats.hpp"
#include "util/Stopwatch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

int failures = 0;

constexpr std::uint64_t kHoverTileCandidateBudget = 3'000'000ull;

void expect(bool condition, std::string_view name)
{
    if (!condition) {
        std::cerr << "[FAIL] " << name << '\n';
        ++failures;
    }
}

void test_percentile_of_empty_input_is_zero()
{
    expect(
        gs3d::util::percentile({}, 50.0) == 0.0,
        "percentile of empty input is 0.0"
    );
}

void test_percentile_of_single_element_ignores_p()
{
    expect(
        gs3d::util::percentile({42.0}, 0.0) == 42.0,
        "p0 of single-element input is that element"
    );
    expect(
        gs3d::util::percentile({42.0}, 99.0) == 42.0,
        "p99 of single-element input is that element"
    );
}

void test_percentile_p0_and_p100_are_min_and_max()
{
    const std::vector<double> samples = {5.0, 1.0, 3.0, 4.0, 2.0};
    expect(
        gs3d::util::percentile(samples, 0.0) == 1.0,
        "p0 returns the minimum"
    );
    expect(
        gs3d::util::percentile(samples, 100.0) == 5.0,
        "p100 returns the maximum"
    );
}

void test_percentile_p50_is_median_for_odd_count()
{
    const std::vector<double> samples = {3.0, 1.0, 2.0};
    expect(
        gs3d::util::percentile(samples, 50.0) == 2.0,
        "p50 of {1,2,3} is the median 2.0"
    );
}

void test_percentile_interpolates_between_values()
{
    // Sorted: {0, 10}. p25 -> 0 + 0.25 * (10 - 0) = 2.5
    const std::vector<double> samples = {10.0, 0.0};
    expect(
        gs3d::util::percentile(samples, 25.0) == 2.5,
        "p25 of {0,10} interpolates to 2.5"
    );
}

struct PercentileTriple {
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    bool has_samples = false;
};

struct PickBatchSummary {
    std::string scenario;
    std::string mode;
    std::size_t query_count = 0;
    std::size_t hit_count = 0;
    std::uint64_t candidate_point_count = 0;
    bool candidate_uses_tiles = false;
    bool budget_exceeded = false;
    PercentileTriple delta_px{};
    PercentileTriple latency_ms{};
};

struct OrderSensitivitySummary {
    std::string scenario;
    std::size_t query_count = 0;
    std::size_t flip_count = 0;
    PercentileTriple latency_ms{};
};

struct PickQuery {
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    float max_screen_distance_px = 12.0f;
};

struct BoundarySelectionCase {
    std::string name;
    gs3d::camera::Camera camera;
    gs3d::camera::Viewport viewport{};
    std::vector<std::uint64_t> tile_ids;
    std::vector<std::shared_ptr<std::vector<gs3d::data::Gs3dPoint>>> tiles;
    std::vector<PickQuery> queries;
    std::uint64_t total_tile_points = 0;
};

struct CandidateAssembly {
    std::vector<gs3d::core::PointDataView> views;
    std::uint64_t point_count = 0;
    bool used_tile_points = false;
    bool budget_exceeded = false;
    std::size_t base_view_count = 0;
    std::vector<std::pair<std::uint64_t, gs3d::core::PointDataView>> tile_views{};
};

struct ObservedPickResult {
    std::size_t query_index = 0;
    bool has_hit = false;
    bool all_tiles_resident = false;
    float depth = 1.0f;
    std::optional<gs3d::core::PointRecord> point{};
    double issue_cpu_ms = 0.0;
    double collect_cpu_ms = 0.0;
    std::vector<std::uint64_t> resident_tile_ids{};
};

struct SyntheticScenario {
    std::string name;
    gs3d::camera::Camera camera;
    gs3d::camera::Viewport viewport{};
    gs3d::core::PointBuffer full_points;
    gs3d::core::PointBuffer lod_points;
    std::vector<PickQuery> queries;
};

[[nodiscard]]
std::vector<std::uint64_t> parse_tile_id_list(std::string_view encoded);

[[nodiscard]]
PercentileTriple make_percentiles(const std::vector<double>& samples)
{
    PercentileTriple stats;
    if (samples.empty()) {
        return stats;
    }

    stats.has_samples = true;
    stats.p50 = gs3d::util::percentile(samples, 50.0);
    stats.p95 = gs3d::util::percentile(samples, 95.0);
    stats.p99 = gs3d::util::percentile(samples, 99.0);
    return stats;
}

[[nodiscard]]
bool same_point_record(
    const gs3d::core::PointRecord& a,
    const gs3d::core::PointRecord& b
) noexcept
{
    return a.x == b.x &&
           a.y == b.y &&
           a.z == b.z &&
           a.value == b.value;
}

[[nodiscard]]
std::string json_escape(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

[[nodiscard]]
std::string date_tag_yyyymmdd()
{
    std::time_t now = std::time(nullptr);
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &now);
#else
    local_tm = *std::localtime(&now);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y%m%d");
    return oss.str();
}

[[nodiscard]]
gs3d::camera::Vec3 to_vec3(const std::array<float, 3>& value)
{
    return {value[0], value[1], value[2]};
}

[[nodiscard]]
gs3d::camera::Camera make_camera_from_config(
    const gs3d::app::ViewerAppConfig& config
)
{
    gs3d::camera::Camera camera;
    camera.set_viewport(config.window_width, config.window_height);
    camera.set_perspective(
        config.camera_fov_y,
        config.camera_near,
        config.camera_far
    );
    if (config.camera_mode == "fit") {
        gs3d::camera::CameraBounds bounds{
            {-1000.0f, -1000.0f, -1000.0f},
            {1000.0f, 1000.0f, 1000.0f}
        };
        camera.fit_bounds(bounds);
    } else {
        camera.look_at(
            to_vec3(config.camera_position),
            to_vec3(config.camera_target),
            to_vec3(config.camera_up)
        );
    }
    return camera;
}

[[nodiscard]]
gs3d::data::Gs3dLodVoxelMode parse_lod_voxel_mode(const std::string& mode)
{
    if (mode == "XY" || mode == "xy") {
        return gs3d::data::Gs3dLodVoxelMode::XY;
    }
    if (mode == "XYZ" || mode == "xyz") {
        return gs3d::data::Gs3dLodVoxelMode::XYZ;
    }
    throw std::runtime_error("unsupported LOD voxel mode: " + mode);
}

[[nodiscard]]
gs3d::data::Gs3dLodDataset ensure_lod_dataset(
    const gs3d::data::Gs3dDataset& dataset,
    const gs3d::app::AppConfig& app_config
)
{
    const auto& viewer = app_config.viewer;
    if (!viewer.lod_enabled) {
        return {};
    }

    if (!viewer.lod_sidecar_path.empty() &&
        std::filesystem::exists(viewer.lod_sidecar_path)) {
        return gs3d::data::Gs3dLodReader::read(
            viewer.lod_sidecar_path,
            dataset.header(),
            gs3d::data::Gs3dLodReadConfig{}
        ).dataset;
    }

    gs3d::data::Gs3dLodBuildConfig lod_config;
    lod_config.include_full_resolution_level = false;
    lod_config.finest_target_points = viewer.lod_finest_target_points;
    lod_config.growth_factor = viewer.lod_growth_factor;
    lod_config.min_points_per_level = viewer.lod_min_points_per_level;
    lod_config.voxel_mode = parse_lod_voxel_mode(viewer.lod_voxel_mode);
    lod_config.voxel_scale = viewer.lod_voxel_scale;
    lod_config.verbose = viewer.lod_verbose;

    auto lod_dataset = gs3d::data::Gs3dLodDataset::build(dataset, lod_config);
    if (!viewer.lod_sidecar_path.empty()) {
        const auto write_stats = gs3d::preprocess::Gs3dLodWriter::write(
            viewer.lod_sidecar_path,
            lod_dataset
        );
        (void)write_stats;
    }
    return lod_dataset;
}

[[nodiscard]]
gs3d::data::Gs3dTileReader ensure_tile_reader(
    const gs3d::data::Gs3dDataset& dataset,
    const gs3d::app::AppConfig& app_config
)
{
    const auto& viewer = app_config.viewer;
    if (!std::filesystem::exists(viewer.tile_index_path) ||
        !std::filesystem::exists(viewer.tile_data_path)) {
        gs3d::preprocess::Gs3dTileWriteConfig tile_config;
        tile_config.num_threads = app_config.tile_build.num_threads;
        tile_config.verbose = viewer.tile_verbose;
        const auto write_stats = gs3d::preprocess::Gs3dTileWriter::write(
            viewer.tile_index_path,
            viewer.tile_data_path,
            dataset,
            tile_config
        );
        (void)write_stats;
    }
    return gs3d::data::Gs3dTileReader::open(
        viewer.tile_index_path,
        viewer.tile_data_path,
        dataset.header()
    );
}

struct FrontMostPickResult {
    gs3d::core::PointRecord point{};
    float depth = 1.0f;
    float screen_distance_px = 0.0f;
};

[[nodiscard]]
float project_depth(
    const gs3d::camera::Mat4& view_projection,
    float x,
    float y,
    float z
) noexcept
{
    const float* m = view_projection.m.data();
    const float clip_z =
        m[2] * x + m[6] * y + m[10] * z + m[14];
    const float clip_w =
        m[3] * x + m[7] * y + m[11] * z + m[15];
    if (clip_w <= 1.0e-6f) {
        return 1.0f;
    }
    return clip_z / clip_w;
}

[[nodiscard]]
std::optional<FrontMostPickResult> brute_force_front_most_pick(
    const std::vector<gs3d::core::PointDataView>& candidate_point_sets,
    float mouse_x,
    float mouse_y,
    const gs3d::camera::Viewport& viewport,
    const gs3d::camera::Camera& camera
) noexcept
{
    const float width =
        static_cast<float>(viewport.width > 0 ? viewport.width : 1);
    const float height =
        static_cast<float>(viewport.height > 0 ? viewport.height : 1);
    const auto view_projection = camera.view_projection_matrix();
    const int center_x =
        static_cast<int>(std::floor(mouse_x));
    const int center_y =
        static_cast<int>(std::floor(mouse_y));

    std::optional<FrontMostPickResult> best;
    for (const auto& points : candidate_point_sets) {
        if (!points.valid() || points.empty()) {
            continue;
        }

        for (std::uint64_t i = 0; i < points.point_count; ++i) {
            const auto point = points.point_at(i);
            const auto screen = gs3d::camera::MouseRay::world_to_screen(
                view_projection,
                point.x,
                point.y,
                point.z,
                width,
                height
            );
            if (!screen) {
                continue;
            }

            const int pixel_x =
                static_cast<int>(std::floor(screen->x));
            const int pixel_y =
                static_cast<int>(std::floor(screen->y));
            // Match the production 11x11 pick block exactly (Chebyshev ≤ 5);
            // a wider tolerance here would let ground truth consider
            // points whose rasterizer pixel can never fall inside the
            // production neighborhood, turning a known-empty result
            // into a contract miss.
            if (std::max(std::abs(pixel_x - center_x),
                         std::abs(pixel_y - center_y)) > 5) {
                continue;
            }

            const float depth = project_depth(
                view_projection,
                point.x,
                point.y,
                point.z
            );
            const float dx = screen->x - mouse_x;
            const float dy = screen->y - mouse_y;
            const float distance_px = std::sqrt(dx * dx + dy * dy);
            if (!best || depth < best->depth) {
                best = FrontMostPickResult{
                    point,
                    depth,
                    distance_px
                };
            }
        }
    }

    return best;
}

[[nodiscard]]
bool matches_front_most_contract(
    const FrontMostPickResult& expected,
    const ObservedPickResult& observed,
    const PickQuery& query,
    const gs3d::camera::Viewport& viewport,
    const gs3d::camera::Camera& camera
) noexcept
{
    if (!observed.point.has_value()) {
        return false;
    }

    const auto actual_screen = gs3d::camera::MouseRay::to_screen(
        {observed.point->x, observed.point->y, observed.point->z},
        viewport,
        camera
    );
    if (!actual_screen) {
        return false;
    }

    const int center_x =
        static_cast<int>(std::floor(query.mouse_x));
    const int center_y =
        static_cast<int>(std::floor(query.mouse_y));
    const int pixel_x =
        static_cast<int>(std::floor(actual_screen->x));
    const int pixel_y =
        static_cast<int>(std::floor(actual_screen->y));
    if (std::max(std::abs(pixel_x - center_x),
                 std::abs(pixel_y - center_y)) > 5) {
        return false;
    }

    constexpr float kDepthEpsilon = 2.0e-3f;
    return observed.depth <= expected.depth + kDepthEpsilon;
}

void write_pick_script(
    const std::vector<PickQuery>& queries,
    const std::filesystem::path& script_path
)
{
    std::error_code ec;
    std::filesystem::create_directories(script_path.parent_path(), ec);
    std::ofstream out(script_path, std::ios::trunc);
    if (!out) {
        throw std::runtime_error(
            "failed to open pick script path: " + script_path.string()
        );
    }
    for (const auto& query : queries) {
        out << query.mouse_x << ' ' << query.mouse_y << '\n';
    }
}

[[nodiscard]]
std::vector<ObservedPickResult> read_pick_results(
    const std::filesystem::path& result_path
)
{
    std::ifstream in(result_path);
    if (!in) {
        throw std::runtime_error(
            "failed to open pick result path: " + result_path.string()
        );
    }

    std::vector<ObservedPickResult> results;
    std::string line;
    bool first_line = true;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        if (first_line) {
            first_line = false;
            continue;
        }

        std::istringstream iss(line);
        ObservedPickResult result;
        int has_hit = 0;
        int gpu_has_hit = 0;
        int all_tiles_resident = 0;
        std::uint32_t point_id = 0;
        float depth = 1.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float value = 0.0f;
        std::string resident_tile_ids;
        if (!(iss >> result.query_index >> has_hit >>
              gpu_has_hit >> all_tiles_resident >>
              point_id >> depth >>
              x >> y >> z >> value >>
              result.issue_cpu_ms >> result.collect_cpu_ms >>
              resident_tile_ids)) {
            throw std::runtime_error(
                "failed to parse pick result line: " + line
            );
        }
        result.has_hit = has_hit != 0;
        result.all_tiles_resident = all_tiles_resident != 0;
        result.depth = depth;
        result.resident_tile_ids = parse_tile_id_list(resident_tile_ids);
        (void)gpu_has_hit;
        (void)point_id;
        if (result.has_hit) {
            result.point = gs3d::core::PointRecord{x, y, z, value, 0};
        }
        results.push_back(result);
    }

    std::sort(
        results.begin(),
        results.end(),
        [](const ObservedPickResult& a, const ObservedPickResult& b) {
            return a.query_index < b.query_index;
        }
    );
    return results;
}

[[nodiscard]]
std::filesystem::path make_pick_temp_path(
    const std::string& scenario,
    const std::string& mode,
    std::string_view suffix
)
{
    return std::filesystem::path("bench") / ".pick_tmp" /
           (scenario + "_" + mode + std::string(suffix));
}

void append_query_from_point(
    std::vector<PickQuery>& queries,
    const gs3d::core::PointRecord& point,
    const gs3d::camera::Camera& camera,
    const gs3d::camera::Viewport& viewport,
    float max_screen_distance_px
)
{
    const auto screen = gs3d::camera::MouseRay::to_screen(
        {point.x, point.y, point.z},
        viewport,
        camera
    );
    if (!screen) {
        return;
    }
    queries.push_back({
        screen->x,
        screen->y,
        max_screen_distance_px
    });
}

[[nodiscard]]
SyntheticScenario make_top_down_scenario()
{
    SyntheticScenario scenario;
    scenario.name = "synthetic_top_down";
    scenario.viewport = {800, 600};
    scenario.camera.set_viewport(
        scenario.viewport.width,
        scenario.viewport.height
    );
    scenario.camera.set_perspective(60.0f, 1.0f, 1000.0f);
    scenario.camera.look_at(
        {0.0f, 0.0f, 100.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}
    );

    const std::vector<gs3d::core::PointRecord> full = {
        {0.0f, 0.0f, 0.0f, 1.0f},
        {18.0f, 0.0f, 0.0f, 2.0f},
        {-18.0f, 0.0f, 0.0f, 3.0f},
        {0.0f, 18.0f, 0.0f, 4.0f},
        {0.0f, -18.0f, 0.0f, 5.0f},
        {70.0f, 0.0f, 0.0f, 6.0f},
        {-70.0f, 0.0f, 0.0f, 7.0f},
        {0.0f, 52.0f, 0.0f, 8.0f},
        {0.0f, -52.0f, 0.0f, 9.0f},
        {12.0f, 12.0f, 8.0f, 10.0f},
        {12.0f, 12.0f, -8.0f, 11.0f},
    };
    const std::vector<gs3d::core::PointRecord> lod = {
        full[0],
        full[2],
        full[4],
        full[6],
        full[8],
        full[9],
    };

    scenario.full_points.reserve(full.size());
    for (const auto& point : full) {
        scenario.full_points.push_back(point);
    }
    scenario.lod_points.reserve(lod.size());
    for (const auto& point : lod) {
        scenario.lod_points.push_back(point);
    }

    append_query_from_point(
        scenario.queries,
        full[0],
        scenario.camera,
        scenario.viewport,
        12.0f
    );
    append_query_from_point(
        scenario.queries,
        full[1],
        scenario.camera,
        scenario.viewport,
        12.0f
    );
    append_query_from_point(
        scenario.queries,
        full[5],
        scenario.camera,
        scenario.viewport,
        12.0f
    );
    append_query_from_point(
        scenario.queries,
        full[7],
        scenario.camera,
        scenario.viewport,
        12.0f
    );
    scenario.queries.push_back({30.0f, 30.0f, 6.0f});
    scenario.queries.push_back({820.0f, 620.0f, 6.0f});
    return scenario;
}

[[nodiscard]]
SyntheticScenario make_grazing_scenario()
{
    SyntheticScenario scenario;
    scenario.name = "synthetic_grazing";
    scenario.viewport = {800, 600};
    scenario.camera.set_viewport(
        scenario.viewport.width,
        scenario.viewport.height
    );
    scenario.camera.set_perspective(55.0f, 1.0f, 2000.0f);
    scenario.camera.look_at(
        {0.0f, -220.0f, 30.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    const std::vector<gs3d::core::PointRecord> full = {
        {-40.0f, 0.0f, 0.0f, 21.0f},
        {-10.0f, 0.0f, 0.0f, 22.0f},
        {15.0f, 0.0f, 0.0f, 23.0f},
        {45.0f, 0.0f, 0.0f, 24.0f},
        {0.0f, 70.0f, 3.0f, 25.0f},
        {0.0f, 95.0f, 5.0f, 26.0f},
    };
    const std::vector<gs3d::core::PointRecord> lod = {
        full[0],
        full[2],
        full[4],
    };

    scenario.full_points.reserve(full.size());
    for (const auto& point : full) {
        scenario.full_points.push_back(point);
    }
    scenario.lod_points.reserve(lod.size());
    for (const auto& point : lod) {
        scenario.lod_points.push_back(point);
    }

    append_query_from_point(
        scenario.queries,
        full[1],
        scenario.camera,
        scenario.viewport,
        14.0f
    );
    append_query_from_point(
        scenario.queries,
        full[3],
        scenario.camera,
        scenario.viewport,
        14.0f
    );
    append_query_from_point(
        scenario.queries,
        full[5],
        scenario.camera,
        scenario.viewport,
        14.0f
    );
    scenario.queries.push_back({-20.0f, 400.0f, 10.0f});
    scenario.queries.push_back({810.0f, 100.0f, 10.0f});
    return scenario;
}

[[nodiscard]]
std::uint64_t total_points_in_views(
    const std::vector<gs3d::core::PointDataView>& views
)
{
    std::uint64_t total = 0;
    for (const auto& view : views) {
        total += view.point_count;
    }
    return total;
}

[[nodiscard]]
CandidateAssembly make_hover_candidate_sets(
    bool lod_enabled,
    const gs3d::core::PointDataView& full_view,
    const std::optional<gs3d::core::PointDataView>& lod_view,
    const std::vector<std::shared_ptr<std::vector<gs3d::data::Gs3dPoint>>>& tile_points,
    std::uint64_t total_tile_points
)
{
    CandidateAssembly assembly;
    if (lod_enabled && lod_view.has_value()) {
        assembly.views.push_back(*lod_view);
    } else if (!lod_enabled && full_view.valid() && !full_view.empty()) {
        assembly.views.push_back(full_view);
    }

    assembly.point_count = total_points_in_views(assembly.views);
    if (!tile_points.empty()) {
        assembly.budget_exceeded = total_tile_points > kHoverTileCandidateBudget;
        if (total_tile_points <= kHoverTileCandidateBudget) {
            for (const auto& tile : tile_points) {
                assembly.views.push_back(
                    gs3d::data::make_point_data_view(*tile)
                );
                assembly.point_count +=
                    static_cast<std::uint64_t>(tile->size());
            }
            assembly.used_tile_points = true;
        }
    }

    return assembly;
}

[[nodiscard]]
std::vector<std::uint64_t> parse_tile_id_list(std::string_view encoded)
{
    if (encoded.empty() || encoded == "-") {
        return {};
    }

    std::vector<std::uint64_t> ids;
    std::string token;
    std::istringstream iss{std::string(encoded)};
    while (std::getline(iss, token, ',')) {
        if (token.empty()) {
            continue;
        }
        ids.push_back(static_cast<std::uint64_t>(std::stoull(token)));
    }
    return ids;
}

[[nodiscard]]
std::vector<gs3d::core::PointDataView> make_reference_sets_for_observed(
    const std::vector<gs3d::core::PointDataView>& default_reference_sets,
    const CandidateAssembly& candidate_sets,
    const ObservedPickResult& observed
)
{
    if (candidate_sets.tile_views.empty() ||
        observed.all_tiles_resident ||
        candidate_sets.base_view_count > default_reference_sets.size()) {
        return default_reference_sets;
    }

    std::vector<gs3d::core::PointDataView> reference_sets;
    reference_sets.reserve(
        candidate_sets.base_view_count + observed.resident_tile_ids.size()
    );
    for (std::size_t i = 0; i < candidate_sets.base_view_count; ++i) {
        reference_sets.push_back(default_reference_sets[i]);
    }
    for (const auto resident_tile_id : observed.resident_tile_ids) {
        const auto found = std::find_if(
            candidate_sets.tile_views.begin(),
            candidate_sets.tile_views.end(),
            [resident_tile_id](const auto& entry) {
                return entry.first == resident_tile_id;
            }
        );
        if (found != candidate_sets.tile_views.end()) {
            reference_sets.push_back(found->second);
        }
    }
    return reference_sets;
}

[[nodiscard]]
PickBatchSummary run_pick_batch(
    const std::string& scenario,
    const std::string& mode,
    const std::vector<PickQuery>& queries,
    const std::vector<gs3d::core::PointDataView>& reference_sets,
    const CandidateAssembly& candidate_sets,
    const gs3d::camera::Camera& camera,
    const gs3d::camera::Viewport& viewport,
    const gs3d::app::ViewerAppConfig& viewer_config
)
{
    const auto script_path =
        make_pick_temp_path(scenario, mode, "_queries.txt");
    const auto result_path =
        make_pick_temp_path(scenario, mode, "_results.txt");
    write_pick_script(queries, script_path);

    auto run_config = viewer_config;
    run_config.benchmark_mode = true;
    run_config.benchmark_frame_count =
        static_cast<std::uint32_t>(queries.size() + 8);
    run_config.benchmark_present_mode = "immediate";
    run_config.benchmark_pick_script_path = script_path;
    run_config.benchmark_pick_result_path = result_path;
    run_config.viewport_count = 1;
    run_config.window_width = viewport.width;
    run_config.window_height = viewport.height;
    run_config.ui_layout_ini_path.clear();
    run_config.camera_mode = "fixed";
    run_config.camera_position = {
        camera.position().x,
        camera.position().y,
        camera.position().z
    };
    run_config.camera_target = {
        camera.target().x,
        camera.target().y,
        camera.target().z
    };
    run_config.camera_up = {
        camera.up().x,
        camera.up().y,
        camera.up().z
    };
    run_config.lod_verbose = false;
    run_config.tile_verbose = false;
    run_config.window_title =
        "GeoScatter3D Pick Benchmark - " + scenario + " - " + mode;

    gs3d::app::ViewerApp app(run_config);
    if (app.run() != 0) {
        throw std::runtime_error(
            "ViewerApp pick benchmark run failed for " +
            scenario + " / " + mode
        );
    }
    const auto observed_results = read_pick_results(result_path);
    if (observed_results.size() != queries.size()) {
        throw std::runtime_error(
            "pick result count mismatch for " + scenario + " / " + mode
        );
    }

    std::vector<double> deltas;
    std::vector<double> latencies;
    deltas.reserve(queries.size());
    latencies.reserve(queries.size());

    std::size_t hits = 0;
    for (std::size_t i = 0; i < queries.size(); ++i) {
        const auto& query = queries[i];
        const auto& observed = observed_results[i];
        const auto active_reference_sets = make_reference_sets_for_observed(
            reference_sets,
            candidate_sets,
            observed
        );
        const auto expected = brute_force_front_most_pick(
            active_reference_sets,
            query.mouse_x,
            query.mouse_y,
            viewport,
            camera
        );
        latencies.push_back(
            observed.issue_cpu_ms + observed.collect_cpu_ms
        );

        const bool hit =
            (!expected.has_value() && !observed.has_hit) ||
            (expected.has_value() &&
             matches_front_most_contract(
                 *expected,
                 observed,
                 query,
                 viewport,
                 camera
             ));
        if (hit) {
            ++hits;
            continue;
        }

        if (expected && observed.point) {
            const auto actual_screen = gs3d::camera::MouseRay::to_screen(
                {observed.point->x, observed.point->y, observed.point->z},
                viewport,
                camera
            );
            if (!actual_screen) {
                deltas.push_back(expected->screen_distance_px);
                continue;
            }
            const float dx = actual_screen->x - query.mouse_x;
            const float dy = actual_screen->y - query.mouse_y;
            deltas.push_back(
                std::abs(
                    static_cast<double>(std::sqrt(dx * dx + dy * dy)) -
                    static_cast<double>(expected->screen_distance_px)
                )
            );
        } else if (expected && !observed.point) {
            deltas.push_back(expected->screen_distance_px);
        } else if (!expected && observed.point) {
            const auto actual_screen = gs3d::camera::MouseRay::to_screen(
                {observed.point->x, observed.point->y, observed.point->z},
                viewport,
                camera
            );
            if (actual_screen) {
                const float dx = actual_screen->x - query.mouse_x;
                const float dy = actual_screen->y - query.mouse_y;
                deltas.push_back(std::sqrt(dx * dx + dy * dy));
            }
        }
    }

    PickBatchSummary summary;
    summary.scenario = scenario;
    summary.mode = mode;
    summary.query_count = queries.size();
    summary.hit_count = hits;
    summary.candidate_point_count = candidate_sets.point_count;
    summary.candidate_uses_tiles = candidate_sets.used_tile_points;
    summary.budget_exceeded = candidate_sets.budget_exceeded;
    summary.delta_px = make_percentiles(deltas);
    summary.latency_ms = make_percentiles(latencies);
    return summary;
}

[[nodiscard]]
std::vector<PickQuery> collect_queries_from_tiles(
    const std::vector<std::shared_ptr<std::vector<gs3d::data::Gs3dPoint>>>& tiles,
    const gs3d::camera::Camera& camera,
    const gs3d::camera::Viewport& viewport,
    std::size_t desired_count
)
{
    std::vector<PickQuery> queries;
    queries.reserve(desired_count);
    for (const auto& tile : tiles) {
        if (!tile || tile->empty()) {
            continue;
        }

        const std::size_t stride =
            std::max<std::size_t>(1, tile->size() / 16);
        for (std::size_t i = 0; i < tile->size() &&
                                queries.size() < desired_count;
             i += stride) {
            const auto& point = (*tile)[i];
            const auto screen = gs3d::camera::MouseRay::to_screen(
                {point.x, point.y, point.z},
                viewport,
                camera
            );
            if (!screen) {
                continue;
            }
            if (screen->x < 20.0f ||
                screen->x > static_cast<float>(viewport.width) - 20.0f ||
                screen->y < 20.0f ||
                screen->y > static_cast<float>(viewport.height) - 20.0f) {
                continue;
            }
            queries.push_back({screen->x, screen->y, 12.0f});
        }
        if (queries.size() >= desired_count) {
            break;
        }
    }
    return queries;
}

[[nodiscard]]
std::shared_ptr<std::vector<gs3d::data::Gs3dPoint>> load_tile_points(
    const gs3d::data::Gs3dTileReader& reader,
    std::uint64_t tile_id
)
{
    return std::make_shared<std::vector<gs3d::data::Gs3dPoint>>(
        reader.read_tile_points(tile_id)
    );
}

[[nodiscard]]
BoundarySelectionCase find_boundary_case(
    const std::string& name,
    bool want_above_budget,
    const gs3d::app::ViewerAppConfig& config,
    const gs3d::data::Gs3dTileReader& tile_reader,
    const gs3d::core::TileIndexView& tile_index_view
)
{
    BoundarySelectionCase best;
    best.name = name;
    best.viewport = {config.window_width, config.window_height};
    best.camera = make_camera_from_config(config);

    gs3d::render::TileSelection tile_selection;
    gs3d::render::TileSelectionConfig selection_config;
    selection_config.min_tile_pixel_size = config.tile_min_pixel_size;
    selection_config.max_visible_tiles = config.tile_max_visible_tiles;
    selection_config.use_full_z_range = config.tile_use_full_z_range;
    tile_selection.set_config(selection_config);

    double best_gap = std::numeric_limits<double>::max();
    auto camera = best.camera;
    for (int step = 0; step < 900; ++step) {
        const auto selection = tile_selection.update(camera, tile_index_view);
        if (selection.enabled && !selection.tile_ids.empty()) {
            std::uint64_t total_points = 0;
            for (const auto tile_id : selection.tile_ids) {
                total_points += tile_reader.record(tile_id).point_count;
            }
            const bool matches_side =
                want_above_budget
                    ? total_points > kHoverTileCandidateBudget
                    : total_points <= kHoverTileCandidateBudget;
            if (matches_side) {
                const double gap = std::abs(
                    static_cast<double>(total_points) -
                    static_cast<double>(kHoverTileCandidateBudget)
                );
                if (gap < best_gap) {
                    best_gap = gap;
                    best.camera = camera;
                    best.tile_ids = selection.tile_ids;
                    best.total_tile_points = total_points;
                }
            }
        }

        camera.orbit(0.01f, 0.0f);
        camera.zoom(0.9985f);
    }

    if (best.tile_ids.empty()) {
        struct TileWithDistance {
            std::uint64_t tile_id = 0;
            std::uint64_t point_count = 0;
            float center_x = 0.0f;
            float center_y = 0.0f;
            float distance_sq = 0.0f;
        };

        const auto& records = tile_reader.records();
        if (records.empty()) {
            throw std::runtime_error("tile reader has no records");
        }

        float sum_center_x = 0.0f;
        float sum_center_y = 0.0f;
        for (const auto& record : records) {
            sum_center_x += 0.5f * (record.bbox_min_x + record.bbox_max_x);
            sum_center_y += 0.5f * (record.bbox_min_y + record.bbox_max_y);
        }
        const float data_center_x =
            sum_center_x / static_cast<float>(records.size());
        const float data_center_y =
            sum_center_y / static_cast<float>(records.size());

        std::vector<TileWithDistance> sorted_tiles;
        sorted_tiles.reserve(records.size());
        for (const auto& record : records) {
            const float center_x =
                0.5f * (record.bbox_min_x + record.bbox_max_x);
            const float center_y =
                0.5f * (record.bbox_min_y + record.bbox_max_y);
            const float dx = center_x - data_center_x;
            const float dy = center_y - data_center_y;
            sorted_tiles.push_back({
                record.tile_id,
                record.point_count,
                center_x,
                center_y,
                dx * dx + dy * dy
            });
        }
        std::sort(
            sorted_tiles.begin(),
            sorted_tiles.end(),
            [](const TileWithDistance& a, const TileWithDistance& b) {
                return a.distance_sq < b.distance_sq;
            }
        );

        std::uint64_t total_points = 0;
        std::vector<std::uint64_t> tile_ids;
        tile_ids.reserve(sorted_tiles.size());
        for (const auto& tile : sorted_tiles) {
            if (!want_above_budget &&
                total_points + tile.point_count > kHoverTileCandidateBudget &&
                !tile_ids.empty()) {
                break;
            }
            tile_ids.push_back(tile.tile_id);
            total_points += tile.point_count;
            if (want_above_budget &&
                total_points > kHoverTileCandidateBudget) {
                break;
            }
        }

        if (tile_ids.empty()) {
            throw std::runtime_error(
                "failed to synthesize tile budget boundary case: " + name
            );
        }

        float min_x = std::numeric_limits<float>::max();
        float min_y = std::numeric_limits<float>::max();
        float min_z = std::numeric_limits<float>::max();
        float max_x = -std::numeric_limits<float>::max();
        float max_y = -std::numeric_limits<float>::max();
        float max_z = -std::numeric_limits<float>::max();
        for (const auto tile_id : tile_ids) {
            const auto& record = tile_reader.record(tile_id);
            min_x = std::min(min_x, record.bbox_min_x);
            min_y = std::min(min_y, record.bbox_min_y);
            min_z = std::min(min_z, record.bbox_min_z);
            max_x = std::max(max_x, record.bbox_max_x);
            max_y = std::max(max_y, record.bbox_max_y);
            max_z = std::max(max_z, record.bbox_max_z);
        }

        best.tile_ids = std::move(tile_ids);
        best.total_tile_points = total_points;
        best.camera = make_camera_from_config(config);
        best.camera.fit_bounds({
            {min_x, min_y, min_z},
            {max_x, max_y, max_z}
        });
    }

    best.tiles.reserve(best.tile_ids.size());
    for (const auto tile_id : best.tile_ids) {
        best.tiles.push_back(load_tile_points(tile_reader, tile_id));
    }
    best.queries = collect_queries_from_tiles(
        best.tiles,
        best.camera,
        best.viewport,
        16
    );
    if (best.queries.empty()) {
        throw std::runtime_error(
            "failed to collect projected queries for boundary case: " + name
        );
    }

    return best;
}

[[nodiscard]]
std::vector<PickQuery> collect_queries_from_view(
    const gs3d::core::PointDataView& view,
    const gs3d::camera::Camera& camera,
    const gs3d::camera::Viewport& viewport,
    std::size_t desired_count
)
{
    std::vector<PickQuery> queries;
    queries.reserve(desired_count);
    if (!view.valid() || view.empty()) {
        return queries;
    }

    std::uint64_t stride =
        std::max<std::uint64_t>(1, view.point_count / (desired_count * 24));
    for (std::uint64_t i = 0;
         i < view.point_count && queries.size() < desired_count;
         i += stride) {
        const auto point = view.point_at(i);
        const auto screen = gs3d::camera::MouseRay::to_screen(
            {point.x, point.y, point.z},
            viewport,
            camera
        );
        if (!screen) {
            continue;
        }
        if (screen->x < 20.0f ||
            screen->x > static_cast<float>(viewport.width) - 20.0f ||
            screen->y < 20.0f ||
            screen->y > static_cast<float>(viewport.height) - 20.0f) {
            continue;
        }
        queries.push_back({screen->x, screen->y, 12.0f});
    }
    return queries;
}

[[nodiscard]]
OrderSensitivitySummary run_depth_tie_order_sensitivity()
{
    OrderSensitivitySummary summary;
    summary.scenario = "depth_tie_order";

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 1.0f, 1000.0f);
    camera.look_at(
        {0.0f, 0.0f, 100.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}
    );
    const gs3d::camera::Viewport viewport{800, 600};

    gs3d::core::PointBuffer first_then_second;
    first_then_second.push_back({0.0f, 0.0f, 10.0f, 101.0f});
    first_then_second.push_back({0.0f, 0.0f, -10.0f, 102.0f});

    gs3d::core::PointBuffer second_then_first;
    second_then_first.push_back({0.0f, 0.0f, -10.0f, 102.0f});
    second_then_first.push_back({0.0f, 0.0f, 10.0f, 101.0f});

    std::vector<double> latencies;
    for (int i = 0; i < 8; ++i) {
        const auto screen = gs3d::camera::MouseRay::to_screen(
            {0.0f, 0.0f, 10.0f},
            viewport,
            camera
        );
        if (!screen) {
            continue;
        }

        gs3d::util::Stopwatch timer;
        const auto a = brute_force_front_most_pick(
            {first_then_second.view()},
            screen->x,
            screen->y,
            viewport,
            camera
        );
        const auto b = brute_force_front_most_pick(
            {second_then_first.view()},
            screen->x,
            screen->y,
            viewport,
            camera
        );
        latencies.push_back(timer.elapsed_milliseconds());
        ++summary.query_count;
        if (a && b && !same_point_record(a->point, b->point)) {
            ++summary.flip_count;
        }
    }

    summary.latency_ms = make_percentiles(latencies);
    return summary;
}

void print_pick_summary(
    const std::vector<PickBatchSummary>& batches,
    const OrderSensitivitySummary& order_sensitivity,
    const std::filesystem::path& output_path
)
{
    std::cout << "[PICK] baseline_path = " << output_path.string() << '\n';
    std::cout << "[PICK] scenario | mode | hit_rate | delta_px_p95 | latency_ms_p95 | candidate_points | tiles | budget\n";
    for (const auto& batch : batches) {
        const double hit_rate = batch.query_count > 0
            ? 100.0 * static_cast<double>(batch.hit_count) /
                static_cast<double>(batch.query_count)
            : 0.0;
        std::cout << "[PICK] "
                  << batch.scenario << " | "
                  << batch.mode << " | "
                  << hit_rate << "% | "
                  << (batch.delta_px.has_samples ? batch.delta_px.p95 : 0.0) << " | "
                  << (batch.latency_ms.has_samples ? batch.latency_ms.p95 : 0.0) << " | "
                  << batch.candidate_point_count << " | "
                  << (batch.candidate_uses_tiles ? "tiles+lod/full" : "lod/full") << " | "
                  << (batch.budget_exceeded ? "over" : "within")
                  << '\n';
    }
    const double flip_rate = order_sensitivity.query_count > 0
        ? 100.0 * static_cast<double>(order_sensitivity.flip_count) /
            static_cast<double>(order_sensitivity.query_count)
        : 0.0;
    std::cout << "[PICK] depth_tie_order | flip_rate = "
              << flip_rate
              << "% | latency_ms_p95 = "
              << (order_sensitivity.latency_ms.has_samples
                      ? order_sensitivity.latency_ms.p95
                      : 0.0)
              << '\n';
}

void write_pick_json(
    const std::vector<PickBatchSummary>& batches,
    const OrderSensitivitySummary& order_sensitivity,
    const std::filesystem::path& output_path
)
{
    std::error_code ec;
    std::filesystem::create_directories(output_path.parent_path(), ec);
    std::ofstream out(output_path, std::ios::trunc);
    if (!out) {
        throw std::runtime_error(
            "failed to open pick baseline output: " + output_path.string()
        );
    }

    out << "{\n";
    out << "  \"pick_contract\": "
        << "\"front_most_hit_pixel_within_11x11_neighborhood\",\n";
    out << "  \"approximation_source\": "
        << "\"production GPU pick over rendered points; reference scans rendered candidates with the same 11x11 front-most rule\",\n";
    out << "  \"batches\": [\n";
    for (std::size_t i = 0; i < batches.size(); ++i) {
        const auto& batch = batches[i];
        const double hit_rate = batch.query_count > 0
            ? static_cast<double>(batch.hit_count) /
                static_cast<double>(batch.query_count)
            : 0.0;
        out << "    {\n";
        out << "      \"scenario\": \"" << json_escape(batch.scenario) << "\",\n";
        out << "      \"mode\": \"" << batch.mode << "\",\n";
        out << "      \"query_count\": " << batch.query_count << ",\n";
        out << "      \"hit_count\": " << batch.hit_count << ",\n";
        out << "      \"hit_rate\": " << hit_rate << ",\n";
        out << "      \"candidate_point_count\": " << batch.candidate_point_count << ",\n";
        out << "      \"candidate_uses_tiles\": "
            << (batch.candidate_uses_tiles ? "true" : "false") << ",\n";
        out << "      \"budget_exceeded\": "
            << (batch.budget_exceeded ? "true" : "false") << ",\n";
        out << "      \"delta_px\": {\n";
        out << "        \"p50\": " << batch.delta_px.p50 << ",\n";
        out << "        \"p95\": " << batch.delta_px.p95 << ",\n";
        out << "        \"p99\": " << batch.delta_px.p99 << "\n";
        out << "      },\n";
        out << "      \"latency_ms\": {\n";
        out << "        \"p50\": " << batch.latency_ms.p50 << ",\n";
        out << "        \"p95\": " << batch.latency_ms.p95 << ",\n";
        out << "        \"p99\": " << batch.latency_ms.p99 << "\n";
        out << "      }\n";
        out << "    }";
        out << (i + 1 < batches.size() ? ",\n" : "\n");
    }
    const double flip_rate = order_sensitivity.query_count > 0
        ? static_cast<double>(order_sensitivity.flip_count) /
            static_cast<double>(order_sensitivity.query_count)
        : 0.0;
    out << "  ],\n";
    out << "  \"order_sensitivity\": {\n";
    out << "    \"scenario\": \"" << order_sensitivity.scenario << "\",\n";
    out << "    \"query_count\": " << order_sensitivity.query_count << ",\n";
    out << "    \"flip_count\": " << order_sensitivity.flip_count << ",\n";
    out << "    \"flip_rate\": " << flip_rate << ",\n";
    out << "    \"latency_ms\": {\n";
    out << "      \"p50\": " << order_sensitivity.latency_ms.p50 << ",\n";
    out << "      \"p95\": " << order_sensitivity.latency_ms.p95 << ",\n";
    out << "      \"p99\": " << order_sensitivity.latency_ms.p99 << "\n";
    out << "    }\n";
    out << "  }\n";
    out << "}\n";
}

int run_pick_benchmark(int argc, char** argv)
{
    auto app_config = gs3d::app::AppConfigLoader::load_from_args(argc, argv);
    auto dataset = gs3d::data::Gs3dDatasetLoader::load(
        app_config.viewer.gs3d_path
    );
    if (!dataset.is_consistent() || dataset.empty()) {
        std::cerr << "[FAIL] dataset unavailable for pick benchmark\n";
        return 1;
    }

    const auto lod_dataset = ensure_lod_dataset(dataset, app_config);
    const auto lod_view = lod_dataset.level_count() > 0
        ? std::optional<gs3d::core::PointDataView>(
              gs3d::data::make_point_data_view(lod_dataset.level(0).points)
          )
        : std::nullopt;

    std::vector<PickBatchSummary> batches;
    batches.reserve(8);

    auto tile_reader = ensure_tile_reader(dataset, app_config);
    auto tile_index_view = gs3d::data::make_tile_index_view(tile_reader);
    const auto below_case = find_boundary_case(
        "tile_budget_below",
        false,
        app_config.viewer,
        tile_reader,
        tile_index_view
    );
    const auto above_case = find_boundary_case(
        "tile_budget_above",
        true,
        app_config.viewer,
        tile_reader,
        tile_index_view
    );

    const auto full_view = gs3d::data::make_point_data_view(dataset);
    auto viewer_lod_off = app_config.viewer;
    viewer_lod_off.lod_enabled = false;
    viewer_lod_off.tile_enabled = false;

    auto viewer_lod_on = app_config.viewer;
    viewer_lod_on.lod_enabled = lod_view.has_value();
    viewer_lod_on.lod_keep_full_buffer = true;
    viewer_lod_on.tile_enabled = true;

    const auto run_boundary = [&](const BoundarySelectionCase& selection_case) {
        std::vector<gs3d::core::PointDataView> lod_on_reference_sets;
        if (lod_view.has_value()) {
            lod_on_reference_sets.push_back(*lod_view);
        }
        lod_on_reference_sets.reserve(
            lod_on_reference_sets.size() + selection_case.tiles.size()
        );
        for (const auto& tile : selection_case.tiles) {
            lod_on_reference_sets.push_back(
                gs3d::data::make_point_data_view(*tile)
            );
        }

        batches.push_back(
            run_pick_batch(
                selection_case.name,
                "lod_off",
                selection_case.queries,
                {full_view},
                CandidateAssembly{
                    {full_view},
                    dataset.point_count(),
                    false,
                    false,
                    1,
                    {}
                },
                selection_case.camera,
                selection_case.viewport,
                viewer_lod_off
            )
        );
        if (lod_view.has_value()) {
            CandidateAssembly rendered_sets;
            rendered_sets.views = lod_on_reference_sets;
            rendered_sets.point_count =
                lod_view->point_count + selection_case.total_tile_points;
            rendered_sets.used_tile_points = !selection_case.tiles.empty();
            rendered_sets.budget_exceeded =
                selection_case.total_tile_points > kHoverTileCandidateBudget;
            rendered_sets.base_view_count = 1;
            rendered_sets.tile_views.reserve(selection_case.tiles.size());
            for (std::size_t i = 0; i < selection_case.tiles.size(); ++i) {
                rendered_sets.tile_views.push_back({
                    selection_case.tile_ids[i],
                    gs3d::data::make_point_data_view(*selection_case.tiles[i])
                });
            }
            batches.push_back(
                run_pick_batch(
                    selection_case.name,
                    "lod_on",
                    selection_case.queries,
                    lod_on_reference_sets,
                    rendered_sets,
                    selection_case.camera,
                    selection_case.viewport,
                    viewer_lod_on
                )
            );
        }
    };
    run_boundary(below_case);
    run_boundary(above_case);

    auto large_scene_camera = make_camera_from_config(app_config.viewer);
    const gs3d::camera::Viewport large_scene_viewport{
        app_config.viewer.window_width,
        app_config.viewer.window_height
    };
    const auto large_scene_queries = collect_queries_from_view(
        full_view,
        large_scene_camera,
        large_scene_viewport,
        64
    );
    if (large_scene_queries.empty()) {
        throw std::runtime_error(
            "failed to collect real-scene latency queries"
        );
    }
    batches.push_back(
        run_pick_batch(
            "large_scene_latency",
            "lod_off",
            large_scene_queries,
            {full_view},
            CandidateAssembly{
                {full_view},
                dataset.point_count(),
                false,
                false,
                1,
                {}
            },
            large_scene_camera,
            large_scene_viewport,
            viewer_lod_off
        )
    );
    if (lod_view.has_value()) {
        auto viewer_large_scene_lod_on = viewer_lod_on;
        viewer_large_scene_lod_on.tile_enabled = false;
        batches.push_back(
            run_pick_batch(
                "large_scene_latency",
                "lod_on",
                large_scene_queries,
                {*lod_view},
                CandidateAssembly{
                    {*lod_view},
                    lod_view->point_count,
                    false,
                    false,
                    1,
                    {}
                },
                large_scene_camera,
                large_scene_viewport,
                viewer_large_scene_lod_on
            )
        );
    }

    const auto order_sensitivity = run_depth_tie_order_sensitivity();
    const std::filesystem::path output_path =
        std::getenv("GS3D_PICK_OUTPUT_PATH")
            ? std::filesystem::path(std::getenv("GS3D_PICK_OUTPUT_PATH"))
            : std::filesystem::path("bench") /
                  ("pick_baseline_after_" + date_tag_yyyymmdd() + ".json");
    write_pick_json(batches, order_sensitivity, output_path);
    print_pick_summary(batches, order_sensitivity, output_path);
    return 0;
}

/*
 * Drives the real ViewerApp render loop in benchmark mode: synthetic
 * camera orbit instead of real input, fixed frame count instead of an
 * interactive window-close exit, frame-time/reload-latency percentiles
 * printed to stdout (see ViewerApp::run()'s "[BENCH] ..." lines).
 *
 * This is a Large/manual test (Vulkan device + GPU required) — it is not
 * part of `ctest`'s default suite, matching docs/spec's decision to keep
 * the fast ctest/dev loop unaffected by GPU-bound benchmarks.
 */
int run_benchmark(int argc, char** argv)
{
    if (const char* submode = std::getenv("GS3D_BENCHMARK_SUBMODE")) {
        if (std::string_view(submode) == "pick") {
            return run_pick_benchmark(argc, argv);
        }
    }

    // Reuses the same --config flag as the interactive app (see main.cpp),
    // so the benchmark exercises the real config/viewer.toml dataset/LOD/
    // tile settings rather than a separate hardcoded path.
    auto app_config =
        gs3d::app::AppConfigLoader::load_from_args(argc, argv);

    auto viewer_config = app_config.viewer;
    viewer_config.benchmark_mode = true;
    viewer_config.benchmark_frame_count = 600;

    // Override via env var (not argv — argv is already claimed by
    // AppConfigLoader's --config flag parsing). Useful when the default
    // "settle" window is too short to observe a full async tile reload.
    if (const char* frames_env = std::getenv("GS3D_BENCHMARK_FRAMES")) {
        const auto frames = std::strtoul(frames_env, nullptr, 10);
        if (frames > 0) {
            viewer_config.benchmark_frame_count =
                static_cast<std::uint32_t>(frames);
        }
    }

    if (const char* present_mode_env =
            std::getenv("GS3D_BENCHMARK_PRESENT_MODE")) {
        const std::string mode = present_mode_env;
        if (mode == "immediate") {
            viewer_config.benchmark_present_mode = "immediate";
        } else if (mode == "mailbox") {
            viewer_config.benchmark_present_mode = "mailbox";
        } else if (mode == "fifo") {
            viewer_config.benchmark_present_mode = "fifo";
        }
    }

    gs3d::app::ViewerApp app(viewer_config);
    return app.run();
}

} // namespace

int main(int argc, char** argv)
{
    test_percentile_of_empty_input_is_zero();
    test_percentile_of_single_element_ignores_p();
    test_percentile_p0_and_p100_are_min_and_max();
    test_percentile_p50_is_median_for_odd_count();
    test_percentile_interpolates_between_values();

    if (failures > 0) {
        std::cerr << "[FAIL] " << failures
                  << " PercentileStats assertion(s) failed.\n";
        return 1;
    }

    return run_benchmark(argc, argv);
}
