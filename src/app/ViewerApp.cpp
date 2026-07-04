#include "app/ViewerApp.hpp"

#include "app/AppState.hpp"
#include "app/TilePointCache.hpp"
#include "app/ViewportResizeScheduler.hpp"
#include "gui/ImGuiLayer.hpp"
#include "render/ViewportManager.hpp"
#include "imgui.h"

#include "camera/BoxSelect.hpp"
#include "camera/Camera.hpp"
#include "camera/CameraController.hpp"
#include "camera/CameraHub.hpp"
#include "camera/MouseRay.hpp"
#include "core/DatasetDescriptor.hpp"
#include "data/Gs3dDataset.hpp"
#include "data/Gs3dReader.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "data/Gs3dLodReader.hpp"
#include "data/Gs3dLodTargets.hpp"
#include "data/PointDataAdapters.hpp"
#include "data/TileDataAdapters.hpp"
#include "data/Gs3dTileReader.hpp"

#include "platform/Window.hpp"
#include "render/AxisGrid.hpp"
#include "render/LodSelector.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/PointCloudLodGpu.hpp"
#include "render/PointPipeline.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanRenderer.hpp"
#include "render/VulkanSwapchain.hpp"
#include "render/PointCloudTileGpu.hpp"
#include "render/TileSelection.hpp"
#include "render/VulkanBuffer.hpp"

#include "preprocess/Gs3dLodWriter.hpp"
#include "util/PercentileStats.hpp"
#include "util/Stopwatch.hpp"
#include "scene/SceneState.hpp"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace gs3d::app {

namespace {

gs3d::camera::Vec3 to_vec3(
    const std::array<float, 3>& value
) {
    return {
        value[0],
        value[1],
        value[2]
    };
}

gs3d::camera::CameraBounds make_camera_bounds(
    const gs3d::data::Gs3dDataset& dataset
) {
    gs3d::camera::CameraBounds bounds;

    bounds.min = {
        dataset.bbox_min_x(),
        dataset.bbox_min_y(),
        dataset.bbox_min_z()
    };

    bounds.max = {
        dataset.bbox_max_x(),
        dataset.bbox_max_y(),
        dataset.bbox_max_z()
    };

    return bounds;
}

gs3d::core::Bounds3f make_dataset_bounds(
    const gs3d::data::Gs3dDataset& dataset
) {
    return {
        dataset.bbox_min_x(),
        dataset.bbox_min_y(),
        dataset.bbox_min_z(),
        dataset.bbox_max_x(),
        dataset.bbox_max_y(),
        dataset.bbox_max_z()
    };
}

std::string format_bounds_label(
    const gs3d::core::Bounds3f& bounds
) {
    return
        "[" + std::to_string(bounds.min_x) + ", " +
        std::to_string(bounds.min_y) + ", " +
        std::to_string(bounds.min_z) + "] -> [" +
        std::to_string(bounds.max_x) + ", " +
        std::to_string(bounds.max_y) + ", " +
        std::to_string(bounds.max_z) + "]";
}

struct BenchmarkPickScriptQuery {
    std::size_t query_index = 0;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
};

struct BenchmarkPickObservedResult {
    std::size_t query_index = 0;
    bool has_hit = false;
    bool gpu_has_hit = false;
    bool all_tiles_resident = false;
    std::uint32_t point_id = 0;
    float depth = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float value = 0.0f;
    double issue_cpu_ms = 0.0;
    double collect_cpu_ms = 0.0;
    std::vector<std::uint64_t> resident_tile_ids{};
};

struct BenchmarkPickIssuedMetadata {
    bool all_tiles_resident = false;
    std::vector<std::uint64_t> resident_tile_ids{};
};

constexpr std::uint32_t kMaxGpuPickRadiusPx = 5;
constexpr std::uint32_t kDefaultGpuPickRadiusPx = 3;

[[nodiscard]]
std::uint32_t compute_hover_pick_radius_px(float point_size) noexcept {
    const float clamped_point_size =
        std::clamp(point_size, 1.0f, 10.0f);
    const int sprite_half_extent =
        static_cast<int>(std::ceil(clamped_point_size * 0.5f));
    return static_cast<std::uint32_t>(
        std::clamp(
            sprite_half_extent + 1,
            static_cast<int>(kDefaultGpuPickRadiusPx),
            static_cast<int>(kMaxGpuPickRadiusPx)
        )
    );
}

std::vector<BenchmarkPickScriptQuery> load_benchmark_pick_script(
    const std::filesystem::path& script_path
) {
    std::ifstream in(script_path);
    if (!in) {
        throw std::runtime_error(
            "ViewerApp: failed to open benchmark pick script: " +
            script_path.string()
        );
    }

    std::vector<BenchmarkPickScriptQuery> queries;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream iss(line);
        BenchmarkPickScriptQuery query;
        query.query_index = queries.size();
        if (!(iss >> query.mouse_x >> query.mouse_y)) {
            throw std::runtime_error(
                "ViewerApp: invalid benchmark pick script line " +
                std::to_string(line_number)
            );
        }
        queries.push_back(query);
    }

    if (queries.empty()) {
        throw std::runtime_error(
            "ViewerApp: benchmark pick script contains no queries"
        );
    }
    return queries;
}

void write_benchmark_pick_results(
    const std::filesystem::path& output_path,
    const std::vector<BenchmarkPickObservedResult>& results
) {
    std::error_code ec;
    std::filesystem::create_directories(output_path.parent_path(), ec);
    std::ofstream out(output_path, std::ios::trunc);
    if (!out) {
        throw std::runtime_error(
            "ViewerApp: failed to open benchmark pick result path: " +
            output_path.string()
        );
    }

    out << "query_index has_hit gpu_has_hit all_tiles_resident point_id depth x y z value issue_cpu_ms collect_cpu_ms resident_tile_ids\n";
    for (const auto& result : results) {
        std::ostringstream resident_tiles;
        if (result.resident_tile_ids.empty()) {
            resident_tiles << '-';
        } else {
            for (std::size_t i = 0; i < result.resident_tile_ids.size(); ++i) {
                if (i > 0) {
                    resident_tiles << ',';
                }
                resident_tiles << result.resident_tile_ids[i];
            }
        }
        out << result.query_index << ' '
            << (result.has_hit ? 1 : 0) << ' '
            << (result.gpu_has_hit ? 1 : 0) << ' '
            << (result.all_tiles_resident ? 1 : 0) << ' '
            << result.point_id << ' '
            << result.depth << ' '
            << result.x << ' '
            << result.y << ' '
            << result.z << ' '
            << result.value << ' '
            << result.issue_cpu_ms << ' '
            << result.collect_cpu_ms << ' '
            << resident_tiles.str() << '\n';
    }
}

gs3d::render::SwapchainPresentModeHint benchmark_present_mode_hint(
    const std::string& mode
) {
    if (mode == "immediate") {
        return gs3d::render::SwapchainPresentModeHint::Immediate;
    }
    if (mode == "mailbox") {
        return gs3d::render::SwapchainPresentModeHint::Mailbox;
    }
    if (mode == "fifo") {
        return gs3d::render::SwapchainPresentModeHint::Fifo;
    }
    return gs3d::render::SwapchainPresentModeHint::Auto;
}

gs3d::data::Gs3dLodVoxelMode parse_lod_voxel_mode(
    const std::string& mode
) {
    if (mode == "XY" || mode == "xy") {
        return gs3d::data::Gs3dLodVoxelMode::XY;
    }

    if (mode == "XYZ" || mode == "xyz") {
        return gs3d::data::Gs3dLodVoxelMode::XYZ;
    }

    throw std::runtime_error(
        "ViewerApp: unsupported LOD voxel_mode: " + mode
    );
}

gs3d::render::TileSelectionConfig make_tile_selection_config(
    const ViewerAppConfig& config
) {
    gs3d::render::TileSelectionConfig tile_config;

    tile_config.min_tile_pixel_size =
        config.tile_min_pixel_size;
    tile_config.max_visible_tiles =
        config.tile_max_visible_tiles;

    tile_config.use_full_z_range =
        config.tile_use_full_z_range;

    return tile_config;
}

gs3d::data::Gs3dLodDataset load_or_build_lod_dataset(
    const gs3d::data::Gs3dDataset& dataset,
    const ViewerAppConfig& config
) {
    if (!config.lod_enabled) {
        return {};
    }

    const auto& sidecar_path =
        config.lod_sidecar_path;

    if (!config.lod_auto_load_sidecar) {
        throw std::runtime_error(
            "ViewerApp: lod.enabled is true but runtime LOD build is "
            "disabled and lod.auto_load_sidecar is false"
        );
    }

    if (sidecar_path.empty()) {
        throw std::runtime_error(
            "ViewerApp: lod.enabled is true but lod.sidecar_path is empty"
        );
    }

    if (!std::filesystem::exists(sidecar_path)) {
        throw std::runtime_error(
            "ViewerApp: required LOD sidecar not found: " +
            sidecar_path.string()
        );
    }

    gs3d::data::Gs3dLodReadConfig read_config;
    read_config.validate_against_source = true;
    read_config.verbose = config.lod_verbose;

    const auto read_result =
        gs3d::data::Gs3dLodReader::read(
            sidecar_path,
            dataset.header(),
            read_config
        );

    std::cout << "[OK] LOD sidecar loaded.\n";
    std::cout << "path = "
              << sidecar_path.string()
              << '\n';

    return read_result.dataset;
}

[[nodiscard]]
std::vector<std::uint32_t> make_runtime_point_ids(
    std::uint64_t point_count
) {
    if (point_count >
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max() - 1)) {
        throw std::runtime_error(
            "ViewerApp: point_count exceeds runtime point_id range"
        );
    }

    std::vector<std::uint32_t> point_ids(
        static_cast<std::size_t>(point_count)
    );
    for (std::uint64_t i = 0; i < point_count; ++i) {
        point_ids[static_cast<std::size_t>(i)] =
            static_cast<std::uint32_t>(i + 1);
    }
    return point_ids;
}

[[nodiscard]]
bool same_point_exact(
    const gs3d::data::Gs3dPoint& lhs,
    const gs3d::data::Gs3dPoint& rhs
) noexcept {
    return lhs.x == rhs.x &&
           lhs.y == rhs.y &&
           lhs.z == rhs.z &&
           lhs.value == rhs.value;
}

[[nodiscard]]
std::vector<std::uint32_t> map_subsequence_point_ids(
    const std::vector<gs3d::data::Gs3dPoint>& source_points,
    const std::vector<std::uint32_t>& source_point_ids,
    const std::vector<gs3d::data::Gs3dPoint>& subset_points,
    const char* label
) {
    if (source_points.size() != source_point_ids.size()) {
        throw std::runtime_error(
            "ViewerApp: source point/id array size mismatch"
        );
    }

    std::vector<std::uint32_t> subset_ids;
    subset_ids.reserve(subset_points.size());

    std::size_t source_index = 0;
    for (const auto& point : subset_points) {
        while (source_index < source_points.size() &&
               !same_point_exact(source_points[source_index], point)) {
            ++source_index;
        }

        if (source_index >= source_points.size()) {
            throw std::runtime_error(
                std::string("ViewerApp: failed to map runtime point_id for ") +
                label
            );
        }

        subset_ids.push_back(source_point_ids[source_index]);
        ++source_index;
    }

    return subset_ids;
}

[[nodiscard]]
std::uint32_t point_tile_coord_runtime(
    float value,
    float origin,
    float tile_size,
    std::uint32_t grid_count
) {
    if (grid_count == 0 || tile_size <= 0.0f) {
        throw std::runtime_error(
            "ViewerApp: invalid tile grid configuration for runtime ids"
        );
    }

    const auto raw = static_cast<std::int64_t>(
        std::floor((value - origin) / tile_size)
    );
    if (raw < 0) {
        return 0;
    }

    const auto upper =
        static_cast<std::int64_t>(grid_count - 1);
    if (raw > upper) {
        return grid_count - 1;
    }

    return static_cast<std::uint32_t>(raw);
}

[[nodiscard]]
std::unordered_map<std::uint64_t, std::vector<std::uint32_t>>
build_runtime_tile_point_ids(
    const gs3d::data::Gs3dDataset& dataset,
    const gs3d::data::Gs3dTileReader& tile_reader,
    const std::vector<std::uint32_t>& source_point_ids
) {
    if (dataset.points().size() != source_point_ids.size()) {
        throw std::runtime_error(
            "ViewerApp: source point/id array size mismatch for tiles"
        );
    }

    const auto& header = tile_reader.index_header();
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> tile_ids;
    std::unordered_map<std::uint64_t, std::uint64_t> grid_to_tile_id;
    tile_ids.reserve(tile_reader.records().size());
    grid_to_tile_id.reserve(tile_reader.records().size());
    for (const auto& record : tile_reader.records()) {
        tile_ids.emplace(record.tile_id, std::vector<std::uint32_t>{});
        tile_ids[record.tile_id].reserve(
            static_cast<std::size_t>(record.point_count)
        );
        const auto grid_key =
            static_cast<std::uint64_t>(record.tile_y) *
                static_cast<std::uint64_t>(header.grid_count_x) +
            static_cast<std::uint64_t>(record.tile_x);
        grid_to_tile_id.emplace(grid_key, record.tile_id);
    }

    for (std::size_t i = 0; i < dataset.points().size(); ++i) {
        const auto& point = dataset.points()[i];
        const auto tile_x = point_tile_coord_runtime(
            point.x,
            header.grid_origin_x,
            header.tile_size_x,
            header.grid_count_x
        );
        const auto tile_y = point_tile_coord_runtime(
            point.y,
            header.grid_origin_y,
            header.tile_size_y,
            header.grid_count_y
        );
        const auto tile_id =
            static_cast<std::uint64_t>(tile_y) *
                static_cast<std::uint64_t>(header.grid_count_x) +
            static_cast<std::uint64_t>(tile_x);
        const auto grid_found = grid_to_tile_id.find(tile_id);
        if (grid_found == grid_to_tile_id.end()) {
            continue;
        }
        auto found = tile_ids.find(grid_found->second);
        if (found == tile_ids.end()) {
            continue;
        }
        found->second.push_back(source_point_ids[i]);
    }

    for (const auto& record : tile_reader.records()) {
        const auto found = tile_ids.find(record.tile_id);
        if (found == tile_ids.end() ||
            found->second.size() !=
                static_cast<std::size_t>(record.point_count)) {
            throw std::runtime_error(
                "ViewerApp: runtime tile point_id reconstruction failed"
            );
        }
    }

    return tile_ids;
}

enum class GpuPickRequestKind {
    None,
    Hover,
    SetOrbitPivot,
    BoxSelectAnchor
};

struct PickDebugDumpMetadata {
    std::uint64_t dump_index = 0;
    std::uint64_t frame_index = 0;
    int viewport_index = -1;
    std::uint32_t viewport_width = 0;
    std::uint32_t viewport_height = 0;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    std::uint32_t sample_left = 0;
    std::uint32_t sample_top = 0;
    std::uint32_t sample_width = 0;
    std::uint32_t sample_height = 0;
    std::size_t active_lod_level = 0;
    bool tile_overlay_rendered = false;
    bool all_tiles_resident = false;
    std::string render_source{};
    std::string trigger_reason{};
    std::vector<std::uint64_t> selected_tile_ids{};
    std::vector<std::uint64_t> resident_tile_ids{};
};

struct PickDebugDumpFrame {
    PickDebugDumpMetadata metadata{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> color_pixels{};
    std::vector<std::uint32_t> pick_ids{};
};

[[nodiscard]]
std::string join_uint64_list(const std::vector<std::uint64_t>& values)
{
    if (values.empty()) {
        return "-";
    }

    std::ostringstream oss;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            oss << ',';
        }
        oss << values[i];
    }
    return oss.str();
}

[[nodiscard]]
std::array<std::uint8_t, 3> bright_hash_color(std::uint32_t id) noexcept
{
    if (id == 0) {
        return {0, 0, 0};
    }

    // Keep zero strictly black; every non-zero id gets a bright HSV color
    // so "has value but too dark" cannot be mistaken for empty.
    std::uint32_t hash = id;
    hash ^= hash >> 16;
    hash *= 0x7feb352dU;
    hash ^= hash >> 15;
    hash *= 0x846ca68bU;
    hash ^= hash >> 16;

    const float hue =
        static_cast<float>(hash % 360u) / 60.0f;
    const float saturation = 0.90f;
    const float value = 0.98f;

    const float chroma = value * saturation;
    const float x = chroma * (1.0f - std::fabs(std::fmod(hue, 2.0f) - 1.0f));
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    if (hue < 1.0f) {
        r = chroma;
        g = x;
    } else if (hue < 2.0f) {
        r = x;
        g = chroma;
    } else if (hue < 3.0f) {
        g = chroma;
        b = x;
    } else if (hue < 4.0f) {
        g = x;
        b = chroma;
    } else if (hue < 5.0f) {
        r = x;
        b = chroma;
    } else {
        r = chroma;
        b = x;
    }

    const float match = value - chroma;
    r += match;
    g += match;
    b += match;

    return {
        static_cast<std::uint8_t>(std::round(r * 255.0f)),
        static_cast<std::uint8_t>(std::round(g * 255.0f)),
        static_cast<std::uint8_t>(std::round(b * 255.0f))
    };
}

[[nodiscard]]
std::string dump_index_label(std::uint64_t dump_index)
{
    std::ostringstream oss;
    oss << std::setw(4) << std::setfill('0') << dump_index;
    return oss.str();
}

void set_rgb_pixel(
    std::vector<std::uint8_t>& image,
    std::uint32_t width,
    std::uint32_t height,
    int x,
    int y,
    const std::array<std::uint8_t, 3>& color
)
{
    if (x < 0 || y < 0 ||
        x >= static_cast<int>(width) ||
        y >= static_cast<int>(height)) {
        return;
    }

    const std::size_t index =
        (static_cast<std::size_t>(y) * width +
         static_cast<std::size_t>(x)) * 3u;
    if (index + 2 >= image.size()) {
        return;
    }
    image[index + 0] = color[0];
    image[index + 1] = color[1];
    image[index + 2] = color[2];
}

void draw_rect_rgb(
    std::vector<std::uint8_t>& image,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t left,
    std::uint32_t top,
    std::uint32_t rect_width,
    std::uint32_t rect_height,
    const std::array<std::uint8_t, 3>& color
)
{
    if (rect_width == 0 || rect_height == 0) {
        return;
    }

    const int right =
        static_cast<int>(left + rect_width - 1);
    const int bottom =
        static_cast<int>(top + rect_height - 1);
    for (int x = static_cast<int>(left); x <= right; ++x) {
        set_rgb_pixel(image, width, height, x, static_cast<int>(top), color);
        set_rgb_pixel(image, width, height, x, bottom, color);
    }
    for (int y = static_cast<int>(top); y <= bottom; ++y) {
        set_rgb_pixel(image, width, height, static_cast<int>(left), y, color);
        set_rgb_pixel(image, width, height, right, y, color);
    }
}

void draw_cross_rgb(
    std::vector<std::uint8_t>& image,
    std::uint32_t width,
    std::uint32_t height,
    int center_x,
    int center_y,
    int radius,
    const std::array<std::uint8_t, 3>& color
)
{
    for (int dx = -radius; dx <= radius; ++dx) {
        set_rgb_pixel(
            image,
            width,
            height,
            center_x + dx,
            center_y,
            color
        );
    }
    for (int dy = -radius; dy <= radius; ++dy) {
        set_rgb_pixel(
            image,
            width,
            height,
            center_x,
            center_y + dy,
            color
        );
    }
}

void write_binary_ppm(
    const std::filesystem::path& output_path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<std::uint8_t>& rgb_pixels
)
{
    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error(
            "ViewerApp: failed to open debug ppm path: " +
            output_path.string()
        );
    }
    out << "P6\n" << width << ' ' << height << "\n255\n";
    out.write(
        reinterpret_cast<const char*>(rgb_pixels.data()),
        static_cast<std::streamsize>(rgb_pixels.size())
    );
}

[[nodiscard]]
std::vector<std::uint8_t> convert_color_image_to_rgb(
    const std::vector<std::uint8_t>& color_pixels,
    std::uint32_t width,
    std::uint32_t height,
    VkFormat color_format
)
{
    if (color_pixels.size() !=
        static_cast<std::size_t>(width) * height * 4u) {
        throw std::runtime_error(
            "ViewerApp: debug color dump size mismatch"
        );
    }

    const bool is_bgra =
        color_format == VK_FORMAT_B8G8R8A8_UNORM ||
        color_format == VK_FORMAT_B8G8R8A8_SRGB;
    const bool is_rgba =
        color_format == VK_FORMAT_R8G8B8A8_UNORM ||
        color_format == VK_FORMAT_R8G8B8A8_SRGB;
    if (!is_bgra && !is_rgba) {
        throw std::runtime_error(
            "ViewerApp: unsupported debug color format for dump"
        );
    }

    std::vector<std::uint8_t> rgb(
        static_cast<std::size_t>(width) * height * 3u
    );
    for (std::uint32_t out_y = 0; out_y < height; ++out_y) {
        const std::uint32_t src_y = height - 1 - out_y;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t src_index =
                (static_cast<std::size_t>(src_y) * width + x) * 4u;
            const std::size_t dst_index =
                (static_cast<std::size_t>(out_y) * width + x) * 3u;
            if (is_bgra) {
                rgb[dst_index + 0] = color_pixels[src_index + 2];
                rgb[dst_index + 1] = color_pixels[src_index + 1];
                rgb[dst_index + 2] = color_pixels[src_index + 0];
            } else {
                rgb[dst_index + 0] = color_pixels[src_index + 0];
                rgb[dst_index + 1] = color_pixels[src_index + 1];
                rgb[dst_index + 2] = color_pixels[src_index + 2];
            }
        }
    }
    return rgb;
}

[[nodiscard]]
std::vector<std::uint8_t> visualize_pick_ids_to_rgb(
    const std::vector<std::uint32_t>& pick_ids,
    std::uint32_t width,
    std::uint32_t height
)
{
    if (pick_ids.size() != static_cast<std::size_t>(width) * height) {
        throw std::runtime_error(
            "ViewerApp: debug pick-id dump size mismatch"
        );
    }

    std::vector<std::uint8_t> rgb(
        static_cast<std::size_t>(width) * height * 3u
    );
    for (std::uint32_t out_y = 0; out_y < height; ++out_y) {
        const std::uint32_t src_y = height - 1 - out_y;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t src_index =
                static_cast<std::size_t>(src_y) * width + x;
            const std::size_t dst_index =
                (static_cast<std::size_t>(out_y) * width + x) * 3u;
            const auto color = bright_hash_color(pick_ids[src_index]);
            rgb[dst_index + 0] = color[0];
            rgb[dst_index + 1] = color[1];
            rgb[dst_index + 2] = color[2];
        }
    }
    return rgb;
}

void annotate_pick_debug_image(
    std::vector<std::uint8_t>& rgb_pixels,
    const PickDebugDumpMetadata& metadata,
    std::uint32_t width,
    std::uint32_t height
)
{
    static constexpr std::array<std::uint8_t, 3> kRectColor{
        255u, 255u, 255u
    };
    static constexpr std::array<std::uint8_t, 3> kCrossColor{
        255u, 32u, 255u
    };

    draw_rect_rgb(
        rgb_pixels,
        width,
        height,
        metadata.sample_left,
        metadata.sample_top,
        metadata.sample_width,
        metadata.sample_height,
        kRectColor
    );
    draw_cross_rgb(
        rgb_pixels,
        width,
        height,
        static_cast<int>(std::floor(metadata.mouse_x)),
        static_cast<int>(std::floor(metadata.mouse_y)),
        8,
        kCrossColor
    );
}

void write_pick_debug_dump(
    const std::filesystem::path& output_dir,
    const PickDebugDumpFrame& dump
)
{
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);

    const std::string label = dump_index_label(dump.metadata.dump_index);
    const auto color_path =
        output_dir / ("frame_" + label + "_color.ppm");
    const auto pick_path =
        output_dir / ("frame_" + label + "_pick_id.ppm");
    const auto meta_path =
        output_dir / ("frame_" + label + "_meta.txt");

    auto color_rgb = convert_color_image_to_rgb(
        dump.color_pixels,
        dump.width,
        dump.height,
        dump.color_format
    );
    auto pick_rgb = visualize_pick_ids_to_rgb(
        dump.pick_ids,
        dump.width,
        dump.height
    );
    annotate_pick_debug_image(
        color_rgb,
        dump.metadata,
        dump.width,
        dump.height
    );
    annotate_pick_debug_image(
        pick_rgb,
        dump.metadata,
        dump.width,
        dump.height
    );

    write_binary_ppm(color_path, dump.width, dump.height, color_rgb);
    write_binary_ppm(pick_path, dump.width, dump.height, pick_rgb);

    std::ofstream meta(meta_path, std::ios::trunc);
    if (!meta) {
        throw std::runtime_error(
            "ViewerApp: failed to open pick debug metadata path: " +
            meta_path.string()
        );
    }
    meta << "dump_index=" << dump.metadata.dump_index << '\n';
    meta << "frame_index=" << dump.metadata.frame_index << '\n';
    meta << "viewport_index=" << dump.metadata.viewport_index << '\n';
    meta << "viewport_width=" << dump.metadata.viewport_width << '\n';
    meta << "viewport_height=" << dump.metadata.viewport_height << '\n';
    meta << "mouse_x=" << dump.metadata.mouse_x << '\n';
    meta << "mouse_y=" << dump.metadata.mouse_y << '\n';
    meta << "sample_left=" << dump.metadata.sample_left << '\n';
    meta << "sample_top=" << dump.metadata.sample_top << '\n';
    meta << "sample_width=" << dump.metadata.sample_width << '\n';
    meta << "sample_height=" << dump.metadata.sample_height << '\n';
    meta << "active_lod_level=" << dump.metadata.active_lod_level << '\n';
    meta << "tile_overlay_rendered="
         << (dump.metadata.tile_overlay_rendered ? "true" : "false") << '\n';
    meta << "all_tiles_resident="
         << (dump.metadata.all_tiles_resident ? "true" : "false") << '\n';
    meta << "render_source=" << dump.metadata.render_source << '\n';
    meta << "trigger_reason=" << dump.metadata.trigger_reason << '\n';
    meta << "selected_tile_ids="
         << join_uint64_list(dump.metadata.selected_tile_ids) << '\n';
    meta << "resident_tile_ids="
         << join_uint64_list(dump.metadata.resident_tile_ids) << '\n';

    std::cout << "[PICK_DEBUG] wrote color dump: "
              << color_path.string() << '\n';
    std::cout << "[PICK_DEBUG] wrote pick-id dump: "
              << pick_path.string() << '\n';
    std::cout << "[PICK_DEBUG] wrote metadata: "
              << meta_path.string() << '\n';
}

struct GpuPickRequest {
    int viewport_index = -1;
    GpuPickRequestKind kind = GpuPickRequestKind::None;
    int benchmark_query_index = -1;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    std::uint32_t pick_radius_px = kDefaultGpuPickRadiusPx;
    std::uint32_t viewport_width = 0;
    std::uint32_t viewport_height = 0;
    float box_select_min_x = 0.0f;
    float box_select_min_y = 0.0f;
    float box_select_max_x = 0.0f;
    float box_select_max_y = 0.0f;
    gs3d::camera::Camera anchor_camera{};

    [[nodiscard]]
    bool valid() const noexcept {
        return kind != GpuPickRequestKind::None &&
               viewport_index >= 0 &&
               viewport_width > 0 &&
               viewport_height > 0;
    }
};

struct GpuPickResult {
    GpuPickRequest request{};
    std::uint32_t point_id = 0;
    float depth = 1.0f;
    bool has_hit = false;
};

void register_runtime_point_lookup(
    const std::vector<gs3d::data::Gs3dPoint>& points,
    const std::vector<std::uint32_t>& point_ids,
    std::vector<gs3d::data::Gs3dPoint>& points_by_id,
    std::vector<std::uint8_t>& points_valid_by_id
) {
    if (points.size() != point_ids.size()) {
        throw std::runtime_error(
            "ViewerApp: runtime point lookup size mismatch"
        );
    }

    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto point_id = point_ids[i];
        if (point_id == 0 ||
            point_id >= points_by_id.size() ||
            point_id >= points_valid_by_id.size()) {
            throw std::runtime_error(
                "ViewerApp: runtime point lookup id out of range"
            );
        }

        points_by_id[point_id] = points[i];
        points_valid_by_id[point_id] = 1;
    }
}

void register_runtime_tile_point_lookup(
    const std::vector<std::pair<std::uint64_t, SharedTilePoints>>& tiles,
    std::vector<gs3d::data::Gs3dPoint>& points_by_id,
    std::vector<std::uint8_t>& points_valid_by_id
) {
    for (const auto& [tile_id, points] : tiles) {
        static_cast<void>(tile_id);
        if (!points) {
            continue;
        }
        register_runtime_point_lookup(
            points->points,
            points->point_ids,
            points_by_id,
            points_valid_by_id
        );
    }
}

class GpuPickReadback {
public:
    GpuPickReadback(
        const gs3d::render::VulkanContext& context,
        std::uint32_t frames_in_flight,
        std::size_t viewport_count
    )
        : context_(&context)
    {
        slots_.resize(frames_in_flight);
        for (auto& frame_slots : slots_) {
            frame_slots.resize(viewport_count);
        }
        for (auto& frame_slots : slots_) {
            for (auto& slot : frame_slots) {
                slot.id_buffer.create(
                    context,
                    sizeof(std::uint32_t) * kMaxPickPixels,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                );
                slot.depth_buffer.create(
                    context,
                    sizeof(float) * kMaxPickPixels,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                );
            }
        }
    }

    void record_request(
        VkCommandBuffer command_buffer,
        std::uint32_t frame_slot,
        const gs3d::render::OffscreenFramebuffer& framebuffer,
        const GpuPickRequest& request
    ) {
        if (frame_slot >= slots_.size() ||
            request.viewport_index < 0 ||
            static_cast<std::size_t>(request.viewport_index) >=
                slots_[frame_slot].size()) {
            return;
        }

        auto& slot =
            slots_[frame_slot][static_cast<std::size_t>(request.viewport_index)];
        slot.pending = {};

        if (!request.valid()) {
            return;
        }

        const auto extent = framebuffer.extent();
        if (extent.width == 0 || extent.height == 0) {
            return;
        }

        const int center_x = std::clamp(
            static_cast<int>(std::floor(request.mouse_x)),
            0,
            static_cast<int>(extent.width) - 1
        );
        // mouse_y is already in Vulkan framebuffer coordinates (top-left
        // origin, y down), matching both the viewport transform and
        // vkCmdCopyImageToBuffer's image layout. No Y-flip needed.
        const int center_y = std::clamp(
            static_cast<int>(std::floor(request.mouse_y)),
            0,
            static_cast<int>(extent.height) - 1
        );

        const int pick_radius_px = std::clamp(
            static_cast<int>(request.pick_radius_px),
            0,
            kPickRadiusPx
        );
        const std::uint32_t left =
            static_cast<std::uint32_t>(std::max(0, center_x - pick_radius_px));
        const std::uint32_t top =
            static_cast<std::uint32_t>(std::max(0, center_y - pick_radius_px));
        const std::uint32_t right =
            static_cast<std::uint32_t>(
                std::min(
                    static_cast<int>(extent.width) - 1,
                    center_x + pick_radius_px
                )
            );
        const std::uint32_t bottom =
            static_cast<std::uint32_t>(
                std::min(
                    static_cast<int>(extent.height) - 1,
                    center_y + pick_radius_px
                )
            );

        slot.pending.request = request;
        slot.pending.x = left;
        slot.pending.y = top;
        slot.pending.width = right - left + 1;
        slot.pending.height = bottom - top + 1;
        slot.pending.center_local_x =
            static_cast<std::uint32_t>(center_x) - left;
        slot.pending.center_local_y =
            static_cast<std::uint32_t>(center_y) - top;
        slot.pending.pending = true;

        VkBufferImageCopy copy_region{};
        copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_region.imageSubresource.mipLevel = 0;
        copy_region.imageSubresource.baseArrayLayer = 0;
        copy_region.imageSubresource.layerCount = 1;
        copy_region.imageOffset = {
            static_cast<std::int32_t>(slot.pending.x),
            static_cast<std::int32_t>(slot.pending.y),
            0
        };
        copy_region.imageExtent = {
            slot.pending.width,
            slot.pending.height,
            1
        };

        vkCmdCopyImageToBuffer(
            command_buffer,
            framebuffer.pick_image(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            slot.id_buffer.handle(),
            1,
            &copy_region
        );

        vkCmdCopyImageToBuffer(
            command_buffer,
            framebuffer.pick_depth_image(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            slot.depth_buffer.handle(),
            1,
            &copy_region
        );
    }

    [[nodiscard]]
    std::vector<GpuPickResult> collect_ready_frame(std::uint32_t frame_slot) {
        std::vector<GpuPickResult> results;
        if (context_ == nullptr || frame_slot >= slots_.size()) {
            return results;
        }

        for (auto& slot : slots_[frame_slot]) {
            if (!slot.pending.pending) {
                continue;
            }

            const auto pixel_count =
                slot.pending.width * slot.pending.height;
            if (pixel_count == 0 || pixel_count > kMaxPickPixels) {
                slot.pending = {};
                continue;
            }

            void* id_memory = nullptr;
            void* depth_memory = nullptr;
            vkMapMemory(
                context_->device(),
                slot.id_buffer.memory(),
                0,
                sizeof(std::uint32_t) * pixel_count,
                0,
                &id_memory
            );
            vkMapMemory(
                context_->device(),
                slot.depth_buffer.memory(),
                0,
                sizeof(float) * pixel_count,
                0,
                &depth_memory
            );

            const auto* point_ids =
                static_cast<const std::uint32_t*>(id_memory);
            const auto* depths =
                static_cast<const float*>(depth_memory);

            GpuPickResult result;
            result.request = slot.pending.request;
            result.depth = 1.0f;
            std::uint32_t best_distance_sq =
                std::numeric_limits<std::uint32_t>::max();
            for (std::uint32_t y = 0; y < slot.pending.height; ++y) {
                for (std::uint32_t x = 0; x < slot.pending.width; ++x) {
                    const std::uint32_t i =
                        y * slot.pending.width + x;
                    const auto point_id = point_ids[i];
                    if (point_id == 0) {
                        continue;
                    }

                    const float depth = depths[i];
                    const int dx =
                        static_cast<int>(x) -
                        static_cast<int>(slot.pending.center_local_x);
                    const int dy =
                        static_cast<int>(y) -
                        static_cast<int>(slot.pending.center_local_y);
                    const auto distance_sq =
                        static_cast<std::uint32_t>(dx * dx + dy * dy);

                    if (!result.has_hit ||
                        distance_sq < best_distance_sq ||
                        (distance_sq == best_distance_sq &&
                         depth < result.depth)) {
                        result.has_hit = true;
                        result.point_id = point_id;
                        result.depth = depth;
                        best_distance_sq = distance_sq;
                    }
                }
            }

            vkUnmapMemory(context_->device(), slot.depth_buffer.memory());
            vkUnmapMemory(context_->device(), slot.id_buffer.memory());
            slot.pending = {};
            results.push_back(result);
        }

        return results;
    }

private:
    // Buffers are sized for the largest hover aperture we allow.
    // Each request can choose a smaller point-size-aware radius.
    static constexpr int kPickRadiusPx =
        static_cast<int>(kMaxGpuPickRadiusPx);
    // Must hold (2 * kPickRadiusPx + 1)^2 = 121 pixels; 144 leaves
    // headroom for any future 12x12 bump without re-touching this.
    static constexpr std::uint32_t kMaxPickPixels = 144;

    struct PendingState {
        GpuPickRequest request{};
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t center_local_x = 0;
        std::uint32_t center_local_y = 0;
        bool pending = false;
    };

    struct SlotState {
        gs3d::render::VulkanBuffer id_buffer{};
        gs3d::render::VulkanBuffer depth_buffer{};
        PendingState pending{};
    };

    const gs3d::render::VulkanContext* context_ = nullptr;
    std::vector<std::vector<SlotState>> slots_{};
};

class PickDebugFrameDumper {
public:
    PickDebugFrameDumper(
        const gs3d::render::VulkanContext& context,
        std::uint32_t frames_in_flight
    )
        : context_(&context)
    {
        slots_.resize(frames_in_flight);
    }

    [[nodiscard]]
    bool record_request(
        VkCommandBuffer command_buffer,
        std::uint32_t frame_slot,
        const gs3d::render::OffscreenFramebuffer& framebuffer,
        const PickDebugDumpMetadata& metadata
    ) {
        if (context_ == nullptr || frame_slot >= slots_.size()) {
            return false;
        }

        const auto extent = framebuffer.extent();
        if (extent.width == 0 || extent.height == 0) {
            return false;
        }

        auto& slot = slots_[frame_slot];
        const VkDeviceSize color_bytes =
            static_cast<VkDeviceSize>(extent.width) *
            extent.height *
            4u;
        const VkDeviceSize id_bytes =
            static_cast<VkDeviceSize>(extent.width) *
            extent.height *
            sizeof(std::uint32_t);
        ensure_buffer_size(slot.color_buffer, color_bytes);
        ensure_buffer_size(slot.id_buffer, id_bytes);

        transition_color_image_for_dump(
            command_buffer,
            framebuffer.color_image(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT
        );

        VkBufferImageCopy copy_region{};
        copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_region.imageSubresource.mipLevel = 0;
        copy_region.imageSubresource.baseArrayLayer = 0;
        copy_region.imageSubresource.layerCount = 1;
        copy_region.imageOffset = {0, 0, 0};
        copy_region.imageExtent = {
            extent.width,
            extent.height,
            1
        };

        vkCmdCopyImageToBuffer(
            command_buffer,
            framebuffer.color_image(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            slot.color_buffer.handle(),
            1,
            &copy_region
        );
        vkCmdCopyImageToBuffer(
            command_buffer,
            framebuffer.pick_image(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            slot.id_buffer.handle(),
            1,
            &copy_region
        );

        transition_color_image_for_dump(
            command_buffer,
            framebuffer.color_image(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        );

        slot.pending = true;
        slot.metadata = metadata;
        slot.width = extent.width;
        slot.height = extent.height;
        slot.color_format = framebuffer.color_format();
        return true;
    }

    [[nodiscard]]
    std::optional<PickDebugDumpFrame> collect_ready_frame(
        std::uint32_t frame_slot
    ) {
        if (context_ == nullptr || frame_slot >= slots_.size()) {
            return std::nullopt;
        }

        auto& slot = slots_[frame_slot];
        if (!slot.pending ||
            slot.width == 0 ||
            slot.height == 0 ||
            !slot.color_buffer.valid() ||
            !slot.id_buffer.valid()) {
            return std::nullopt;
        }

        void* color_memory = nullptr;
        void* id_memory = nullptr;
        const VkDeviceSize color_bytes =
            static_cast<VkDeviceSize>(slot.width) *
            slot.height *
            4u;
        const VkDeviceSize id_bytes =
            static_cast<VkDeviceSize>(slot.width) *
            slot.height *
            sizeof(std::uint32_t);

        vkMapMemory(
            context_->device(),
            slot.color_buffer.memory(),
            0,
            color_bytes,
            0,
            &color_memory
        );
        vkMapMemory(
            context_->device(),
            slot.id_buffer.memory(),
            0,
            id_bytes,
            0,
            &id_memory
        );

        PickDebugDumpFrame dump;
        dump.metadata = slot.metadata;
        dump.color_format = slot.color_format;
        dump.width = slot.width;
        dump.height = slot.height;
        dump.color_pixels.resize(static_cast<std::size_t>(color_bytes));
        dump.pick_ids.resize(
            static_cast<std::size_t>(slot.width) * slot.height
        );
        std::memcpy(
            dump.color_pixels.data(),
            color_memory,
            static_cast<std::size_t>(color_bytes)
        );
        std::memcpy(
            dump.pick_ids.data(),
            id_memory,
            static_cast<std::size_t>(id_bytes)
        );

        vkUnmapMemory(context_->device(), slot.id_buffer.memory());
        vkUnmapMemory(context_->device(), slot.color_buffer.memory());
        slot.pending = false;
        return dump;
    }

private:
    struct SlotState {
        gs3d::render::VulkanBuffer color_buffer{};
        gs3d::render::VulkanBuffer id_buffer{};
        PickDebugDumpMetadata metadata{};
        VkFormat color_format = VK_FORMAT_UNDEFINED;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool pending = false;
    };

    void ensure_buffer_size(
        gs3d::render::VulkanBuffer& buffer,
        VkDeviceSize size
    ) {
        if (buffer.valid() && buffer.size() == size) {
            return;
        }
        buffer.destroy();
        buffer.create(
            *context_,
            size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
    }

    static void transition_color_image_for_dump(
        VkCommandBuffer command_buffer,
        VkImage image,
        VkImageLayout old_layout,
        VkImageLayout new_layout,
        VkAccessFlags src_access_mask,
        VkAccessFlags dst_access_mask,
        VkPipelineStageFlags src_stage_mask,
        VkPipelineStageFlags dst_stage_mask
    ) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = src_access_mask;
        barrier.dstAccessMask = dst_access_mask;
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            command_buffer,
            src_stage_mask,
            dst_stage_mask,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );
    }

    const gs3d::render::VulkanContext* context_ = nullptr;
    std::vector<SlotState> slots_{};
};

gs3d::render::PointCloudLodSource build_lod_source(
    const gs3d::data::Gs3dLodDataset& lod_dataset,
    const std::vector<std::vector<std::uint32_t>>& lod_point_ids
) {
    gs3d::render::PointCloudLodSource source;
    source.levels.reserve(lod_dataset.level_count());
    for (std::size_t i = 0; i < lod_dataset.level_count(); ++i) {
        const auto& level = lod_dataset.level(i);
        source.levels.push_back({
            level.name,
            gs3d::data::Gs3dLodDataset::voxel_mode_name(level.voxel_mode),
            level.level_index,
            level.source_point_count,
            level.target_point_count,
            level.voxel_size,
            gs3d::data::make_point_data_view(
                level.points,
                lod_point_ids[i].data()
            )
        });
    }
    return source;
}

[[nodiscard]]
gs3d::data::Gs3dPoint to_gs3d_point(
    const gs3d::core::PointRecord& point
) noexcept {
    return {
        point.x,
        point.y,
        point.z,
        point.value
    };
}

[[nodiscard]]
std::optional<gs3d::data::Gs3dPoint> find_point_by_id_in_views(
    const std::vector<gs3d::core::PointDataView>& candidate_point_sets,
    std::uint32_t point_id
) noexcept {
    if (point_id == 0) {
        return std::nullopt;
    }

    for (const auto& points : candidate_point_sets) {
        if (!points.valid() || points.empty() || !points.has_point_ids()) {
            continue;
        }

        for (std::uint64_t i = 0; i < points.point_count; ++i) {
            if (points.point_id_at(i) != point_id) {
                continue;
            }
            return to_gs3d_point(points.point_at(i));
        }
    }

    return std::nullopt;
}

void initialize_camera_from_config(
    gs3d::camera::Camera& camera,
    const ViewerAppConfig& config,
    const gs3d::camera::CameraBounds& bounds
) {
    // Default: orthographic projection, fit to data bounds.
    // fit_bounds() sets ortho_height, near/far, position, target, up.
    camera.set_orthographic(10.0f, 0.01f, 10000.0f);

    if (config.camera_mode == "fit") {
        camera.fit_bounds(bounds);
        return;
    }

    // Explicit camera overrides: still use ortho by default.
    camera.look_at(
        to_vec3(config.camera_position),
        to_vec3(config.camera_target),
        to_vec3(config.camera_up)
    );
    // Derive ortho_height from distance and FOV for backwards compat.
    const float fov_rad = config.camera_fov_y * 3.14159265f / 180.0f;
    const float view_h =
        2.0f * camera.distance() * std::tan(fov_rad * 0.5f);
    // Ortho near/far: small near, huge far — covers any practical depth.
    camera.set_orthographic(view_h, 0.01f, 1.0e7f);
}

void fill_push_constants(
    gs3d::render::PointPushConstants& push,
    const gs3d::camera::Camera& camera
) {
    const auto mvp = camera.view_projection_matrix();
    std::copy(mvp.m.begin(), mvp.m.end(), push.mvp);
    // flags (colormap, value_clip, spatial_clip) are managed on the
    // template push object and copied per-viewport — do NOT reset here.
}

/*
 * QGIS Print Layout 风格的地图坐标框：
 *   底面矩形框 + 四边刻度标注 + 四根角柱 + 高程刻度。
 *
 * 所有几何在这里算好（世界坐标 -> 视口本地像素），UiRoot 只管用 ImGui
 * draw list 画线/画字，不需要碰相机或投影矩阵。
 */
/*
 * Orientation gizmo 三轴方向：用 MouseRay::to_screen 把相机 target 点和
 * target+axis*step 点分别投影到屏幕，差值即为世界轴在屏幕上的方向。
 * 存进 RenderViewState::gizmo_*_axis，UiRoot 只管照着画线。
 * 每帧调用，所以旋转主视图时 gizmo 同步旋转。
 */
void compute_gizmo_axes(
    gs3d::app::RenderViewState& view,
    const gs3d::camera::Camera& camera
) {
    view.gizmo_axes_valid = false;

    const gs3d::camera::Viewport viewport{
        camera.viewport_width(),
        camera.viewport_height()
    };

    const auto target = camera.target();
    const float step = 1.0f;
    const gs3d::camera::Vec3 axes[] = {
        {step, 0.0f, 0.0f},
        {0.0f, step, 0.0f},
        {0.0f, 0.0f, step},
    };

    const auto center =
        gs3d::camera::MouseRay::to_screen(target, viewport, camera);
    if (!center) return;

    gs3d::app::RenderViewState::GizmoAxisEnd ends[3];

    for (int i = 0; i < 3; ++i) {
        const auto tip = gs3d::camera::MouseRay::to_screen(
            {target.x + axes[i].x,
             target.y + axes[i].y,
             target.z + axes[i].z},
            viewport, camera);
        if (!tip) return;

        ends[i].dx = tip->x - center->x;
        ends[i].dy = tip->y - center->y;
    }

    view.gizmo_x_axis   = ends[0];
    view.gizmo_y_axis   = ends[1];
    view.gizmo_z_axis   = ends[2];
    view.gizmo_axes_valid = true;
}

void compute_axis_overlay(
    gs3d::app::RenderViewState& view,
    const gs3d::camera::CameraBounds& bounds,
    const gs3d::camera::Camera& camera,
    double origin_x,
    double origin_y,
    double origin_z,
    float  z_label_mult   = 1.0f,
    float  z_label_offset = 0.0f
) {
    view.axis_lines.clear();
    view.axis_tick_labels.clear();

    if (!view.show_world_axis) {
        return;
    }

    const gs3d::camera::Viewport viewport{
        camera.viewport_width(),
        camera.viewport_height()
    };

    const float min_x = bounds.min.x;
    const float min_y = bounds.min.y;
    const float min_z = bounds.min.z;
    const float max_x = bounds.max.x;
    const float max_y = bounds.max.y;
    const float max_z = bounds.max.z;

    // ---- helper: project to screen, skip if behind camera ----
    const auto sv = [&](float x, float y, float z)
        -> std::optional<gs3d::camera::ScreenPoint>
    {
        return gs3d::camera::MouseRay::to_screen(
            {x, y, z}, viewport, camera
        );
    };

    // ---- helper: push a screen-space line segment ----
    const auto add_line =
        [&](const std::optional<gs3d::camera::ScreenPoint>& a,
            const std::optional<gs3d::camera::ScreenPoint>& b) {
            if (a && b) {
                view.axis_lines.push_back({a->x, a->y, b->x, b->y});
            }
        };

    // ---- 1. 底面矩形框（四条边，参考面 = z_label_offset，对应属性值 0）----
    const float ref_z = z_label_offset;
    add_line(sv(min_x, min_y, ref_z), sv(max_x, min_y, ref_z)); // 底边
    add_line(sv(max_x, min_y, ref_z), sv(max_x, max_y, ref_z)); // 右边
    add_line(sv(max_x, max_y, ref_z), sv(min_x, max_y, ref_z)); // 顶边
    add_line(sv(min_x, max_y, ref_z), sv(min_x, min_y, ref_z)); // 左边

    // ---- 2. 四根角柱（覆盖数据范围 + 参考面）----
    const float pillar_base = std::min(min_z, ref_z);
    const float pillar_top  = std::max(max_z, ref_z);
    add_line(sv(min_x, min_y, pillar_base), sv(min_x, min_y, pillar_top));
    add_line(sv(max_x, min_y, pillar_base), sv(max_x, min_y, pillar_top));
    add_line(sv(max_x, max_y, pillar_base), sv(max_x, max_y, pillar_top));
    add_line(sv(min_x, max_y, pillar_base), sv(min_x, max_y, pillar_top));

    // ---- 3. X 轴刻度（底边 + 顶边，参考面）----
    {
        const auto x_ticks =
            gs3d::render::compute_axis_ticks(min_x, max_x, 5);
        for (const float tick : x_ticks) {
            const auto bottom = sv(tick, min_y, ref_z);
            const auto top    = sv(tick, max_y, ref_z);
            // small tick mark (8 px outward from the frame)
            constexpr float kMarkPx = 8.0f;
            if (bottom) {
                // Tick along bottom edge, label below frame (y + kMarkPx)
                view.axis_lines.push_back(
                    {bottom->x, bottom->y, bottom->x, bottom->y + kMarkPx}
                );
                char label[32];
                std::snprintf(label, sizeof(label), "%.0f",
                    static_cast<double>(tick) + origin_x);
                view.axis_tick_labels.push_back(
                    {bottom->x, bottom->y + kMarkPx + 2.0f, label}
                );
            }
            if (top) {
                view.axis_lines.push_back(
                    {top->x, top->y, top->x, top->y - kMarkPx}
                );
                char label[32];
                std::snprintf(label, sizeof(label), "%.0f",
                    static_cast<double>(tick) + origin_x);
                view.axis_tick_labels.push_back(
                    {top->x, top->y - kMarkPx - 14.0f, label}
                );
            }
        }
    }

    // ---- 4. Y 轴刻度（左边 + 右边，参考面）----
    {
        const auto y_ticks =
            gs3d::render::compute_axis_ticks(min_y, max_y, 5);
        for (const float tick : y_ticks) {
            const auto left  = sv(min_x, tick, ref_z);
            const auto right = sv(max_x, tick, ref_z);
            constexpr float kMarkPx = 8.0f;
            if (left) {
                view.axis_lines.push_back(
                    {left->x, left->y, left->x - kMarkPx, left->y}
                );
                char label[32];
                std::snprintf(label, sizeof(label), "%.0f",
                    static_cast<double>(tick) + origin_y);
                view.axis_tick_labels.push_back(
                    {left->x - kMarkPx - 3.0f, left->y, label}
                );
            }
            if (right) {
                view.axis_lines.push_back(
                    {right->x, right->y, right->x + kMarkPx, right->y}
                );
                char label[32];
                std::snprintf(label, sizeof(label), "%.0f",
                    static_cast<double>(tick) + origin_y);
                view.axis_tick_labels.push_back(
                    {right->x + kMarkPx + 2.0f, right->y, label}
                );
            }
        }
    }

    // ---- 5. Z / 高程刻度（左下角柱）----
    // 在标签空间（属性原始值范围）确定刻度，再映射回世界 Z 画几何。
    // nice_step 进位激进（>2.0→5.0），对小范围可能只产 1 个 tick，
    // 逐步提高目标数兜底，确保至少 2 个刻度。
    {
        const float label_min =
            (min_z - z_label_offset) / z_label_mult;
        const float label_max =
            (max_z - z_label_offset) / z_label_mult;
        std::vector<float> label_ticks;
        for (int target = 4; target <= 12 && label_ticks.size() < 2; target += 2) {
            label_ticks =
                gs3d::render::compute_axis_ticks(label_min, label_max, target);
        }

        for (const float label_val : label_ticks) {
            const float world_z =
                label_val * z_label_mult + z_label_offset;
            const auto p = sv(min_x, min_y, world_z);
            if (!p) continue;
            constexpr float kMarkPx = 8.0f;
            view.axis_lines.push_back(
                {p->x, p->y, p->x - kMarkPx, p->y}
            );
            char label[32];
            std::snprintf(label, sizeof(label), "%.6g",
                static_cast<double>(label_val));
            view.axis_tick_labels.push_back(
                {p->x - kMarkPx - 3.0f, p->y, label}
            );
        }
    }
}

/*
 * 地图式坐标轴可见范围：从相机参数估算视口内 X/Y 平面的可见坐标范围。
 * 对 2.5D 数据是合理近似，极斜视角下有轻微偏差但不影响刻度使用。
 */
void compute_map_axis_overlay(
    gs3d::app::RenderViewState& view,
    const gs3d::camera::Camera& camera,
    double origin_x,
    double origin_y
) {
    const gs3d::camera::Viewport vp{
        camera.viewport_width(),
        camera.viewport_height()
    };

    // Ground plane: Z = 0, normal = (0, 0, 1)
    const gs3d::camera::Vec3 ground_point{0.0f, 0.0f, 0.0f};
    const gs3d::camera::Vec3 ground_normal{0.0f, 0.0f, 1.0f};

    // 4 corners of the viewport in screen space (y=0 is top in GLFW/ImGui)
    const float scr_w = static_cast<float>(vp.width);
    const float scr_h = static_cast<float>(vp.height);
    const float corners[4][2] = {
        {0.0f,     0.0f},       // top-left
        {scr_w,    0.0f},       // top-right
        {0.0f,     scr_h},      // bottom-left
        {scr_w,    scr_h}       // bottom-right
    };

    // ── diagnostic: map-axis update trace (rate-limited, off by default) ──
    constexpr bool kMapAxisDiag = false;  // set true to enable
    static int diag_frame_count = 0;
    const bool diag_now = kMapAxisDiag && (++diag_frame_count % 30 == 0);

    float xs[4], ys[4];
    int valid = 0;

    for (int i = 0; i < 4; ++i) {
        const auto ray = gs3d::camera::MouseRay::from_screen(
            static_cast<double>(corners[i][0]),
            static_cast<double>(corners[i][1]),
            vp,
            camera
        );

        const auto hit = gs3d::camera::MouseRay::intersect_plane(
            ray, ground_point, ground_normal
        );

        if (hit) {
            xs[valid] = hit->x;
            ys[valid] = hit->y;
            ++valid;
        }

        if (diag_now) {
            std::fprintf(stderr,
                "[MAPAXIS] frame=%d corner[%d] scr=(%.0f,%.0f) "
                "ray_org=(%.2f,%.2f,%.2f) ray_dir=(%.4f,%.4f,%.4f) "
                "hit_z0=%s hit_xyz=(%.2f,%.2f,%.2f)\n",
                diag_frame_count, i,
                static_cast<double>(corners[i][0]),
                static_cast<double>(corners[i][1]),
                static_cast<double>(ray.origin.x),
                static_cast<double>(ray.origin.y),
                static_cast<double>(ray.origin.z),
                static_cast<double>(ray.direction.x),
                static_cast<double>(ray.direction.y),
                static_cast<double>(ray.direction.z),
                hit ? "YES" : "NO",
                hit ? static_cast<double>(hit->x) : 0.0,
                hit ? static_cast<double>(hit->y) : 0.0,
                hit ? static_cast<double>(hit->z) : 0.0);
        }
    }

    if (valid == 0) {
        /*
         * No ray hits Z=0 (e.g. camera is below the ground plane or
         * looking upwards). Fall back to the visible-world estimate
         * centred on the camera target.
         */
        float world_w, world_h;
        if (camera.projection_mode() ==
            gs3d::camera::ProjectionMode::Orthographic) {
            world_h = camera.ortho_height();
            world_w = world_h * camera.aspect_ratio();
        } else {
            const float distance = camera.distance();
            const float fov_rad  =
                camera.fov_y_degrees() * (3.14159265f / 180.0f);
            world_h = 2.0f * distance * std::tan(fov_rad * 0.5f);
            world_w = world_h * camera.aspect_ratio();
        }
        const float cx = camera.target().x;
        const float cy = camera.target().y;
        view.map_axis_x_min = cx - world_w * 0.5f;
        view.map_axis_x_max = cx + world_w * 0.5f;
        view.map_axis_y_min = cy - world_h * 0.5f;
        view.map_axis_y_max = cy + world_h * 0.5f;
    } else {
        float x_min = xs[0], x_max = xs[0];
        float y_min = ys[0], y_max = ys[0];
        for (int i = 1; i < valid; ++i) {
            if (xs[i] < x_min) x_min = xs[i];
            if (xs[i] > x_max) x_max = xs[i];
            if (ys[i] < y_min) y_min = ys[i];
            if (ys[i] > y_max) y_max = ys[i];
        }
        view.map_axis_x_min = x_min;
        view.map_axis_x_max = x_max;
        view.map_axis_y_min = y_min;
        view.map_axis_y_max = y_max;
    }

    view.map_axis_origin_x = origin_x;
    view.map_axis_origin_y = origin_y;

    if (diag_now) {
        std::fprintf(stderr,
            "[MAPAXIS] frame=%d vp=%d &cam=%p valid=%d fallback=%s "
            "cam_target=(%.2f,%.2f,%.2f) cam_pos=(%.2f,%.2f,%.2f) "
            "ortho_h=%.2f vp=%ux%u "
            "axis_xy=[%.2f,%.2f]x[%.2f,%.2f]\n",
            diag_frame_count,
            view.viewport_index,
            static_cast<const void*>(&camera),
            valid,
            (valid == 0) ? "YES" : "NO",
            static_cast<double>(camera.target().x),
            static_cast<double>(camera.target().y),
            static_cast<double>(camera.target().z),
            static_cast<double>(camera.position().x),
            static_cast<double>(camera.position().y),
            static_cast<double>(camera.position().z),
            static_cast<double>(camera.ortho_height()),
            vp.width, vp.height,
            static_cast<double>(view.map_axis_x_min),
            static_cast<double>(view.map_axis_x_max),
            static_cast<double>(view.map_axis_y_min),
            static_cast<double>(view.map_axis_y_max));
    }
}

void print_dataset_info(
    const gs3d::data::Gs3dDataset& dataset
) {
    std::cout << "[OK] Dataset loaded.\n";
    std::cout << "point_count = " << dataset.point_count() << '\n';
    std::cout << "loaded_point_bytes = "
              << dataset.point_bytes() << '\n';
    std::cout << "metadata_only = "
              << (dataset.metadata_only() ? "true" : "false")
              << '\n';

    std::cout << "bbox_min = ["
              << dataset.bbox_min_x() << ", "
              << dataset.bbox_min_y() << ", "
              << dataset.bbox_min_z() << "]\n";

    std::cout << "bbox_max = ["
              << dataset.bbox_max_x() << ", "
              << dataset.bbox_max_y() << ", "
              << dataset.bbox_max_z() << "]\n";

    std::cout << "value_range = ["
              << dataset.value_min() << ", "
              << dataset.value_max() << "]\n";
}

void print_controls(
    bool lod_enabled,
    bool tile_enabled
){
    std::cout << "[OK] Entering render loop.\n";
    std::cout << "操作说明：\n";
    std::cout << "  左键拖动：轨道旋转\n";
    std::cout << "  右键拖动：视角平移\n";
    std::cout << "  滚轮：缩放到光标位置\n";
    std::cout << "  Ctrl+左键拖动：框选\n";
    std::cout << "  双击点：选择并设置旋转中心\n";
    std::cout << "  F：聚焦选中点\n";
    std::cout << "  + / -：调整点大小\n";
    std::cout << "  R：恢复全局视图\n";
    std::cout << "  Tab：切换着色属性\n";
    std::cout << "  Esc：退出\n";
    std::cout << "渲染模式：\n";
    std::cout << "  LOD         : "
              << (lod_enabled ? "启用" : "关闭")
              << '\n';
    std::cout << "  全分辨率瓦片："
              << (tile_enabled ? "启用" : "关闭")
              << '\n';
}

// Round a world-space distance to a human-readable "nice" value:
//   1, 2, 5, 10, 20, 50, 100, 200, 500, 1000 …
// Follows the same algorithm used by Leaflet (BSD-2) and Cesium (Apache 2).
float nice_scale_distance(float raw)
{
    if (raw <= 0.0f) return 1.0f;
    // Guard against denormalized floats: log10(very small) -> pow underflow -> 0.
    constexpr float kMinRaw = 1.0e-30f;
    if (raw < kMinRaw) return kMinRaw;
    const double mag_d = std::pow(10.0, std::floor(std::log10(static_cast<double>(raw))));
    if (mag_d <= 0.0) return 1.0f;
    const float mag = static_cast<float>(mag_d);
    const float n   = raw / mag;
    if (n < 1.5f) return       mag;
    if (n < 3.5f) return 2.0f * mag;
    if (n < 7.5f) return 5.0f * mag;
    return 10.0f * mag;
}

// 假设数据集坐标单位为米（UTM / 本地网格）。若源数据使用其他单位
// （如英尺、度），需要按数据集配置比例尺单位标签。

// ponytail: 硬编码公制单位，若支持多数据源需改为可配置。
std::string format_scale_distance(float d)
{
    char buf[48];
    if (d >= 1000.0f) {
        std::snprintf(buf, sizeof(buf), "%.0f 千米",
            static_cast<double>(d / 1000.0f));
    } else if (d >= 1.0f) {
        std::snprintf(buf, sizeof(buf), "%.0f 米",
            static_cast<double>(d));
    } else if (d >= 0.01f) {
        std::snprintf(buf, sizeof(buf), "%.0f 厘米",
            static_cast<double>(d * 100.0f));
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f 毫米",
            static_cast<double>(d * 1000.0f));
    }
    return buf;
}

std::string format_vec3_text(const gs3d::camera::Vec3& value)
{
    char buf[96];
    std::snprintf(
        buf,
        sizeof(buf),
        "%.1f, %.1f, %.1f",
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z)
    );
    return buf;
}

constexpr double kTileSelectionDebounceSeconds = 0.12;
constexpr double kInteractingDebounceSeconds = 0.15;
constexpr int kMaxViewportCount = 4;

} // namespace

ViewerApp::ViewerApp(ViewerAppConfig config)
    : config_(std::move(config))
{
}

int ViewerApp::run() {
    try {
        gs3d::util::Stopwatch startup_timer;

        const bool can_start_from_metadata =
            config_.lod_enabled &&
            !config_.lod_keep_full_buffer &&
            config_.lod_auto_load_sidecar &&
            !config_.lod_sidecar_path.empty() &&
            std::filesystem::exists(config_.lod_sidecar_path);

        gs3d::util::Stopwatch dataset_load_timer;
        auto dataset = can_start_from_metadata
            ? gs3d::data::Gs3dDatasetLoader::load_header_only(
                config_.gs3d_path
            )
            : gs3d::data::Gs3dDatasetLoader::load(
                config_.gs3d_path
            );
        std::cout << "[TIME] viewer.dataset_load_seconds = "
                  << dataset_load_timer.elapsed_seconds()
                  << '\n';

        if (!dataset.is_consistent()) {
            std::cerr << "[FAIL] dataset is inconsistent.\n";
            return 1;
        }

        if (dataset.point_count() == 0) {
            std::cerr << "[FAIL] dataset is empty.\n";
            return 1;
        }

        print_dataset_info(dataset);
        const auto full_point_ids =
            make_runtime_point_ids(dataset.point_count());

        std::optional<gs3d::data::Gs3dTileReader> tile_reader;
        gs3d::core::TileIndexView tile_index_view;
        std::unordered_map<std::uint64_t, std::vector<std::uint32_t>>
            tile_point_ids_by_tile;

        if (config_.tile_enabled) {
            gs3d::util::Stopwatch tile_reader_timer;
            tile_reader =
                gs3d::data::Gs3dTileReader::open(
                    config_.tile_index_path,
                    config_.tile_data_path,
                    dataset.header()
                );
            std::cout << "[TIME] viewer.tile_reader_open_seconds = "
                      << tile_reader_timer.elapsed_seconds()
                      << '\n';

            if (!tile_reader->valid()) {
                std::cerr << "[FAIL] TileReader is invalid.\n";
                return 1;
            }

            const auto tile_stats =
                tile_reader->stats();

            std::cout << "[OK] TileReader opened.\n";
            std::cout << "tile_count = "
                      << tile_stats.tile_count
                      << '\n';
            std::cout << "tile_total_point_count = "
                      << tile_stats.total_point_count
                      << '\n';
            std::cout << "tile_total_point_bytes = "
                      << tile_stats.total_point_bytes
                      << '\n';

            tile_index_view =
                gs3d::data::make_tile_index_view(*tile_reader);

            if (dataset.metadata_only()) {
                // tile runtime-id reconstruction needs to scan the
                // full point buffer (map_subsequence_point_ids does an
                // exact point match against dataset.points()); in the
                // metadata-only startup path the dataset has no points
                // resident, so fall back to loading the full GS3D
                // before building tile ids. Mirrors the LOD fallback
                // below.
                std::cout
                    << "[WARN] Metadata-only startup cannot build "
                    << "tile runtime ids; loading full GS3D data.\n";
                gs3d::util::Stopwatch fallback_load_timer;
                dataset = gs3d::data::Gs3dDatasetLoader::load(
                    config_.gs3d_path
                );
                std::cout
                    << "[TIME] viewer.dataset_fallback_load_seconds = "
                    << fallback_load_timer.elapsed_seconds()
                    << '\n';
            }

            tile_point_ids_by_tile =
                build_runtime_tile_point_ids(
                    dataset,
                    *tile_reader,
                    full_point_ids
                );
        }

        gs3d::data::Gs3dLodDataset lod_dataset;
        std::vector<std::vector<std::uint32_t>> lod_point_ids;

        if (config_.lod_enabled) {
            gs3d::util::Stopwatch lod_timer;
            lod_dataset =
                load_or_build_lod_dataset(
                    dataset,
                    config_
                );
            std::cout << "[TIME] viewer.lod_prepare_seconds = "
                      << lod_timer.elapsed_seconds()
                      << '\n';

            lod_point_ids.reserve(lod_dataset.level_count());
            for (const auto& level : lod_dataset.levels()) {
                lod_point_ids.push_back(
                    map_subsequence_point_ids(
                        dataset.points(),
                        full_point_ids,
                        level.points,
                        "LOD level"
                    )
                );
            }
        }

        std::vector<gs3d::data::Gs3dPoint> runtime_points_by_id(
            full_point_ids.size() + 1
        );
        std::vector<std::uint8_t> runtime_points_valid_by_id(
            full_point_ids.size() + 1,
            0
        );
        if (dataset.has_point_data()) {
            register_runtime_point_lookup(
                dataset.points(),
                full_point_ids,
                runtime_points_by_id,
                runtime_points_valid_by_id
            );
        }
        for (std::size_t i = 0; i < lod_dataset.level_count(); ++i) {
            register_runtime_point_lookup(
                lod_dataset.level(i).points,
                lod_point_ids[i],
                runtime_points_by_id,
                runtime_points_valid_by_id
            );
        }
        
        gs3d::platform::WindowConfig window_config;
        window_config.width = config_.window_width;
        window_config.height = config_.window_height;
        window_config.title = config_.window_title;
        window_config.resizable = config_.window_resizable;

        gs3d::platform::Window window(window_config);

        gs3d::render::VulkanContextConfig vk_config;
        vk_config.enable_validation_layers =
            config_.enable_validation_layers;
        vk_config.application_name = "GeoScatter3D";

        gs3d::render::VulkanContext context(window, vk_config);
        std::cout << "[TIME] viewer.startup_seconds = "
                  << startup_timer.elapsed_seconds()
                  << '\n';

        std::cout << "[OK] VulkanContext created.\n";
        std::cout << "Physical device: "
                  << context.physical_device_name() << '\n';

        gs3d::render::VulkanSwapchain swapchain(
            context,
            window,
            benchmark_present_mode_hint(
                config_.benchmark_present_mode
            )
        );
        gs3d::render::VulkanRenderer renderer(context, swapchain);

        gs3d::gui::ImGuiLayer imgui_layer;
        imgui_layer.init(
            window.native_handle(),
            context,
            renderer,
            swapchain.image_count(),
            config_.ui_layout_ini_path
        );

        gs3d::render::ClearColor clear_color;
        clear_color.r = config_.clear_color[0];
        clear_color.g = config_.clear_color[1];
        clear_color.b = config_.clear_color[2];
        clear_color.a = config_.clear_color[3];
        renderer.set_clear_color(clear_color);

        std::unique_ptr<gs3d::render::PointCloudGpu> full_gpu_cloud;
        std::unique_ptr<gs3d::render::PointCloudLodGpu> lod_gpu_cloud;
        std::unique_ptr<gs3d::render::PointCloudTileGpu> tile_gpu_cloud;

        if (config_.lod_enabled) {
            const auto lod_source =
                build_lod_source(lod_dataset, lod_point_ids);
            lod_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudLodGpu>(
                    context,
                    renderer.command_pool(),
                    context.graphics_queue(),
                    lod_source
                );

            std::cout << "[OK] PointCloudLodGpu uploaded.\n";
            std::cout << lod_gpu_cloud->summary();

        } else {
            full_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudGpu>(
                    context,
                    renderer.command_pool(),
                    context.graphics_queue(),
                    gs3d::data::make_point_data_view(
                        dataset,
                        full_point_ids.data()
                    )
                );

            std::cout << "[OK] PointCloudGpu uploaded.\n";
            std::cout << "gpu point_count = "
                      << full_gpu_cloud->point_count()
                      << '\n';
        }

        // Camera must be initialized before ViewportManager (which clones it).
        // Bounds are needed for fit-mode and for the CameraController.
        const gs3d::camera::CameraBounds bounds =
            make_camera_bounds(dataset);

        gs3d::camera::Camera initial_camera;
        VkExtent2D initial_viewport_extent = swapchain.extent();
        initial_camera.set_viewport(
            initial_viewport_extent.width,
            initial_viewport_extent.height
        );
        initialize_camera_from_config(initial_camera, config_, bounds);

        // ViewportManager: N (OffscreenFramebuffer, Camera) pairs.
        // All framebuffers use swapchain.image_format() → Vulkan-compatible with
        // each other, so a single PointPipeline works for all viewports.
        // Must be created after ImGui init (registers descriptors via AddTexture).
        gs3d::render::ViewportManager viewport_manager;
        viewport_manager.init(
            context,
            swapchain.image_format(),
            kMaxViewportCount,
            initial_viewport_extent,
            initial_camera
        );
        viewport_manager.set_active_count(
            std::clamp(config_.viewport_count, 1, kMaxViewportCount)
        );
        viewport_manager.set_clear_color(clear_color);

        gs3d::render::PointPipelineConfig pipeline_config;
        pipeline_config.vertex_shader_path =
            config_.vertex_shader_path;
        pipeline_config.fragment_shader_path =
            config_.fragment_shader_path;

        gs3d::render::PointPipeline point_pipeline(
            context,
            viewport_manager.render_pass(),
            pipeline_config
        );

        std::cout << "[OK] PointPipeline created.\n";

        GpuPickReadback gpu_pick_readback(
            context,
            renderer.frames_in_flight(),
            static_cast<std::size_t>(viewport_manager.viewport_count())
        );
        PickDebugFrameDumper pick_debug_frame_dumper(
            context,
            renderer.frames_in_flight()
        );
        std::uint64_t pick_debug_dump_count = 0;
        bool pick_debug_dump_completed = false;
        std::vector<bool> pending_hover_miss_dump(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            false
        );
        std::vector<std::optional<gs3d::data::Gs3dPoint>>
            latest_gpu_hover_points(
                static_cast<std::size_t>(viewport_manager.viewport_count())
            );
        std::vector<float> latest_gpu_capture_x(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            0.0f
        );
        std::vector<float> latest_gpu_capture_y(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            0.0f
        );
        // Frames since last successful pick hit — clears stale hover
        // data after ~0.5 s of no hits.
        std::vector<int> hover_timeout(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            0
        );
        // Consecutive GPU pick misses (has_hit=false) — clears hover
        // after a short debounce so moving between points doesn't flicker.
        std::vector<int> consecutive_no_hit(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            0
        );
        constexpr int kNoHitClearThreshold = 3;
        std::vector<GpuPickRequest> gpu_pick_requests(
            static_cast<std::size_t>(viewport_manager.viewport_count())
        );
        std::uint32_t gpu_pick_frame_slot = 0;
        const bool benchmark_pick_enabled =
            config_.benchmark_mode &&
            !config_.benchmark_pick_script_path.empty();
        const auto benchmark_pick_queries =
            benchmark_pick_enabled
                ? load_benchmark_pick_script(
                      config_.benchmark_pick_script_path
                  )
                : std::vector<BenchmarkPickScriptQuery>{};
        std::vector<double> benchmark_pick_issue_cpu_ms(
            benchmark_pick_queries.size(),
            0.0
        );
        std::vector<BenchmarkPickIssuedMetadata> benchmark_pick_issue_metadata(
            benchmark_pick_queries.size()
        );
        std::vector<BenchmarkPickObservedResult> benchmark_pick_results;
        benchmark_pick_results.reserve(benchmark_pick_queries.size());
        std::size_t benchmark_pick_issue_index = 0;
        constexpr std::uint32_t kBenchmarkPickWarmupFrames = 80;
        std::uint32_t benchmark_target_frame_count =
            config_.benchmark_frame_count;
        if (benchmark_pick_enabled) {
            benchmark_target_frame_count = static_cast<std::uint32_t>(
                kBenchmarkPickWarmupFrames +
                benchmark_pick_queries.size() +
                static_cast<std::size_t>(renderer.frames_in_flight()) + 1
            );
        }

        gs3d::camera::CameraControllerConfig controller_config;
        controller_config.rotate_speed =
            config_.controller_rotate_speed;
        controller_config.pan_speed =
            config_.controller_pan_speed;
        controller_config.zoom_speed =
            config_.controller_zoom_speed;
        controller_config.invert_rotate_x =
            config_.controller_invert_rotate_x;
        controller_config.invert_rotate_y =
            config_.controller_invert_rotate_y;
        controller_config.invert_pan_x =
            config_.controller_invert_pan_x;
        controller_config.invert_pan_y =
            config_.controller_invert_pan_y;

        // One controller per viewport so each can be interacted with independently.
        std::vector<gs3d::camera::CameraController> controllers;
        controllers.reserve(
            static_cast<std::size_t>(viewport_manager.viewport_count())
        );
        for (int i = 0; i < viewport_manager.viewport_count(); ++i) {
            controllers.emplace_back(controller_config);
            controllers.back().set_bounds(bounds);
        }
        std::vector<std::optional<gs3d::camera::Vec3>>
            selected_focus_points(
                static_cast<std::size_t>(viewport_manager.viewport_count())
            );

        // Per-viewport previous-frame rotate state for rotate_begin detection.
        std::vector<bool> prev_rotate(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            false
        );

        // Per-viewport click-vs-drag tracking: rotation pivot is only locked
        // after the cursor moves ≥ kRotateActivationPx from the button-down
        // position.  Pure clicks (press + release without drag) skip rotation
        // entirely — leaves camera unchanged and reserves left-click for
        // future point-selection features.
        constexpr float kRotateActivationPx = 5.0f;
        std::vector<float> mouse_down_x(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            0.0f
        );
        std::vector<float> mouse_down_y(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            0.0f
        );
        std::vector<bool> rotation_activated(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            false
        );

        // Views start independent. The per-view UI can opt into sync group 0.
        gs3d::camera::CameraHub camera_hub;
        for (int i = 0; i < viewport_manager.viewport_count(); ++i) {
            camera_hub.add(
                i,
                &viewport_manager.camera(i),
                gs3d::camera::CameraHub::kIndependent
            );
        }

        std::cout << "[OK] CameraController initialized.\n";
        std::cout << "camera position = ["
                  << viewport_manager.camera(0).position().x << ", "
                  << viewport_manager.camera(0).position().y << ", "
                  << viewport_manager.camera(0).position().z << "]\n";

        std::cout << "camera target = ["
                  << viewport_manager.camera(0).target().x << ", "
                  << viewport_manager.camera(0).target().y << ", "
                  << viewport_manager.camera(0).target().z << "]\n";

        std::cout << "camera distance = "
                  << viewport_manager.camera(0).distance() << '\n';

        gs3d::render::PointPushConstants push{};
        push.point_size  = config_.initial_point_size;
        // Channel attributes set below after attr_list is built.
        // MVP is set per-viewport inside render_all; flags is zero-initialized.

        /*
         * Tracks the query box of the tile buffer currently on the GPU.
         * Used to clip LOD draws so LOD points don't overlap full-res tiles.
         * Reset when the tile buffer is cleared.
         */
        std::array<
            std::vector<std::uint64_t>,
            kMaxViewportCount
        > viewport_tile_ids;
        std::array<
            std::optional<gs3d::data::Gs3dTileQueryBox>,
            kMaxViewportCount
        > viewport_tile_query_boxes;

        /*
         * 异步磁盘读取（Potree/Cesium 模式）：
         * 后台线程读取 tile 数据，主线程每帧非阻塞检查 future 是否完成。
         * GPU upload 仍在主线程，调用前用 in-flight fence 代替 vkDeviceWaitIdle。
         */
        struct TileLoadResult {
            std::vector<std::uint64_t>          tile_ids;
            std::vector<std::pair<
                std::uint64_t,
                SharedTilePoints
            >>                                  tiles;
            gs3d::data::Gs3dTileQueryBox        actual_bbox;
            double                              read_seconds = 0.0;
            std::size_t                         cache_hit_tiles = 0;
            std::size_t                         cache_miss_tiles = 0;
            std::size_t                         candidate_tiles = 0;
        };

        TilePointCache tile_point_cache(
            config_.tile_cpu_cache_max_bytes
        );
        std::future<TileLoadResult> tile_load_future;
        // IDs dispatched to background thread (may differ from current selection)
        std::vector<std::uint64_t> tile_loading_ids;
        std::vector<std::uint64_t> debounced_tile_ids;
        auto tile_selection_changed_at =
            std::chrono::steady_clock::now();
        // Debounce interacting so rapid scroll zoom doesn't cause
        // frame-by-frame toggling (tiles pop in/out, LOD clip flicker).
        auto interacting_debounce_until =
            std::chrono::steady_clock::now();
        gs3d::util::Stopwatch tile_async_cycle_timer;

        /*
         * 全量预加载状态(tile_preload_all):后台一次性读取全部瓦片,主循环
         * 用大预算渐进上传到 GPU 常驻;全部驻留后 tiles_fully_resident=true,
         * 之后走"每帧选择可见子集、零加载"的快路径,不再碰流式状态机。
         * 仅在所有瓦片总字节 <= tile_preload_max_bytes 时启用,否则保持 false
         * 走原有按需流式。
         */
        const bool tile_preload_enabled =
            config_.tile_enabled &&
            config_.tile_preload_all &&
            !config_.benchmark_mode &&
            tile_reader.has_value() &&
            !tile_reader->records().empty() &&
            tile_reader->stats().total_point_bytes <=
                config_.tile_preload_max_bytes;
        std::future<std::vector<std::pair<
            std::uint64_t, SharedTilePoints>>> tile_preload_future;
        std::vector<std::pair<std::uint64_t, SharedTilePoints>>
            tile_preload_tiles;
        bool tile_preload_dispatched = false;
        bool tile_preload_failed = false;
        bool tiles_fully_resident = false;
        int tile_preload_stall_frames = 0;
        gs3d::util::Stopwatch tile_preload_timer;

        const auto make_cached_tile_views =
            [](const std::vector<std::pair<std::uint64_t, SharedTilePoints>>&
                   tiles) {
                std::vector<std::pair<
                    std::uint64_t,
                    gs3d::core::PointDataView
                >> views;
                views.reserve(tiles.size());
                for (const auto& [tile_id, points] : tiles) {
                    if (!points) {
                        continue;
                    }
                    views.emplace_back(
                        tile_id,
                        gs3d::data::make_point_data_view(
                            points->points,
                            points->point_ids.data()
                        )
                    );
                }
                return views;
            };

        const auto collect_visible_hover_tile_views =
            [&viewport_tile_ids,
             &tile_point_cache,
             &tile_preload_tiles,
             &tiles_fully_resident](std::size_t view_index) {
                std::vector<gs3d::core::PointDataView> views;
                if (view_index >= viewport_tile_ids.size()) {
                    return views;
                }

                const auto& tile_ids = viewport_tile_ids[view_index];
                views.reserve(tile_ids.size());
                for (const auto tile_id : tile_ids) {
                    SharedTilePoints points = tile_point_cache.find(tile_id);
                    if (!points && tiles_fully_resident) {
                        const auto found = std::find_if(
                            tile_preload_tiles.begin(),
                            tile_preload_tiles.end(),
                            [tile_id](
                                const std::pair<
                                    std::uint64_t,
                                    SharedTilePoints
                                >& entry
                            ) {
                                return entry.first == tile_id;
                            }
                        );
                        if (found != tile_preload_tiles.end()) {
                            points = found->second;
                        }
                    }

                    if (!points || points->points.empty() ||
                        points->point_ids.empty()) {
                        continue;
                    }

                    views.push_back(
                        gs3d::data::make_point_data_view(
                            points->points,
                            points->point_ids.data()
                        )
                    );
                }

                return views;
            };

        const auto resolve_hover_point_from_visible_tiles =
            [&collect_visible_hover_tile_views](
                std::size_t view_index,
                std::uint32_t point_id,
                float mouse_x,
                float mouse_y
            ) {
                static_cast<void>(mouse_x);
                static_cast<void>(mouse_y);
                const auto candidate_point_sets =
                    collect_visible_hover_tile_views(view_index);
                if (candidate_point_sets.empty()) {
                    return std::optional<gs3d::data::Gs3dPoint>{};
                }

                if (const auto exact =
                        find_point_by_id_in_views(
                            candidate_point_sets,
                            point_id
                        )) {
                    return exact;
                }
                return std::optional<gs3d::data::Gs3dPoint>{};
            };

        const auto load_tile_points_with_ids =
            [&tile_reader, &tile_point_ids_by_tile](std::uint64_t tile_id) {
                auto loaded = std::make_shared<TilePoints>();
                loaded->points =
                    tile_reader->read_tile_points(tile_id);
                const auto found =
                    tile_point_ids_by_tile.find(tile_id);
                if (found == tile_point_ids_by_tile.end()) {
                    throw std::runtime_error(
                        "ViewerApp: missing runtime tile point ids"
                    );
                }
                loaded->point_ids = found->second;
                if (loaded->points.size() != loaded->point_ids.size()) {
                    throw std::runtime_error(
                        "ViewerApp: tile point/id size mismatch"
                    );
                }
                return loaded;
            };

        // 从瓦片 id 列表算并集包围盒(供 clip 用)。
        const auto compute_tiles_bbox =
            [&tile_reader](const std::vector<std::uint64_t>& ids)
                -> gs3d::data::Gs3dTileQueryBox {
            gs3d::data::Gs3dTileQueryBox box;
            box.min_x = box.min_y = box.min_z =
                std::numeric_limits<float>::max();
            box.max_x = box.max_y = box.max_z =
                -std::numeric_limits<float>::max();
            for (const auto tile_id : ids) {
                const auto& r = tile_reader->record(tile_id);
                box.min_x = std::min(box.min_x, r.bbox_min_x);
                box.min_y = std::min(box.min_y, r.bbox_min_y);
                box.min_z = std::min(box.min_z, r.bbox_min_z);
                box.max_x = std::max(box.max_x, r.bbox_max_x);
                box.max_y = std::max(box.max_y, r.bbox_max_y);
                box.max_z = std::max(box.max_z, r.bbox_max_z);
            }
            return box;
        };

        ViewportResizeScheduler viewport_resize_scheduler(0.15);

        // Benchmark-mode instrumentation (no-ops when benchmark_mode is false).
        std::uint64_t app_frame_index = 0;
        std::uint32_t benchmark_frame_index = 0;
        std::vector<double> benchmark_wall_frame_times_ms;
        std::vector<double> benchmark_cpu_frame_times_ms;
        std::vector<double> benchmark_gpu_frame_times_ms;
        std::vector<double> benchmark_camera_update_ms;
        std::vector<double> benchmark_lod_tile_select_ms;
        std::vector<double> benchmark_cpu_cull_ms;
        std::vector<double> benchmark_upload_record_ms;
        std::vector<double> benchmark_draw_record_ms;
        std::vector<double> benchmark_acquire_wait_ms;
        std::vector<double> benchmark_frame_fence_wait_ms;
        std::vector<double> benchmark_upload_fence_wait_ms;
        std::vector<double> benchmark_reload_seconds;
        if (config_.benchmark_mode) {
            benchmark_wall_frame_times_ms.reserve(config_.benchmark_frame_count);
            benchmark_cpu_frame_times_ms.reserve(config_.benchmark_frame_count);
            benchmark_gpu_frame_times_ms.reserve(config_.benchmark_frame_count);
            benchmark_camera_update_ms.reserve(config_.benchmark_frame_count);
            benchmark_lod_tile_select_ms.reserve(config_.benchmark_frame_count);
            benchmark_cpu_cull_ms.reserve(config_.benchmark_frame_count);
            benchmark_upload_record_ms.reserve(config_.benchmark_frame_count);
            benchmark_draw_record_ms.reserve(config_.benchmark_frame_count);
            benchmark_acquire_wait_ms.reserve(config_.benchmark_frame_count);
            benchmark_frame_fence_wait_ms.reserve(config_.benchmark_frame_count);
            benchmark_upload_fence_wait_ms.reserve(config_.benchmark_frame_count);
        }
        // Orbit for the first 2/3 of the run (measures interaction frame
        // time), then hold still for the last 1/3 (measures tile/LOD
        // settle/reload latency once the camera stops moving).
        const std::uint32_t benchmark_orbit_frames =
            config_.benchmark_frame_count * 2 / 3;

        gs3d::render::LodSelector lod_selector;

        if (config_.lod_enabled) {
            gs3d::render::LodSelectorConfig lod_selector_config;
            lod_selector_config.medium_delay_seconds =
                config_.lod_medium_delay_seconds;
            lod_selector_config.high_delay_seconds =
                config_.lod_high_delay_seconds;
            lod_selector_config.use_lowest_while_interacting =
                config_.lod_use_lowest_while_interacting;
            lod_selector_config.adaptive_interacting_level =
                config_.lod_adaptive_interacting_level;
            lod_selector_config.frame_time_budget_ms =
                config_.lod_frame_time_budget_ms;

            lod_selector.set_config(lod_selector_config);
        }

        gs3d::render::TileSelection tile_selection;
        gs3d::render::TileSelectionResult tile_result;
        bool tile_selection_dirty = true;
        int streaming_viewport_index = 0;

        if (config_.tile_enabled && tile_reader.has_value()) {
            tile_selection.set_config(
                make_tile_selection_config(config_)
            );

            tile_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudTileGpu>();
            tile_gpu_cloud->set_resident_tile_budget(
                config_.tile_gpu_cache_max_tiles
            );

            std::cout << "[OK] TileSelection initialized.\n";
        }

        bool r_was_pressed = false;
        bool f_was_pressed = false;
        bool tab_was_pressed = false;
        bool shift_tab_was_pressed = false;

        const std::string primary_value_name =
            config_.primary_value_field_name.empty()
                ? "value"
                : config_.primary_value_field_name;
        const std::string z_field_name =
            config_.z_field_name.empty()
                ? "z"
                : config_.z_field_name;

        // Logical names come from preprocessing; physical slots stay Value/Z.
        const std::vector<gs3d::app::AttrDescriptor> attr_list = {
            { primary_value_name,
              gs3d::app::AttrPhysicalSource::Value,
              dataset.value_min(),
              dataset.value_max() },
            { z_field_name,
              gs3d::app::AttrPhysicalSource::Z,
              dataset.bbox_min_z(),
              dataset.bbox_max_z() }
        };

        // 高程范围 — 用于非空间属性映射到物理 Z 坐标时的基准。
        const float elev_min   = dataset.bbox_min_z();
        const float elev_range = dataset.bbox_max_z() - dataset.bbox_min_z();

        // 当前高度夸张系数（跨源持久）。
        float height_exag = 1.0f;

        // 根据属性描述 + 夸张系数计算 height_offset / height_mult。
        auto apply_height_attr = [&](const gs3d::app::AttrDescriptor& a, float exag) {
            push.height_source = static_cast<std::uint32_t>(a.source);
            if (a.source == gs3d::app::AttrPhysicalSource::Z) {
                // 高程值已在空间尺度，stretch around origin
                push.height_mult   = exag;
                push.height_offset = 0.0f;
            } else {
                // 非空间属性 → 线性映射到 [elev_min, elev_min + elev_range * exag]
                const float r = a.range();
                const float m = (r > 0.0f) ? (elev_range / r * exag) : exag;
                push.height_mult   = m;
                push.height_offset = elev_min - a.min_val * m;
            }
        };

        // 初始化默认通道：颜色=fold (attr_list[0]), 高度=高程 (attr_list[1])
        {
            const auto& c = attr_list[0];
            push.color_source = static_cast<std::uint32_t>(c.source);
            push.color_min    = c.min_val;
            push.color_range  = c.range();
            if (push.color_range <= 0.0f) push.color_range = 1.0f;
        }
        apply_height_attr(attr_list[1], height_exag);

        std::size_t last_lod_level =
             static_cast<std::size_t>(-1);
        // Hoisted out of the loop body so report_frame_time() can pair the
        // level rendered in frame N-1 with frame N-1's measured duration
        // (delta_seconds, computed at the top of frame N) before this
        // frame reassigns it.
        std::size_t lod_level_for_frame = 0;

        /*
         * 安全网：最后一次确认可用的 LOD 级别。select_level() 理论上
         * 永远返回有效值（LOD GPU cloud 所有级别都是启动时预上传的），
         * 但万一出现越界或无效索引，退回到 last_valid 而不是画空帧。
         */
        std::size_t last_valid_lod_level = 0;

        /*
         * KeepStableHighQuality 模式下,交互期间冻结的显示档位。空闲时持续
         * 刷新为当前稳定显示的档位(会收敛到 level 0 = 最高细节);交互开始
         * 时锁定该值,交互途中绝不切到比它更粗的档位。lower index = 更精细。
         */
        std::size_t frozen_display_lod = 0;

        /*
         * 空间选层的上一帧结果（用于滞回）。初始化为 level_count
         * 表示"无前值"，首帧不出滞回。
         */
        std::size_t last_spatial_level =
            static_cast<std::size_t>(-1);

        /*
         * 交互期间冻结的空间目标层。交互中不跟踪 ortho_height 变化，
         * 停手后才更新——与 tile 选择的 !interacting 门控语义一致，
         * 避免缩放中 LOD 硬切导致抽稀跳变。
         */
        std::size_t frozen_spatial_level =
            static_cast<std::size_t>(-1);

        const auto log_tile_upload =
            [this](
                const gs3d::render::PointCloudTileGpuStats& stats,
                const gs3d::render::PointCloudTileGpuSyncResult& sync,
                double total_seconds
            ) {
                if (!config_.tile_verbose) {
                    return;
                }

                std::cout << "[TILE] upload complete (all desired tiles resident).\n";
                std::cout << "tile_count = "
                          << stats.tile_count << '\n';
                std::cout << "point_count = "
                          << stats.point_count << '\n';
                std::cout << "gpu_buffer_bytes = "
                          << stats.gpu_buffer_bytes << '\n';
                std::cout << "resident_tile_count = "
                          << stats.resident_tile_count << '\n';
                std::cout << "uploaded_tile_count = "
                          << sync.uploaded_tile_count << '\n';
                std::cout << "uploaded_point_count = "
                          << sync.uploaded_point_count << '\n';
                std::cout << "uploaded_bytes = "
                          << sync.uploaded_bytes << '\n';
                std::cout << "[TIME] tile.async_total_seconds = "
                          << total_seconds << '\n';
            };

        const auto print_benchmark_percentiles =
            [](const char* name, const std::vector<double>& samples) {
                if (samples.empty()) {
                    std::cout << "[BENCH] " << name
                              << ": no samples\n";
                    return;
                }

                std::cout << "[BENCH] " << name << "_p50 = "
                          << gs3d::util::percentile(samples, 50.0)
                          << '\n';
                std::cout << "[BENCH] " << name << "_p95 = "
                          << gs3d::util::percentile(samples, 95.0)
                          << '\n';
                std::cout << "[BENCH] " << name << "_p99 = "
                          << gs3d::util::percentile(samples, 99.0)
                          << '\n';
            };

        auto previous_time =
            std::chrono::steady_clock::now();

        // Smoothed FPS via exponential moving average
        float fps_smooth = 0.0f;

        print_controls(
                        config_.lod_enabled,
                        config_.tile_enabled
                    );

        gs3d::core::DatasetDescriptor dataset_descriptor;
        dataset_descriptor.display_name =
            config_.gs3d_path.filename().string();
        dataset_descriptor.path = config_.gs3d_path.string();
        dataset_descriptor.format = "GS3D";
        dataset_descriptor.point_count = dataset.point_count();
        dataset_descriptor.bounds = make_dataset_bounds(dataset);
        dataset_descriptor.dataset_tree = {
            dataset_descriptor.display_name,
            "瓦片",
            "细节层级",
            "属性"
        };
        dataset_descriptor.attributes.clear();
        for (const auto& attr : attr_list) {
            dataset_descriptor.attributes.push_back(
                gs3d::core::AttributeDescriptor{attr.name});
        }
        {
            std::error_code ec;
            const auto file_bytes = std::filesystem::file_size(config_.gs3d_path, ec);
            if (!ec) {
                const double mb = static_cast<double>(file_bytes) / (1024.0 * 1024.0);
                std::ostringstream oss;
                oss.setf(std::ios::fixed);
                oss.precision(2);
                oss << mb << " MB";
                dataset_descriptor.file_size = oss.str();
            }
        }
        gs3d::scene::SceneState scene_state;
        scene_state.active_dataset = &dataset_descriptor;
        scene_state.active_attribute_index = 0;  // 颜色=fold (attr_list[0])
        scene_state.active_height_index    = 1;  // 高度=高程 (attr_list[1])
        gs3d::app::AppState app_state;
        app_state.dataset.active_dataset = dataset_descriptor.display_name;
        app_state.dataset.path = dataset_descriptor.path;
        app_state.dataset.format = dataset_descriptor.format;
        app_state.dataset.point_count = dataset_descriptor.point_count;
        app_state.dataset.loaded_points = dataset_descriptor.point_count;
        app_state.dataset.file_size = dataset_descriptor.file_size;
        app_state.dataset.bounding_box =
            format_bounds_label(dataset_descriptor.bounds);
        app_state.dataset.dataset_tree = dataset_descriptor.dataset_tree;
        app_state.dataset.attributes.clear();
        app_state.render_settings.height_by_options.clear();
        app_state.render_settings.color_by_options.clear();
        for (const auto& attr : attr_list) {
            app_state.dataset.attributes.push_back(attr.name);
            app_state.render_settings.height_by_options.push_back(attr.name);
            app_state.render_settings.color_by_options.push_back(attr.name);
        }
        app_state.render_views.resize(
            static_cast<std::size_t>(viewport_manager.viewport_count())
        );
        const int startup_view_count =
            std::clamp(config_.viewport_count, 1, kMaxViewportCount);
        for (int i = 0; i < viewport_manager.viewport_count(); ++i) {
            auto& view =
                app_state.render_views[static_cast<std::size_t>(i)];
            view.viewport_index = i;
            view.visible = i < startup_view_count;
            view.camera_linked = false;
        }
        if (config_.benchmark_mode) {
            app_state.panels.dataset = false;
            app_state.panels.render_settings = false;
            app_state.panels.debug_log = false;
            app_state.panels.tile_inspector = false;
            app_state.panels.lod_view = false;
            app_state.panels.performance = false;
        }

        // ── 导航图缩略图：离屏预渲染到独立 framebuffer ─────────────────
        // bbox 宽高比决定纹理尺寸，保证纹理像素全部有效，无 letterbox。
        const float nav_bbox_w =
            dataset.bbox_max_x() - dataset.bbox_min_x();
        const float nav_bbox_h_ =
            dataset.bbox_max_y() - dataset.bbox_min_y();
        const float nav_aspect =
            nav_bbox_h_ > 0.0f ? nav_bbox_w / nav_bbox_h_ : 1.0f;
        constexpr float kNavBaseSize = 256.0f;
        std::uint32_t nav_tex_w = kNavBaseSize;
        std::uint32_t nav_tex_h = kNavBaseSize;
        if (nav_aspect >= 1.0f) {
            nav_tex_h = std::max(
                64u,
                static_cast<std::uint32_t>(kNavBaseSize / nav_aspect)
            );
        } else {
            nav_tex_w = std::max(
                64u,
                static_cast<std::uint32_t>(kNavBaseSize * nav_aspect)
            );
        }

        gs3d::render::OffscreenFramebuffer nav_thumbnail_fb;
        nav_thumbnail_fb.create(
            context,
            VkExtent2D{nav_tex_w, nav_tex_h},
            swapchain.image_format()
        );

        // 坐标系映射：纹理像素 ↔ 数据集 XY 包围盒，只在这里写一次。
        // 缩略图渲染相机和视野框绘制共用这组参数。
        auto& nm = app_state.navigation_map;
        nm.tex_w = static_cast<float>(nav_tex_w);
        nm.tex_h = static_cast<float>(nav_tex_h);
        nm.bbox_min_x = dataset.bbox_min_x();
        nm.bbox_min_y = dataset.bbox_min_y();
        nm.bbox_max_x = dataset.bbox_max_x();
        nm.bbox_max_y = dataset.bbox_max_y();

        // 单次命令缓冲区：渲染缩略图
        {
            VkCommandBufferAllocateInfo alloc_info{};
            alloc_info.sType =
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc_info.commandPool = renderer.command_pool();
            alloc_info.commandBufferCount = 1;

            VkCommandBuffer cmd = VK_NULL_HANDLE;
            vkAllocateCommandBuffers(
                context.device(),
                &alloc_info,
                &cmd
            );

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType =
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags =
                VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &begin_info);

            // 正交俯视相机：从上往下看，覆盖整个 bbox 的 XY 范围
            gs3d::camera::Camera nav_cam;
            nav_cam.set_viewport(nav_tex_w, nav_tex_h);
            nav_cam.set_orthographic(
                nav_bbox_h_,
                0.001f,
                std::max(1.0f, nav_bbox_h_ * 10.0f)
            );
            const float nav_cx =
                (nm.bbox_min_x + nm.bbox_max_x) * 0.5f;
            const float nav_cy =
                (nm.bbox_min_y + nm.bbox_max_y) * 0.5f;
            nav_cam.look_at(
                {nav_cx, nav_cy, dataset.bbox_max_z() + nav_bbox_h_},
                {nav_cx, nav_cy, 0.0f},
                {0.0f, 1.0f, 0.0f}
            );

            const auto nav_mvp = nav_cam.view_projection_matrix();
            gs3d::render::PointPushConstants nav_push = push;
            std::memcpy(nav_push.mvp, nav_mvp.data(), sizeof(nav_push.mvp));

            const auto& nav_cloud =
                lod_gpu_cloud
                    ? lod_gpu_cloud->lowest_detail().gpu_cloud
                    : *full_gpu_cloud;

            nav_thumbnail_fb.render(
                cmd,
                [&](VkCommandBuffer cb) {
                    point_pipeline.draw(
                        cb,
                        nav_cloud,
                        VkExtent2D{nav_tex_w, nav_tex_h},
                        nav_push
                    );
                }
            );

            vkEndCommandBuffer(cmd);

            VkFence fence = VK_NULL_HANDLE;
            VkFenceCreateInfo fence_info{};
            fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            vkCreateFence(context.device(), &fence_info, nullptr, &fence);

            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &cmd;
            vkQueueSubmit(
                context.graphics_queue(),
                1,
                &submit_info,
                fence
            );

            vkWaitForFences(context.device(), 1, &fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(context.device(), fence, nullptr);
            vkFreeCommandBuffers(
                context.device(),
                renderer.command_pool(),
                1,
                &cmd
            );
        }

        nm.texture_descriptor = nav_thumbnail_fb.imgui_descriptor();
        nm.valid = true;
        nm.dirty = false;

        std::vector<int> visible_viewports;
        visible_viewports.reserve(
            static_cast<std::size_t>(viewport_manager.viewport_count())
        );

        const auto consume_ready_pick_frame_slot =
            [&](std::uint32_t frame_slot) {
                gpu_pick_frame_slot = frame_slot;
                if (config_.pick_debug_dump_enabled) {
                    const auto debug_dump =
                        pick_debug_frame_dumper.collect_ready_frame(frame_slot);
                    if (debug_dump) {
                        write_pick_debug_dump(
                            config_.pick_debug_dump_dir,
                            *debug_dump
                        );
                    }
                }

                gs3d::util::Stopwatch collect_timer;
                const auto pick_results =
                    gpu_pick_readback.collect_ready_frame(frame_slot);
                const double collect_cpu_ms =
                    collect_timer.elapsed_milliseconds();
                for (const auto& result : pick_results) {
                    if (result.request.viewport_index < 0 ||
                        result.request.viewport_index >=
                            static_cast<int>(
                                latest_gpu_hover_points.size()
                            )) {
                        continue;
                    }

                    std::optional<gs3d::data::Gs3dPoint> hit_point;
                    bool lookup_ok = false;
                    if (result.has_hit &&
                        result.point_id < runtime_points_by_id.size() &&
                        runtime_points_valid_by_id[result.point_id] != 0) {
                        hit_point =
                            runtime_points_by_id[result.point_id];
                        lookup_ok = true;
                    }

                    const auto view_index =
                        static_cast<std::size_t>(
                            result.request.viewport_index
                        );
                    const char* hover_resolve_path =
                        lookup_ok ? "runtime_lookup" : "miss";
                    if ((result.request.kind ==
                             GpuPickRequestKind::Hover ||
                         result.request.kind ==
                             GpuPickRequestKind::SetOrbitPivot) &&
                        result.has_hit &&
                        !lookup_ok) {
                        if (const auto resolved =
                                resolve_hover_point_from_visible_tiles(
                                    view_index,
                                    result.point_id,
                                    result.request.mouse_x,
                                    result.request.mouse_y
                                )) {
                            hit_point = *resolved;
                            lookup_ok = true;
                            hover_resolve_path = "resident_tile_fallback";
                        } else {
                            hover_resolve_path =
                                "resident_tile_fallback_miss";
                        }
                    }
                    if (result.request.kind ==
                        GpuPickRequestKind::Hover) {
                        // Only overwrite when we actually resolved a
                        // point.  A GPU hit whose runtime lookup failed
                        // must NOT null out valid data from a previous
                        // successful hit — otherwise the tooltip
                        // flickers or disappears when hovering over
                        // points whose IDs are not in the lookup table.
                        if (hit_point.has_value()) {
                            latest_gpu_hover_points[view_index] = hit_point;
                            hover_timeout[view_index] = 0;
                            consecutive_no_hit[view_index] = 0;
                        }
                        // has_hit=false means GPU found no point at the
                        // cursor — the mouse is over empty space.
                        // Debounce across a few frames so that moving
                        // between nearby points (where the GPU pick lags
                        // behind the cursor) doesn't flicker the tooltip.
                        if (!result.has_hit &&
                            view_index < consecutive_no_hit.size()) {
                            ++consecutive_no_hit[view_index];
                            if (consecutive_no_hit[view_index] >=
                                    kNoHitClearThreshold &&
                                view_index <
                                    latest_gpu_hover_points.size()) {
                                latest_gpu_hover_points[view_index].reset();
                            }
                        }
                        if (result.has_hit &&
                            view_index < consecutive_no_hit.size()) {
                            consecutive_no_hit[view_index] = 0;
                        }
                        latest_gpu_capture_x[view_index] =
                            result.request.mouse_x;
                        latest_gpu_capture_y[view_index] =
                            result.request.mouse_y;
                        if (benchmark_pick_enabled &&
                            result.request.benchmark_query_index >= 0) {
                            const auto query_index =
                                static_cast<std::size_t>(
                                    result.request.benchmark_query_index
                                );
                            BenchmarkPickObservedResult observed;
                            observed.query_index = query_index;
                            observed.has_hit = hit_point.has_value();
                            observed.gpu_has_hit = result.has_hit;
                            observed.point_id = result.point_id;
                            observed.depth = result.depth;
                            observed.issue_cpu_ms =
                                query_index <
                                        benchmark_pick_issue_cpu_ms.size()
                                    ? benchmark_pick_issue_cpu_ms[
                                          query_index
                                      ]
                                    : 0.0;
                            observed.collect_cpu_ms = collect_cpu_ms;
                            if (query_index <
                                benchmark_pick_issue_metadata.size()) {
                                observed.all_tiles_resident =
                                    benchmark_pick_issue_metadata[
                                        query_index
                                    ].all_tiles_resident;
                                observed.resident_tile_ids =
                                    benchmark_pick_issue_metadata[
                                        query_index
                                    ].resident_tile_ids;
                            }
                            if (hit_point) {
                                observed.x = hit_point->x;
                                observed.y = hit_point->y;
                                observed.z = hit_point->z;
                                observed.value = hit_point->value;
                            }
                            benchmark_pick_results.push_back(observed);
                        }
                        continue;
                    }

                    if (result.request.kind ==
                        GpuPickRequestKind::SetOrbitPivot) {
                        if (!hit_point.has_value()) {
                            controllers[view_index].clear_orbit_pivot();
                            selected_focus_points[view_index].reset();
                            std::cout
                                << "[CAMERA] orbit pivot cleared"
                                << " (double-clicked empty space)\n";
                            continue;
                        }

                        float raw = hit_point->z;
                        if (push.height_source ==
                            static_cast<std::uint32_t>(
                                gs3d::app::AttrPhysicalSource::Value)) {
                            raw = hit_point->value;
                        }
                        float mapped_z =
                            push.height_offset + raw * push.height_mult;

                        const gs3d::camera::Vec3 selected_point{
                            hit_point->x,
                            hit_point->y,
                            mapped_z
                        };
                        controllers[view_index].set_orbit_pivot(
                            selected_point
                        );
                        selected_focus_points[view_index] = selected_point;
                        streaming_viewport_index =
                            result.request.viewport_index;
                        std::cout
                            << "[CAMERA] orbit pivot selected at ["
                            << selected_point.x << ", "
                            << selected_point.y << ", "
                            << selected_point.z << "]\n";
                        continue;
                    }

                    if (result.request.kind !=
                        GpuPickRequestKind::BoxSelectAnchor) {
                        continue;
                    }

                    // Use the camera's own viewport so NDC conversion
                    // and the projection matrix use the same dimensions.
                    const gs3d::camera::Viewport mouse_viewport{
                        result.request.anchor_camera.viewport_width(),
                        result.request.anchor_camera.viewport_height()
                    };
                    const float plane_z =
                        hit_point
                            ? hit_point->z
                            : result.request.anchor_camera.target().z;
                    const auto selection_bounds =
                        gs3d::camera::box_select_world_bounds(
                            result.request.box_select_min_x,
                            result.request.box_select_min_y,
                            result.request.box_select_max_x,
                            result.request.box_select_max_y,
                            mouse_viewport,
                            result.request.anchor_camera,
                            bounds,
                            plane_z
                        );
                    if (!selection_bounds) {
                        continue;
                    }

                    // --- diagnostic: box-select full trace ---
                    constexpr bool kBoxFitDiag = false;  // set true to enable
                    const auto& ac = result.request.anchor_camera;
                    const float fb_min_x = result.request.box_select_min_x;
                    const float fb_min_y = result.request.box_select_min_y;
                    const float fb_max_x = result.request.box_select_max_x;
                    const float fb_max_y = result.request.box_select_max_y;

                    if (kBoxFitDiag) {
                        const auto& bc = ac;
                        std::fprintf(stderr,
                            "[BOXFIT] ========================================\n");
                        std::fprintf(stderr,
                            "[BOXFIT] fb_rect=(%.1f,%.1f)-(%.1f,%.1f) "
                            "fb_size=(%.1f,%.1f) fb_center=(%.1f,%.1f)\n",
                            static_cast<double>(fb_min_x),
                            static_cast<double>(fb_min_y),
                            static_cast<double>(fb_max_x),
                            static_cast<double>(fb_max_y),
                            static_cast<double>(fb_max_x - fb_min_x),
                            static_cast<double>(fb_max_y - fb_min_y),
                            static_cast<double>(0.5*(fb_min_x + fb_max_x)),
                            static_cast<double>(0.5*(fb_min_y + fb_max_y)));
                        std::fprintf(stderr,
                            "[BOXFIT] mouse_vp=%ux%u cam_vp=%ux%u "
                            "plane_z=%.2f vp_idx=%d\n",
                            mouse_viewport.width, mouse_viewport.height,
                            ac.viewport_width(), ac.viewport_height(),
                            static_cast<double>(plane_z),
                            result.request.viewport_index);
                        std::fprintf(stderr,
                            "[BOXFIT] cam_BEFORE: pos=(%.2f,%.2f,%.2f) "
                            "tgt=(%.2f,%.2f,%.2f) ortho_h=%.2f aspect=%.4f "
                            "near=%.4f far=%.2f\n",
                            static_cast<double>(bc.position().x),
                            static_cast<double>(bc.position().y),
                            static_cast<double>(bc.position().z),
                            static_cast<double>(bc.target().x),
                            static_cast<double>(bc.target().y),
                            static_cast<double>(bc.target().z),
                            static_cast<double>(bc.ortho_height()),
                            static_cast<double>(bc.aspect_ratio()),
                            static_cast<double>(bc.near_plane()),
                            static_cast<double>(bc.far_plane()));

                        // Print each corner's from_screen result
                        const std::array<std::pair<double,double>, 4> dbg_corners{{
                            {static_cast<double>(fb_min_x), static_cast<double>(fb_min_y)},
                            {static_cast<double>(fb_max_x), static_cast<double>(fb_min_y)},
                            {static_cast<double>(fb_min_x), static_cast<double>(fb_max_y)},
                            {static_cast<double>(fb_max_x), static_cast<double>(fb_max_y)},
                        }};
                        const char* dbg_names[] = {"TL","TR","BL","BR"};
                        for (int ci = 0; ci < 4; ++ci) {
                            auto dbg_ray = gs3d::camera::MouseRay::from_screen(
                                dbg_corners[ci].first, dbg_corners[ci].second,
                                mouse_viewport, ac);
                            auto dbg_hit = gs3d::camera::MouseRay::intersect_plane(
                                dbg_ray, {0,0,ac.target().z}, {0,0,1});
                            std::fprintf(stderr,
                                "[BOXFIT] corner[%s] scr=(%.1f,%.1f) "
                                "ray_org=(%.2f,%.2f,%.2f) ray_dir=(%.4f,%.4f,%.4f) "
                                "hit_z=%.2f %s",
                                dbg_names[ci],
                                dbg_corners[ci].first, dbg_corners[ci].second,
                                static_cast<double>(dbg_ray.origin.x),
                                static_cast<double>(dbg_ray.origin.y),
                                static_cast<double>(dbg_ray.origin.z),
                                static_cast<double>(dbg_ray.direction.x),
                                static_cast<double>(dbg_ray.direction.y),
                                static_cast<double>(dbg_ray.direction.z),
                                static_cast<double>(ac.target().z),
                                dbg_hit ? "hit" : "miss");
                            if (dbg_hit) std::fprintf(stderr,
                                "=(%.2f,%.2f,%.2f)",
                                static_cast<double>(dbg_hit->x),
                                static_cast<double>(dbg_hit->y),
                                static_cast<double>(dbg_hit->z));
                            std::fprintf(stderr, "\n");
                        }
                    }

                    auto& box_camera =
                        viewport_manager.camera(
                            result.request.viewport_index
                        );

                    // Save current state, run fit_screen_rect on the
                    // real camera (so the ray uses the correct viewport
                    // and old projection — see fit_screen_rect fix),
                    // then restore and animate toward the desired state.
                    const auto saved_pos = box_camera.position();
                    const auto saved_target = box_camera.target();
                    const float saved_ortho_h = box_camera.ortho_height();

                    box_camera.fit_screen_rect(
                        fb_min_x, fb_min_y,
                        fb_max_x, fb_max_y,
                        mouse_viewport.width, mouse_viewport.height,
                        1.05f);

                    const auto desired_target = box_camera.target();
                    const float desired_ortho_h = box_camera.ortho_height();
                    const auto desired_pos = box_camera.position();
                    const float desired_near = box_camera.near_plane();
                    const float desired_far = box_camera.far_plane();
                    const auto desired_up = box_camera.up();

                    // Restore and animate.
                    box_camera.look_at(
                        saved_pos, saved_target, box_camera.up());
                    box_camera.set_orthographic(
                        saved_ortho_h,
                        box_camera.near_plane(),
                        box_camera.far_plane());

                    controllers[
                        static_cast<std::size_t>(
                            result.request.viewport_index
                        )
                    ].animate_to(
                        box_camera,
                        desired_target,
                        desired_ortho_h
                    );

                    // --- expected box-zoom (screen-space formula) ---
                    if (kBoxFitDiag) {
                        const float old_ortho_h = ac.ortho_height();
                        const float vp_w = static_cast<float>(mouse_viewport.width);
                        const float vp_h = static_cast<float>(mouse_viewport.height);
                        const float rect_w = fb_max_x - fb_min_x;
                        const float rect_h = fb_max_y - fb_min_y;
                        const float scale_x = rect_w / std::max(vp_w, 1.0f);
                        const float scale_y = rect_h / std::max(vp_h, 1.0f);
                        const float expected_ortho_h =
                            old_ortho_h * std::max(scale_x, scale_y) * 1.05f;

                        const float ctr_x = 0.5f*(fb_min_x + fb_max_x);
                        const float ctr_y = 0.5f*(fb_min_y + fb_max_y);
                        auto ctr_ray = gs3d::camera::MouseRay::from_screen(
                            static_cast<double>(ctr_x),
                            static_cast<double>(ctr_y),
                            mouse_viewport, ac);
                        auto ctr_hit = gs3d::camera::MouseRay::intersect_plane(
                            ctr_ray, {0,0,ac.target().z}, {0,0,1});
                        float exp_tgt_x = ac.target().x;
                        float exp_tgt_y = ac.target().y;
                        if (ctr_hit) {
                            exp_tgt_x = ctr_hit->x;
                            exp_tgt_y = ctr_hit->y;
                        }

                        std::fprintf(stderr,
                            "[BOXFIT] EXPECTED: scale_x=%.4f scale_y=%.4f "
                            "ortho_h=%.2f target=(%.2f,%.2f,%.2f)\n",
                            static_cast<double>(scale_x),
                            static_cast<double>(scale_y),
                            static_cast<double>(expected_ortho_h),
                            static_cast<double>(exp_tgt_x),
                            static_cast<double>(exp_tgt_y),
                            static_cast<double>(ac.target().z));
                    }

                    if (kBoxFitDiag) {
                        std::fprintf(stderr,
                            "[BOXFIT] ACTUAL:   ortho_h=%.2f "
                            "target=(%.2f,%.2f,%.2f) "
                            "pos=(%.2f,%.2f,%.2f) "
                            "near=%.4f far=%.2f up=(%.2f,%.2f,%.2f)\n",
                            static_cast<double>(desired_ortho_h),
                            static_cast<double>(desired_target.x),
                            static_cast<double>(desired_target.y),
                            static_cast<double>(desired_target.z),
                            static_cast<double>(desired_pos.x),
                            static_cast<double>(desired_pos.y),
                            static_cast<double>(desired_pos.z),
                            static_cast<double>(desired_near),
                            static_cast<double>(desired_far),
                            static_cast<double>(desired_up.x),
                            static_cast<double>(desired_up.y),
                            static_cast<double>(desired_up.z));
                    }
                    streaming_viewport_index =
                        result.request.viewport_index;
                    tile_selection_dirty = true;
                }
            };

        // ponytail: screenshot staging — allocated on demand in post_pass, read
        // back after draw_frame. Only one screenshot at a time.
        VkBuffer screenshot_staging_buf = VK_NULL_HANDLE;
        VkDeviceMemory screenshot_staging_mem = VK_NULL_HANDLE;
        VkExtent2D screenshot_offset{};
        VkExtent2D screenshot_extent{};
        bool screenshot_pending = false;

        while (!window.should_close() &&
               (!config_.benchmark_mode ||
                benchmark_frame_index < benchmark_target_frame_count)) {
            gs3d::util::Stopwatch benchmark_frame_timer;
            double benchmark_cpu_frame_ms = 0.0;
            double benchmark_camera_update_ms_frame = 0.0;
            double benchmark_lod_tile_select_ms_frame = 0.0;
            double benchmark_cpu_cull_ms_frame = 0.0;
            double benchmark_upload_record_ms_frame = 0.0;
            const auto current_time =
                std::chrono::steady_clock::now();

            const double delta_seconds =
                std::chrono::duration<double>(
                    current_time - previous_time
                ).count();

            previous_time = current_time;

            for (std::uint32_t frame_slot = 0;
                 frame_slot < renderer.frames_in_flight();
                 ++frame_slot) {
                if (!renderer.is_frame_slot_ready(frame_slot)) {
                    continue;
                }
                consume_ready_pick_frame_slot(frame_slot);
            }

            // Pairs the level rendered last frame with its measured
            // duration, driving LodSelectorConfig::adaptive_interacting_level
            // (no-op otherwise). Must run before lod_level_for_frame is
            // reassigned for *this* frame, further down.
            if (config_.lod_enabled && delta_seconds > 0.0) {
                /*
                 * On VK_PRESENT_MODE_FIFO_KHR the total wall-clock frame
                 * time includes vsync present-wait inside vkAcquireNextImageKHR
                 * (~16.67ms at 60Hz).  Subtracting that wait gives a better
                 * estimate of actual GPU render time so the adaptive LOD
                 * budget (14ms) doesn't silently pin to the lowest level on
                 * FIFO-only systems (iGPU / older hardware).
                 */
                const double present_wait_ms =
                    renderer.last_acquire_wait_ms();
                const double report_ms =
                    (delta_seconds * 1000.0) - present_wait_ms;
                lod_selector.report_frame_time(
                    lod_level_for_frame,
                    report_ms > 0.0 ? report_ms : 0.0
                );
            }

            window.poll_events();

            // Update FPS (exponential moving average, α=0.1)
            if (delta_seconds > 0.0) {
                const float frame_fps =
                    static_cast<float>(1.0 / delta_seconds);
                fps_smooth = fps_smooth > 0.0f
                    ? fps_smooth * 0.9f + frame_fps * 0.1f
                    : frame_fps;
            }

            const int n_viewports = viewport_manager.viewport_count();
            const auto& primary_camera =
                viewport_manager.camera(streaming_viewport_index);

            std::uint32_t loaded_tiles = 0;
            // Pending = desired tiles not yet GPU-resident
            std::size_t pending_tile_count = 0;
            if (config_.tile_enabled && tile_gpu_cloud &&
                tile_result.enabled) {
                for (const auto tile_id : tile_result.tile_ids) {
                    if (!tile_gpu_cloud->has_resident_tile(tile_id)) {
                        ++pending_tile_count;
                    }
                }
            }
            if (tile_load_future.valid()) {
                pending_tile_count = std::max(
                    pending_tile_count,
                    tile_loading_ids.size());
            }
            const std::uint32_t pending_tiles =
                static_cast<std::uint32_t>(
                    std::min<std::size_t>(
                        pending_tile_count,
                        std::numeric_limits<std::uint32_t>::max()
                    )
                );
            std::uint64_t gpu_buffer_bytes = 0;
            std::uint64_t gpu_resident_points = 0;

            if (config_.tile_enabled && tile_gpu_cloud) {
                const auto& ts = tile_gpu_cloud->stats();
                loaded_tiles =
                    static_cast<std::uint32_t>(
                        ts.resident_tile_count
                    );
                gpu_buffer_bytes += ts.gpu_buffer_bytes;
                gpu_resident_points += ts.point_count;
            }
            if (full_gpu_cloud) {
                gpu_buffer_bytes +=
                    static_cast<std::uint64_t>(full_gpu_cloud->vertex_buffer_size());
                gpu_resident_points += full_gpu_cloud->point_count();
            }
            if (lod_gpu_cloud) {
                for (std::size_t i = 0; i < lod_gpu_cloud->level_count(); ++i) {
                    gpu_buffer_bytes += static_cast<std::uint64_t>(
                        lod_gpu_cloud->gpu_cloud(i).vertex_buffer_size()
                    );
                    gpu_resident_points +=
                        lod_gpu_cloud->level(i).gpu_point_count;
                }
            }

            // 视窗中实际可见的点数：瓦片模式下统计视锥体筛选后
            // 的瓦片点数和，非瓦片模式下使用 GPU 驻留点数。
            std::uint64_t visible_points = gpu_resident_points;
            if (tile_result.enabled && tile_reader.has_value()) {
                visible_points = 0;
                for (const auto tile_id : tile_result.tile_ids) {
                    visible_points +=
                        tile_reader->record(tile_id).point_count;
                }
            }

            app_state.dataset.point_count = dataset.point_count();
            app_state.dataset.loaded_points = gpu_resident_points;
            app_state.render_settings.point_size = push.point_size;
            app_state.render_settings.color_attr_index = scene_state.active_attribute_index;
            app_state.render_settings.height_attr_index = scene_state.active_height_index;
            app_state.render_settings.height_exaggeration = height_exag;
            app_state.render_settings.data_value_min = push.color_min;
            app_state.render_settings.data_value_max =
                push.color_min + push.color_range;
            app_state.render_settings.loaded_tiles = loaded_tiles;
            app_state.render_settings.pending_tiles = pending_tiles;
            app_state.render_settings.cache_usage =
                std::to_string(loaded_tiles) + " / " +
                std::to_string(config_.tile_gpu_cache_max_tiles);
            const auto tile_cache_stats = tile_point_cache.stats();
            app_state.render_settings.cpu_cache_usage =
                std::to_string(
                    tile_cache_stats.resident_bytes /
                    (1024ull * 1024ull)
                ) + " / " +
                std::to_string(
                    tile_cache_stats.max_bytes /
                    (1024ull * 1024ull)
                ) + " MB";
            const auto tile_cache_requests =
                tile_cache_stats.hits + tile_cache_stats.misses;
            app_state.render_settings.cache_hit_rate =
                tile_cache_requests > 0
                    ? 100.0f * static_cast<float>(
                        tile_cache_stats.hits
                    ) /
                        static_cast<float>(tile_cache_requests)
                    : 0.0f;

            app_state.performance.fps = fps_smooth;
            app_state.performance.frame_time_ms =
                delta_seconds > 0.0 ? static_cast<float>(delta_seconds * 1000.0) : 0.0f;
            app_state.performance.visible_points = visible_points;
            app_state.performance.total_points = dataset.point_count();
            app_state.performance.loaded_tiles = loaded_tiles;
            app_state.performance.pending_tiles = pending_tiles;
            app_state.performance.gpu_memory_bytes = gpu_buffer_bytes;
            app_state.performance.lod_mode =
                config_.lod_enabled
                    ? "已启用细节层级"
                    : "全分辨率";

            app_state.status_bar.fps = fps_smooth;
            app_state.status_bar.visible_points = visible_points;
            app_state.status_bar.loaded_tiles = loaded_tiles;
            app_state.status_bar.pending_tiles = pending_tiles;
            app_state.status_bar.gpu_memory_bytes = gpu_buffer_bytes;
            app_state.status_bar.camera_position = format_vec3_text(primary_camera.position());
            app_state.status_bar.crs = "本地坐标 / 未知";
            app_state.status_bar.ready_state = "就绪";

            for (int i = 0; i < n_viewports; ++i) {
                const auto& camera = viewport_manager.camera(i);
                auto& view =
                    app_state.render_views[static_cast<std::size_t>(i)];
                view.viewport_index = i;
                view.descriptor =
                    viewport_manager.framebuffer(i).imgui_descriptor();
                view.show_live_image = view.descriptor != VK_NULL_HANDLE;
                view.image_width = camera.viewport_width();
                view.image_height = camera.viewport_height();
                view.points_visible = visible_points;
                view.points_total = dataset.point_count();
                view.frame_time_ms = app_state.performance.frame_time_ms;
                view.camera_mode = "轨道";
                view.position = format_vec3_text(camera.position());
                view.fov = camera.fov_y_degrees();
                view.measure_mode_active =
                    app_state.measurement.measure_mode_active();

                const float vp_h =
                    static_cast<float>(camera.viewport_height());
                float scale_world = 500.0f;
                if (vp_h > 1.0f) {
                    float pixel_world = 0.0f;
                    if (camera.projection_mode() ==
                        gs3d::camera::ProjectionMode::Orthographic) {
                        pixel_world = camera.ortho_height() / vp_h;
                    } else {
                        const float d = camera.distance();
                        const float fov_rad =
                            camera.fov_y_degrees() * (3.14159265f / 180.0f);
                        pixel_world = 2.0f * d * std::tan(fov_rad * 0.5f) / vp_h;
                    }
                    if (pixel_world > 0.0f) {
                        scale_world = nice_scale_distance(pixel_world * 96.0f);
                    }
                }
                view.scale = format_scale_distance(scale_world);

                // Z 轴范围 + 刻度标签同步当前高度属性 & 夸张系数。
                // 几何：包围盒 Z 用 world-space 范围（含 exag）。
                // 标签：逆映射回属性原始值。
                //   - Z source: 参考面 = 世界 Z=0 (= -origin_z * exag 渲染坐标)
                //   - Value source: 参考面 = 属性值 0 (= height_offset 渲染坐标)
                auto axis_bounds = bounds;
                const float z_label_mult   = push.height_mult;
                float       z_label_offset = push.height_offset;
                if (push.height_source == static_cast<std::uint32_t>(gs3d::app::AttrPhysicalSource::Z)) {
                    axis_bounds.min.z = push.height_offset + dataset.bbox_min_z() * push.height_mult;
                    axis_bounds.max.z = push.height_offset + dataset.bbox_max_z() * push.height_mult;
                } else {
                    axis_bounds.min.z = push.height_offset + dataset.value_min() * push.height_mult;
                    axis_bounds.max.z = push.height_offset + dataset.value_max() * push.height_mult;
                }

                compute_axis_overlay(
                    view,
                    axis_bounds,
                    camera,
                    dataset.origin_x(),
                    dataset.origin_y(),
                    dataset.origin_z(),
                    z_label_mult,
                    z_label_offset
                );

                compute_map_axis_overlay(
                    view,
                    camera,
                    dataset.origin_x(),
                    dataset.origin_y()
                );

                compute_gizmo_axes(view, camera);

                const auto& hover_point =
                    latest_gpu_hover_points[static_cast<std::size_t>(i)];

                // Timeout only increments when no pick request is issued
                // (cursor outside image).  While the cursor is on the
                // image and picks are in flight, existing data stays live.
                constexpr int kHoverTimeoutFrames = 30;
                auto& ht = hover_timeout[static_cast<std::size_t>(i)];
                view.hover_tooltip_visible =
                    hover_point.has_value() && ht <= kHoverTimeoutFrames;
                view.hover_x = 0.0f;
                view.hover_y = 0.0f;
                view.hover_fold = 0.0f;
                view.hover_elevation = 0.0f;
                view.hover_primary_value_label = primary_value_name;
                view.hover_z_label = z_field_name;
                view.hover_screen_x = -1.0f;
                view.hover_screen_y = -1.0f;
                if (hover_point) {
                    view.hover_x =
                        static_cast<float>(
                            static_cast<double>(hover_point->x) +
                            dataset.origin_x());
                    view.hover_y =
                        static_cast<float>(
                            static_cast<double>(hover_point->y) +
                            dataset.origin_y());
                    view.hover_fold = hover_point->value;
                    view.hover_elevation =
                        static_cast<float>(
                            static_cast<double>(hover_point->z) +
                            dataset.origin_z());

                    // Z 映射：与 vertex shader 的 height = offset + raw * mult
                    // 完全一致，对所有 height_source 统一应用，否则高度缩放后
                    // 准星投影会与渲染点错位。
                    float raw_height = hover_point->z;
                    if (push.height_source ==
                        static_cast<std::uint32_t>(
                            gs3d::app::AttrPhysicalSource::Value)) {
                        raw_height = hover_point->value;
                    }
                    float mapped_z = push.height_offset +
                                     raw_height * push.height_mult;

                    // Marker screen position via to_screen projection.
                    const auto screen_pt =
                        gs3d::camera::MouseRay::to_screen(
                            {hover_point->x, hover_point->y, mapped_z},
                            {camera.viewport_width(),
                             camera.viewport_height()},
                            camera);
                    if (screen_pt) {
                        view.hover_screen_x = screen_pt->x;
                        view.hover_screen_y = screen_pt->y;
                        static int diag_count = 0;
                        if (diag_count < 5) {
                            ++diag_count;
                            const auto idx = static_cast<std::size_t>(i);
                            std::fprintf(stderr,
                                "[PICK] mouse=(%.0f,%.0f) fb=%ux%u "
                                "hit3d=(%.3f,%.3f,%.3f) "
                                "proj_screen=(%.1f,%.1f)\n",
                                static_cast<double>(idx < latest_gpu_capture_x.size() ? latest_gpu_capture_x[idx] : -1.0f),
                                static_cast<double>(idx < latest_gpu_capture_y.size() ? latest_gpu_capture_y[idx] : -1.0f),
                                camera.viewport_width(), camera.viewport_height(),
                                static_cast<double>(hover_point->x),
                                static_cast<double>(hover_point->y),
                                static_cast<double>(mapped_z),
                                static_cast<double>(screen_pt->x),
                                static_cast<double>(screen_pt->y));
                        }
                    }
                }

                const auto view_index = static_cast<std::size_t>(i);
                view.selected_point_visible = false;
                view.selected_screen_x = -1.0f;
                view.selected_screen_y = -1.0f;
                if (view_index < selected_focus_points.size() &&
                    selected_focus_points[view_index].has_value()) {
                    const auto selected_screen =
                        gs3d::camera::MouseRay::to_screen(
                            *selected_focus_points[view_index],
                            {camera.viewport_width(),
                             camera.viewport_height()},
                            camera
                        );
                    if (selected_screen.has_value()) {
                        view.selected_point_visible = true;
                        view.selected_screen_x = selected_screen->x;
                        view.selected_screen_y = selected_screen->y;
                    }
                }

                // ── 测量线投影：每条全局测量线的两端点 → 本视口屏幕坐标 ──
                {
                    const auto& lines = app_state.measurement.lines();
                    view.measurement_overlays.clear();
                    view.measurement_overlays.reserve(lines.size());
                    for (const auto& line : lines) {
                        RenderViewState::MeasurementLineOverlay overlay;
                        overlay.color = line.color;
                        overlay.label = line.distance_label(
                            app_state.measurement.display_mode());

                        const auto sa = gs3d::camera::MouseRay::to_screen(
                            {line.point_a.x, line.point_a.y, line.point_a.z},
                            {camera.viewport_width(),
                             camera.viewport_height()},
                            camera);
                        const auto sb = gs3d::camera::MouseRay::to_screen(
                            {line.point_b.x, line.point_b.y, line.point_b.z},
                            {camera.viewport_width(),
                             camera.viewport_height()},
                            camera);

                        if (sa && sb) {
                            overlay.a_screen_x = sa->x;
                            overlay.a_screen_y = sa->y;
                            overlay.b_screen_x = sb->x;
                            overlay.b_screen_y = sb->y;
                            overlay.visible = true;
                        }
                        view.measurement_overlays.push_back(overlay);
                    }
                }

                // ── 待定测量点投影（选了第一个点，等第二个点）──
                view.pending_point_visible = false;
                view.pending_point_screen_x = -1.0f;
                view.pending_point_screen_y = -1.0f;
                if (app_state.measurement.has_pending()) {
                    const auto& pending = *app_state.measurement.pending_point();
                    const auto sp = gs3d::camera::MouseRay::to_screen(
                        {pending.x, pending.y, pending.z},
                        {camera.viewport_width(),
                         camera.viewport_height()},
                        camera);
                    if (sp) {
                        view.pending_point_visible = true;
                        view.pending_point_screen_x = sp->x;
                        view.pending_point_screen_y = sp->y;
                    }
                }
            }

            // ── 导航图视野框：复用 compute_map_axis_overlay 的可见 XY 范围 ──
            if (nm.valid) {
                const auto& sv =
                    app_state.render_views[static_cast<std::size_t>(
                        streaming_viewport_index)];
                const float wx_min = sv.map_axis_x_min;
                const float wx_max = sv.map_axis_x_max;
                const float wy_min = sv.map_axis_y_min;
                const float wy_max = sv.map_axis_y_max;

                const float bbox_w = nm.bbox_max_x - nm.bbox_min_x;
                const float bbox_h = nm.bbox_max_y - nm.bbox_min_y;
                if (bbox_w > 0.0f && bbox_h > 0.0f) {
                    nm.view_rect_min_x =
                        (wx_min - nm.bbox_min_x) / bbox_w * nm.tex_w;
                    nm.view_rect_max_x =
                        (wx_max - nm.bbox_min_x) / bbox_w * nm.tex_w;
                    // 纹理北在上(tex_y=0)，世界 Y↑ 映射到 tex_y↓
                    nm.view_rect_min_y =
                        (1.0f - (wy_max - nm.bbox_min_y) / bbox_h) *
                        nm.tex_h;
                    nm.view_rect_max_y =
                        (1.0f - (wy_min - nm.bbox_min_y) / bbox_h) *
                        nm.tex_h;
                    nm.view_rect_valid = true;
                }
            }

            // Enforce configured viewport count — ghost viewport windows
            // restored by ImGui layout persistence must not render or
            // consume hover hit-tests (they steal the tooltip).
            for (auto& view : app_state.render_views) {
                view.visible =
                    view.viewport_index < config_.viewport_count;
            }

            auto gui_cmds = imgui_layer.new_frame(app_state);
            const double now_seconds =
                std::chrono::duration<double>(
                    current_time.time_since_epoch()
                ).count();
            if (benchmark_pick_enabled) {
                // Benchmark queries are authored against the requested
                // benchmark viewport size, not whatever persisted ImGui
                // layout happened to leave in the current framebuffer.
                // Force the scripted viewport frame to that target size so
                // the offscreen framebuffer, camera projection, and query
                // coordinates stay in the same space.
                const std::uint32_t benchmark_viewport_width =
                    std::max(config_.window_width, 1u);
                const std::uint32_t benchmark_viewport_height =
                    std::max(config_.window_height, 1u);
                const bool query_active =
                    benchmark_frame_index >= kBenchmarkPickWarmupFrames &&
                    benchmark_pick_issue_index <
                        benchmark_pick_queries.size();
                const auto& query =
                    query_active
                        ? benchmark_pick_queries[benchmark_pick_issue_index]
                        : BenchmarkPickScriptQuery{};
                bool frame_found = false;
                for (auto& frame : gui_cmds.viewport_frames) {
                    if (frame.index != 0) {
                        continue;
                    }
                    frame_found = true;
                    frame.hovered = query_active;
                    frame.active = false;
                    frame.width = benchmark_viewport_width;
                    frame.height = benchmark_viewport_height;
                    frame.mouse_local_x = query.mouse_x;
                    frame.mouse_local_y = query.mouse_y;
                    frame.mouse_on_image = query_active;
                    frame.rotate = false;
                    frame.pan = false;
                    frame.mouse_delta_x = 0.0f;
                    frame.mouse_delta_y = 0.0f;
                    frame.mouse_wheel = 0.0f;
                    frame.box_select_completed = false;
                    break;
                }
                if (!frame_found) {
                    gui_cmds.viewport_frames.push_back({
                        .index = 0,
                        .hovered = query_active,
                        .active = false,
                        .width = benchmark_viewport_width,
                        .height = benchmark_viewport_height,
                        .mouse_delta_x = 0.0f,
                        .mouse_delta_y = 0.0f,
                        .mouse_wheel = 0.0f,
                        .mouse_local_x = query.mouse_x,
                        .mouse_local_y = query.mouse_y,
                        .mouse_on_image = query_active,
                        .rotate = false,
                        .pan = false,
                        .box_select_completed = false
                    });
                }
            }
            for (const auto& frame : gui_cmds.viewport_frames) {
                viewport_resize_scheduler.observe(
                    frame.index,
                    frame.width,
                    frame.height,
                    now_seconds
                );
            }

            visible_viewports.clear();
            for (const auto& view : app_state.render_views) {
                if (view.render_requested) {
                    visible_viewports.push_back(view.viewport_index);
                }
            }

            const bool imgui_wants_keyboard =
                ImGui::GetIO().WantCaptureKeyboard;
            const bool keyboard_shortcuts_allowed =
                !ImGui::GetIO().WantTextInput;
            gs3d::util::Stopwatch benchmark_camera_timer;

            // Apply GUI panel commands (Polyscope pattern: UI produces commands,
            // main loop applies them — keeps UI and app logic decoupled).
            if (gui_cmds.reset_camera_index >= 0 &&
                gui_cmds.reset_camera_index < n_viewports) {
                controllers[
                    static_cast<std::size_t>(
                        gui_cmds.reset_camera_index
                    )
                ].clear_orbit_pivot();
                initialize_camera_from_config(
                    viewport_manager.camera(gui_cmds.reset_camera_index),
                    config_,
                    bounds
                );
                camera_hub.propagate(gui_cmds.reset_camera_index);
                streaming_viewport_index =
                    gui_cmds.reset_camera_index;
                tile_selection_dirty = true;
            }
            if (gui_cmds.point_size_changed) {
                push.point_size = std::clamp(gui_cmds.point_size, 1.0f, 10.0f);
            }
            if (gui_cmds.color_by_changed) {
                const int new_idx = std::clamp(
                    gui_cmds.color_by_index, 0,
                    static_cast<int>(attr_list.size()) - 1
                );
                scene_state.active_attribute_index = new_idx;
                const auto& a = attr_list[static_cast<std::size_t>(new_idx)];
                push.color_source = static_cast<std::uint32_t>(a.source);
                push.color_min    = a.min_val;
                push.color_range  = a.range();
                if (push.color_range <= 0.0f) push.color_range = 1.0f;
                std::cout << "[COLOR] switched to: " << a.name << '\n';
                nm.dirty = true;
            }
            if (gui_cmds.height_by_changed) {
                const int new_idx = std::clamp(
                    gui_cmds.height_by_index, 0,
                    static_cast<int>(attr_list.size()) - 1
                );
                scene_state.active_height_index = new_idx;
                apply_height_attr(attr_list[static_cast<std::size_t>(new_idx)], height_exag);
                std::cout << "[HEIGHT] switched to: "
                          << attr_list[static_cast<std::size_t>(new_idx)].name << '\n';
            }
            if (gui_cmds.height_exag_changed) {
                height_exag = gui_cmds.height_exag;
                apply_height_attr(
                    attr_list[static_cast<std::size_t>(scene_state.active_height_index)],
                    height_exag
                );
            }
            if (gui_cmds.colormap_changed) {
                // 清零 colormap bits 再写入新索引
                push.flags &= ~gs3d::render::PointFlags::kColormapMask;
                push.flags |= (static_cast<std::uint32_t>(gui_cmds.colormap_index) << 1)
                    & gs3d::render::PointFlags::kColormapMask;
                nm.dirty = true;
            }
            if (gui_cmds.value_clip_changed) {
                if (gui_cmds.value_clip_enabled) {
                    push.flags |= gs3d::render::PointFlags::kValueClip;
                    // 将原始数据值转换为归一化 [0,1] 传给 shader
                    const float cr = push.color_range > 0.0f
                        ? push.color_range : 1.0f;
                    const float norm_lo =
                        (gui_cmds.value_clip_min - push.color_min) / cr;
                    const float norm_hi =
                        (gui_cmds.value_clip_max - push.color_min) / cr;
                    push.clip_min[3] = std::clamp(norm_lo, 0.0f, 1.0f);
                    push.clip_max[3] = std::clamp(norm_hi, 0.0f, 1.0f);
                } else {
                    push.flags &= ~gs3d::render::PointFlags::kValueClip;
                }
                nm.dirty = true;
            }
            if (gui_cmds.point_shape_changed) {
                // 清零 point_shape bits 再写入新索引
                push.flags &= ~gs3d::render::PointFlags::kPointShapeMask;
                push.flags |= (static_cast<std::uint32_t>(gui_cmds.point_shape)
                    << gs3d::render::PointFlags::kPointShapeShift)
                    & gs3d::render::PointFlags::kPointShapeMask;
            }
            if (gui_cmds.clear_cache_requested) {
                if (tile_preload_enabled && !tiles_fully_resident &&
                    !tile_preload_failed) {
                    // Cancel preload so clear() isn't immediately defeated
                    // by the background thread re-inserting tiles.
                    tile_preload_failed = true;
                    if (tile_preload_future.valid()) {
                        tile_preload_future.wait();
                    }
                    std::cout << "[TILE] preload cancelled for cache clear.\n";
                }
                tile_point_cache.clear();
                std::cout << "[TILE] CPU cache cleared.\n";
            }
            if (gui_cmds.screenshot_requested) {
                // Map viewport 0's canvas_rect (ImGui screen coords) →
                // swapchain physical pixels.
                for (const auto& view : app_state.render_views) {
                    if (view.viewport_index != 0) continue;
                    if (view.canvas_rect_max_x <= view.canvas_rect_min_x ||
                        view.canvas_rect_max_y <= view.canvas_rect_min_y) break;
                    const auto& io = ImGui::GetIO();
                    const ImVec2 vp_pos = ImGui::GetMainViewport()->Pos;
                    const float sx = io.DisplayFramebufferScale.x;
                    const float sy = io.DisplayFramebufferScale.y;
                    int x = static_cast<int>((view.canvas_rect_min_x - vp_pos.x) * sx);
                    int y = static_cast<int>((view.canvas_rect_min_y - vp_pos.y) * sy);
                    int w = static_cast<int>((view.canvas_rect_max_x - view.canvas_rect_min_x) * sx);
                    int h = static_cast<int>((view.canvas_rect_max_y - view.canvas_rect_min_y) * sy);
                    // Clamp to swapchain extent
                    const auto& sc_ext = swapchain.extent();
                    if (x < 0) { w += x; x = 0; }
                    if (y < 0) { h += y; y = 0; }
                    if (x + w > static_cast<int>(sc_ext.width))  w = static_cast<int>(sc_ext.width)  - x;
                    if (y + h > static_cast<int>(sc_ext.height)) h = static_cast<int>(sc_ext.height) - y;
                    if (w > 0 && h > 0) {
                        screenshot_offset = {static_cast<std::uint32_t>(x),
                                             static_cast<std::uint32_t>(y)};
                        screenshot_extent = {static_cast<std::uint32_t>(w),
                                             static_cast<std::uint32_t>(h)};
                        screenshot_pending = true;
                    }
                    break;
                }
            }

            // ── 区域统计：测量模式下 Shift+左键框选（异步计算）──
            // 屏幕空间判断：逐点 world→screen 投影，检查是否落在框选矩形内。
            // 斜视/俯视均正确，不依赖平面反投影近似。
            //
            // 计算在后台线程执行，主线程立即返回继续渲染。
            // generation counter 实现取消：新框选使旧任务的 gen 失配，旧任务
            // 每 64K 点检查一次并提前退出，结果被丢弃。
            for (const auto& frame : gui_cmds.viewport_frames) {
                if (!frame.stats_select_completed || !frame.mouse_on_image) {
                    continue;
                }
                if (frame.index < 0 ||
                    frame.index >= viewport_manager.viewport_count()) {
                    continue;
                }

                // Cancel any in-flight computation by bumping the generation.
                const auto gen = ++region_stats_gen_;

                const auto& cam = viewport_manager.camera(frame.index);
                const gs3d::camera::Mat4 vp =
                    cam.view_projection_matrix();
                const float vp_w =
                    static_cast<float>(cam.viewport_width());
                const float vp_h =
                    static_cast<float>(cam.viewport_height());

                const float sx_min = frame.stats_select_min_x;
                const float sx_max = frame.stats_select_max_x;
                const float sy_min = frame.stats_select_min_y;
                const float sy_max = frame.stats_select_max_y;

                // Snapshot point-data access: raw pointer when in-memory
                // (dataset lives for the entire run() scope, safe to
                // reference from the short-lived worker), or copy the path
                // and let the worker read from disk.
                const bool has_points = dataset.has_point_data();
                const gs3d::data::Gs3dPoint* points_data =
                    has_points ? dataset.points().data() : nullptr;
                const std::uint64_t point_count =
                    has_points ? dataset.point_count() : 0;
                const std::filesystem::path gs3d_path =
                    has_points ? std::filesystem::path{} : config_.gs3d_path;

                app_state.region_stats = RegionStatsResult{};
                app_state.region_stats.computing = true;
                app_state.region_stats.primary_label = primary_value_name;
                app_state.region_stats.secondary_label = z_field_name;

                // Compute world-space XY bounds of the selection rectangle
                // so the panel can display the approximate coordinate range.
                // box_select_world_bounds returns local-space coords (matching
                // the camera space); add origin to get absolute coords that
                // match the hover tooltip / map axis display.
                double world_x_min = 0.0;
                double world_x_max = 0.0;
                double world_y_min = 0.0;
                double world_y_max = 0.0;
                {
                    const gs3d::camera::Viewport stats_viewport{
                        static_cast<std::uint32_t>(vp_w),
                        static_cast<std::uint32_t>(vp_h)
                    };
                    const float plane_z = cam.target().z;
                    const auto selection_bounds =
                        gs3d::camera::box_select_world_bounds(
                            sx_min, sy_min, sx_max, sy_max,
                            stats_viewport, cam, bounds, plane_z);
                    if (selection_bounds) {
                        const double ox = dataset.origin_x();
                        const double oy = dataset.origin_y();
                        world_x_min = static_cast<double>(selection_bounds->min.x) + ox;
                        world_x_max = static_cast<double>(selection_bounds->max.x) + ox;
                        world_y_min = static_cast<double>(selection_bounds->min.y) + oy;
                        world_y_max = static_cast<double>(selection_bounds->max.y) + oy;
                    }
                }

                region_stats_future_ = std::async(
                    std::launch::async,
                    [gen,
                     vp, vp_w, vp_h,
                     sx_min, sx_max, sy_min, sy_max,
                     has_points, points_data, point_count,
                     gs3d_path,
                     world_x_min, world_x_max, world_y_min, world_y_max,
                     primary_label = primary_value_name,
                     secondary_label = z_field_name,
                     &gen_counter = region_stats_gen_]() -> RegionStatsResult
                    {
                        double fold_sum = 0.0;
                        double elev_sum = 0.0;
                        float fold_min =
                            std::numeric_limits<float>::max();
                        float fold_max =
                            std::numeric_limits<float>::lowest();
                        float elev_min =
                            std::numeric_limits<float>::max();
                        float elev_max =
                            std::numeric_limits<float>::lowest();
                        std::uint64_t count = 0;

                        const auto process =
                            [&](const gs3d::data::Gs3dPoint& p) {
                                const auto sp =
                                    gs3d::camera::MouseRay::world_to_screen(
                                        vp, p.x, p.y, p.z, vp_w, vp_h);
                                if (!sp) return;
                                if (sp->x < sx_min || sp->x > sx_max ||
                                    sp->y < sy_min || sp->y > sy_max) {
                                    return;
                                }
                                ++count;
                                const float f = p.value;
                                const float e = p.z;
                                fold_sum += static_cast<double>(f);
                                elev_sum += static_cast<double>(e);
                                if (f < fold_min) fold_min = f;
                                if (f > fold_max) fold_max = f;
                                if (e < elev_min) elev_min = e;
                                if (e > elev_max) elev_max = e;
                            };

                        constexpr std::uint64_t kCancelCheckInterval =
                            65536;

                        if (has_points) {
                            for (std::uint64_t i = 0; i < point_count;
                                 ++i) {
                                if ((i & (kCancelCheckInterval - 1)) == 0) {
                                    if (gen_counter.load(
                                            std::memory_order_relaxed) !=
                                        gen) {
                                        return RegionStatsResult{};
                                    }
                                }
                                process(points_data[i]);
                            }
                        } else {
                            auto read_result =
                                gs3d::data::Gs3dReader::read_all(
                                    gs3d_path);
                            std::uint64_t i = 0;
                            for (const auto& p : read_result.points) {
                                if ((i & (kCancelCheckInterval - 1)) == 0) {
                                    if (gen_counter.load(
                                            std::memory_order_relaxed) !=
                                        gen) {
                                        return RegionStatsResult{};
                                    }
                                }
                                process(p);
                                ++i;
                            }
                        }

                        RegionStatsResult out;
                        out.valid = true;
                        out.point_count = count;
                        out.world_x_min = world_x_min;
                        out.world_x_max = world_x_max;
                        out.world_y_min = world_y_min;
                        out.world_y_max = world_y_max;
                        out.primary_label = primary_label;
                        out.secondary_label = secondary_label;
                        if (count > 0) {
                            const double inv =
                                1.0 / static_cast<double>(count);
                            out.fold_min = fold_min;
                            out.fold_max = fold_max;
                            out.fold_avg =
                                static_cast<float>(fold_sum * inv);
                            out.elev_min = elev_min;
                            out.elev_max = elev_max;
                            out.elev_avg =
                                static_cast<float>(elev_sum * inv);
                        }
                        return out;
                    });

                break; // one launch per frame
            }

            // Poll completion: when the worker finishes, swap its result
            // into app_state.  If the result is invalid (cancelled),
            // just clear the computing flag so the panel goes back to idle.
            if (region_stats_future_.valid()) {
                if (region_stats_future_.wait_for(
                        std::chrono::seconds(0)) ==
                    std::future_status::ready) {
                    auto result = region_stats_future_.get();
                    if (result.valid) {
                        app_state.region_stats = std::move(result);
                    } else {
                        app_state.region_stats.computing = false;
                    }
                }
            }

            if (!imgui_wants_keyboard && window.key_pressed(GLFW_KEY_ESCAPE)) {
                window.request_close();
            }

            if (!imgui_wants_keyboard) {
                if (window.key_pressed(GLFW_KEY_EQUAL) ||
                    window.key_pressed(GLFW_KEY_KP_ADD)) {
                    push.point_size = std::min(
                        push.point_size + 0.05f,
                        10.0f
                    );
                }

                if (window.key_pressed(GLFW_KEY_MINUS) ||
                    window.key_pressed(GLFW_KEY_KP_SUBTRACT)) {
                    push.point_size = std::max(
                        push.point_size - 0.05f,
                        1.0f
                    );
                }
            }

            const bool r_pressed =
                keyboard_shortcuts_allowed &&
                (window.key_pressed(GLFW_KEY_R) ||
                 ImGui::IsKeyPressed(ImGuiKey_R, false));

            if (r_pressed && !r_was_pressed) {
                controllers[
                    static_cast<std::size_t>(streaming_viewport_index)
                ].clear_orbit_pivot();
                initialize_camera_from_config(
                    viewport_manager.camera(streaming_viewport_index),
                    config_,
                    bounds
                );
                camera_hub.propagate(streaming_viewport_index);
                tile_selection_dirty = true;
            }

            r_was_pressed = r_pressed;

            const bool f_pressed =
                keyboard_shortcuts_allowed &&
                (window.key_pressed(GLFW_KEY_F) ||
                 ImGui::IsKeyPressed(ImGuiKey_F, false));

            if (f_pressed && !f_was_pressed) {
                const auto focus_index =
                    static_cast<std::size_t>(streaming_viewport_index);
                if (focus_index < selected_focus_points.size() &&
                    selected_focus_points[focus_index].has_value()) {
                    controllers[focus_index].focus_on(
                        viewport_manager.camera(
                            streaming_viewport_index
                        ),
                        *selected_focus_points[focus_index]
                    );
                    camera_hub.propagate(streaming_viewport_index);
                    tile_selection_dirty = true;
                    std::cout
                        << "[CAMERA] focused selected point in viewport "
                        << streaming_viewport_index << '\n';
                } else {
                    std::cout
                        << "[CAMERA] focus skipped: no selected point in viewport "
                        << streaming_viewport_index << '\n';
                }
            }

            f_was_pressed = f_pressed;

            // Tab: cycle color attribute; Shift+Tab: cycle height attribute
            // (zero GPU cost — push constant only)
            const bool tab_held =
                !imgui_wants_keyboard && window.key_pressed(GLFW_KEY_TAB);
            const bool shift_mod =
                window.key_pressed(GLFW_KEY_LEFT_SHIFT) ||
                window.key_pressed(GLFW_KEY_RIGHT_SHIFT);

            if (!tab_held) {
                tab_was_pressed = false;
                shift_tab_was_pressed = false;
            } else if (shift_mod && !shift_tab_was_pressed) {
                shift_tab_was_pressed = true;
                tab_was_pressed = true;
                const auto n = static_cast<std::uint32_t>(attr_list.size());
                const std::uint32_t new_idx =
                    (static_cast<std::uint32_t>(scene_state.active_height_index) + 1u) % n;
                scene_state.active_height_index = static_cast<int>(new_idx);
                apply_height_attr(attr_list[new_idx], height_exag);
                std::cout << "[HEIGHT] switched to: " << attr_list[new_idx].name << '\n';
            } else if (!tab_was_pressed) {
                tab_was_pressed = true;
                const auto n = static_cast<std::uint32_t>(attr_list.size());
                const std::uint32_t new_idx =
                    (static_cast<std::uint32_t>(scene_state.active_attribute_index) + 1u) % n;
                scene_state.active_attribute_index = static_cast<int>(new_idx);
                const auto& a = attr_list[new_idx];
                push.color_source = static_cast<std::uint32_t>(a.source);
                push.color_min    = a.min_val;
                push.color_range  = a.range();
                if (push.color_range <= 0.0f) push.color_range = 1.0f;
                std::cout << "[COLOR] switched to: " << a.name << '\n';
            }

            for (const auto& view : app_state.render_views) {
                camera_hub.set_group(
                    view.viewport_index,
                    view.camera_linked
                        ? 0
                        : gs3d::camera::CameraHub::kIndependent
                );
            }

            bool interacting = false;
            bool camera_changed = false;
            for (const auto& frame : gui_cmds.viewport_frames) {
                if (frame.index < 0 ||
                    frame.index >= static_cast<int>(controllers.size())) {
                    continue;
                }

                if ((frame.hovered || frame.active) &&
                    streaming_viewport_index != frame.index) {
                    streaming_viewport_index = frame.index;
                    tile_selection_dirty = true;
                }

                gs3d::camera::CameraInput input;
                input.viewport_width = frame.width;
                input.viewport_height = frame.height;
                input.delta_x = frame.mouse_delta_x;
                input.delta_y = frame.mouse_delta_y;
                input.scroll_y = frame.mouse_wheel;
                input.mouse_x = frame.mouse_local_x;
                input.mouse_y = frame.mouse_local_y;
                input.mouse_position_valid = frame.mouse_on_image;
                input.rotate = frame.rotate;
                input.pan = frame.pan;

                // rotate_begin: deferred until cursor moves ≥ kRotateActivationPx
                // from the button-down position.  Pure clicks skip rotation.
                {
                    const auto idx =
                        static_cast<std::size_t>(frame.index);
                    if (idx < prev_rotate.size()) {
                        const bool pressed =
                            frame.rotate && !prev_rotate[idx];
                        if (pressed) {
                            mouse_down_x[idx] = frame.mouse_local_x;
                            mouse_down_y[idx] = frame.mouse_local_y;
                            rotation_activated[idx] = false;
                        }

                        if (frame.rotate) {
                            if (!rotation_activated[idx]) {
                                const float dx =
                                    frame.mouse_local_x - mouse_down_x[idx];
                                const float dy =
                                    frame.mouse_local_y - mouse_down_y[idx];
                                if (dx * dx + dy * dy >=
                                    kRotateActivationPx * kRotateActivationPx) {
                                    rotation_activated[idx] = true;
                                    input.rotate_begin = true;
                                } else {
                                    // Not yet a drag — suppress rotation.
                                    input.delta_x = 0.0f;
                                    input.delta_y = 0.0f;
                                }
                            }
                        } else {
                            rotation_activated[idx] = false;
                        }

                        prev_rotate[idx] = frame.rotate;
                    }
                }

                interacting = interacting || input.interacting();
                if (controllers[static_cast<std::size_t>(frame.index)]
                        .update(
                            viewport_manager.camera(frame.index),
                            input
                        )) {
                    camera_changed = true;
                    streaming_viewport_index = frame.index;
                    camera_hub.propagate(frame.index);
                }
            }

            if (config_.benchmark_mode &&
                !benchmark_pick_enabled &&
                benchmark_frame_index < benchmark_orbit_frames) {
                // Orbit + a slow zoom-in so the visible region actually
                // shrinks — a pure yaw orbit at a fixed distance can leave
                // the whole bbox in view the entire time, never forcing a
                // different tile selection, which would starve the reload-
                // latency measurement below.
                auto& bench_camera =
                    viewport_manager.camera(streaming_viewport_index);
                bench_camera.orbit(0.01f, 0.0f);
                bench_camera.zoom(0.999f);
                camera_hub.propagate(streaming_viewport_index);
                interacting = true;
                camera_changed = true;
            }

            benchmark_camera_update_ms_frame =
                benchmark_camera_timer.elapsed_milliseconds();

            if (camera_changed) {
                tile_selection_dirty = true;
            }

            gs3d::util::Stopwatch benchmark_lod_tile_timer;
            const auto flush_benchmark_lod_tile_stage =
                [&]() {
                    benchmark_lod_tile_select_ms_frame +=
                        benchmark_lod_tile_timer.elapsed_milliseconds();
                    benchmark_lod_tile_timer.reset();
                };

            /*
             * Debounce the interacting signal so rapid scroll zoom doesn't
             * cause frame-by-frame oscillation between true/false. Without
             * this, fast mouse-wheel scrolling produces frames where
             * scroll_y is 0 between discrete wheel events, briefly flipping
             * interacting to false.  That would:
             *   - toggle tiles on then off (tile_will_render)
             *   - toggle LOD clip bbox on then off
             *   - trigger tile selection update mid-scroll
             * …all of which cause visible flicker.
             *
             * The debounce keeps interacting=true for kInteractingDebounceSeconds
             * after the last real interacting frame, bridging the gaps between
             * discrete scroll events.
             */
            if (interacting) {
                interacting_debounce_until =
                    current_time +
                    std::chrono::milliseconds(
                        static_cast<long>(
                            kInteractingDebounceSeconds * 1000.0
                        )
                    );
            } else if (current_time < interacting_debounce_until) {
                interacting = true;
            }

            if (config_.lod_enabled) {
                lod_selector.update(
                    interacting,
                    delta_seconds
                );
            }

            if (config_.tile_enabled && tile_reader.has_value()) {
                auto tile_config =
                    make_tile_selection_config(config_);
                tile_config.height_offset = push.height_offset;
                tile_config.height_mult = push.height_mult;
                tile_config.height_source = push.height_source;
                tile_selection.set_config(tile_config);
            }

            /*
             * 全量预加载阶段:后台一次性读取全部瓦片,主循环用大预算逐帧
             * 上传到 GPU 常驻。全部驻留后切到快路径(下方),不再走流式。
             */
            if (tile_preload_enabled && !tiles_fully_resident &&
                !tile_preload_failed && tile_gpu_cloud) {
                if (!tile_preload_dispatched) {
                    tile_preload_dispatched = true;
                    tile_preload_timer.reset();
                    const auto& reader = *tile_reader;
                    tile_preload_future = std::async(
                        std::launch::async,
                        [&reader, &tile_point_cache, &load_tile_points_with_ids]()
                            -> std::vector<std::pair<
                                std::uint64_t, SharedTilePoints>> {
                            std::vector<std::pair<
                                std::uint64_t, SharedTilePoints>> all;
                            all.reserve(reader.records().size());
                            for (const auto& rec : reader.records()) {
                                // Use find() (shared lock) for the check to
                                // avoid serializing against main-thread
                                // find()/stats() calls. Only put() (exclusive)
                                // on cache miss.
                                SharedTilePoints pts =
                                    tile_point_cache.find(rec.tile_id);
                                if (!pts) {
                                    auto loaded =
                                        load_tile_points_with_ids(
                                            rec.tile_id
                                        );
                                    tile_point_cache.put(rec.tile_id, loaded);
                                    pts = loaded;
                                }
                                all.emplace_back(rec.tile_id, pts);
                            }
                            return all;
                        });
                }

                if (tile_preload_tiles.empty() &&
                    tile_preload_future.valid() &&
                    tile_preload_future.wait_for(std::chrono::seconds(0)) ==
                        std::future_status::ready) {
                    try {
                        tile_preload_tiles = tile_preload_future.get();
                        register_runtime_tile_point_lookup(
                            tile_preload_tiles,
                            runtime_points_by_id,
                            runtime_points_valid_by_id
                        );
                    } catch (const std::exception& e) {
                        std::cerr
                            << "[TILE] preload failed: " << e.what()
                            << " — falling back to streaming mode.\n";
                        tile_preload_failed = true;
                    }
                }

                if (!tile_preload_tiles.empty()) {
                    renderer.wait_for_in_flight_fences();
                    const auto preload_views =
                        make_cached_tile_views(tile_preload_tiles);
                    const auto sync =
                        tile_gpu_cloud->sync_from_cached_tiles(
                            context,
                            renderer.command_pool(),
                            context.graphics_queue(),
                            preload_views,
                            config_.tile_preload_upload_budget_bytes
                        );
                    if (config_.tile_verbose && sync.uploaded_bytes > 0) {
                        std::cout
                            << "[TILE] preload upload: bytes="
                            << sync.uploaded_bytes
                            << ", resident="
                            << sync.resident_tile_count << "/"
                            << tile_preload_tiles.size()
                            << ", complete="
                            << (sync.complete ? "true" : "false")
                            << '\n';
                    }
                    if (sync.complete) {
                        tiles_fully_resident = true;
                        tile_selection_dirty = true;
                        std::cout
                            << "[TILE] preload complete: "
                            << sync.resident_tile_count
                            << " tiles resident on GPU ("
                            << sync.resident_gpu_buffer_bytes
                            << " bytes) in "
                            << tile_preload_timer.elapsed_seconds()
                            << "s — interactive streaming disabled.\n";
                    } else if (sync.uploaded_bytes == 0) {
                        ++tile_preload_stall_frames;
                        if (tile_preload_stall_frames >= 3) {
                            std::cerr
                                << "[TILE] preload stalled ("
                                << sync.resident_tile_count << "/"
                                << tile_preload_tiles.size()
                                << " tiles uploaded, budget="
                                << config_.tile_preload_upload_budget_bytes
                                << " bytes/frame)"
                                << " — falling back to streaming.\n";
                            tile_preload_failed = true;
                        }
                    } else {
                        tile_preload_stall_frames = 0;
                    }
                }
            }

            /*
             * 快路径:全部瓦片已常驻 GPU。瓦片选择(纯 CPU frustum/屏幕尺寸,
             * 无 I/O)每帧都跑,包括交互期间,把可见子集直接设为绘制集——
             * 无异步读、无上传节流、无去抖等待。绘制仍只画可见子集,开销有界。
             */
            if (tiles_fully_resident) {
                if (camera_changed || tile_selection_dirty) {
                    flush_benchmark_lod_tile_stage();
                    {
                        gs3d::util::Stopwatch cpu_cull_timer;
                        tile_result = tile_selection.update(
                            viewport_manager.camera(streaming_viewport_index),
                            tile_index_view
                        );
                        benchmark_cpu_cull_ms_frame +=
                            cpu_cull_timer.elapsed_milliseconds();
                    }
                    benchmark_lod_tile_timer.reset();
                    tile_selection_dirty = false;
                    const auto view_index =
                        static_cast<std::size_t>(streaming_viewport_index);
                    if (tile_result.enabled &&
                        !tile_result.tile_ids.empty()) {
                        viewport_tile_ids[view_index] =
                            tile_result.tile_ids;
                        viewport_tile_query_boxes[view_index] =
                            compute_tiles_bbox(tile_result.tile_ids);
                    } else {
                        viewport_tile_ids[view_index].clear();
                        viewport_tile_query_boxes[view_index].reset();
                    }
                }
            } else if (config_.tile_enabled &&
                tile_reader.has_value() &&
                tile_gpu_cloud &&
                (!tile_preload_enabled || tile_preload_failed)) {
                if (!interacting && tile_selection_dirty) {
                    flush_benchmark_lod_tile_stage();
                    {
                        gs3d::util::Stopwatch cpu_cull_timer;
                        tile_result = tile_selection.update(
                            viewport_manager.camera(streaming_viewport_index),
                            tile_index_view
                        );
                        benchmark_cpu_cull_ms_frame +=
                            cpu_cull_timer.elapsed_milliseconds();
                    }
                    benchmark_lod_tile_timer.reset();
                    tile_selection_dirty = false;
                    if (tile_result.changed) {
                        debounced_tile_ids = tile_result.tile_ids;
                        tile_selection_changed_at = current_time;
                        /*
                         * Anti-flicker (docs/benchmark/flicker-audit.md
                         * Task 5/6): do NOT clear viewport_tile_ids here.
                         * The previously-displayed tiles are still resident
                         * on the GPU (evict_to_budget only drops tiles once
                         * the *new* selection has finished uploading) and
                         * stay valid to keep drawing. Clearing them the
                         * instant the selection changes — before the async
                         * read+upload for the new selection completes —
                         * caused a visible "detail drops to LOD, then pops
                         * back" cycle on every camera-settle event, lasting
                         * as long as the reload (0.2-0.9s on the 33M-point
                         * baseline). The swap to the new tile set happens
                         * incrementally below as each tile becomes resident,
                         * not atomically at upload completion.
                         */
                    }
                }

                if (tile_load_future.valid() &&
                    tile_load_future.wait_for(std::chrono::seconds(0))
                        == std::future_status::ready) {
                    auto loaded = tile_load_future.get();
                    register_runtime_tile_point_lookup(
                        loaded.tiles,
                        runtime_points_by_id,
                        runtime_points_valid_by_id
                    );
                    tile_load_future = {};
                    tile_loading_ids.clear();
                    // Tiles are now in CPU cache; the per-frame upload
                    // loop below picks them up incrementally.
                    if (config_.tile_verbose) {
                        std::cout
                            << "[TILE] async read complete, "
                            << loaded.cache_miss_tiles
                            << " tiles loaded into CPU cache.\n";
                    }
                }

                if (!tile_selection_dirty && !tile_result.enabled) {
                    debounced_tile_ids.clear();
                    const auto view_index =
                        static_cast<std::size_t>(
                            streaming_viewport_index
                        );
                    viewport_tile_ids[view_index].clear();
                    viewport_tile_query_boxes[view_index].reset();
                } else if (tile_result.enabled && !interacting) {
                    /*
                     * Incremental upload (no batching):
                     * 1. Collect desired tiles that are in CPU cache
                     * 2. Upload whatever fits in the per-frame GPU budget
                     * 3. Update viewport_tile_ids incrementally —
                     *    tiles appear as soon as they become resident
                     * 4. Pin all desired resident tiles against LRU eviction
                     */
                    flush_benchmark_lod_tile_stage();

                    const auto view_index =
                        static_cast<std::size_t>(streaming_viewport_index);

                    // Collect desired tiles already in CPU cache
                    std::vector<std::pair<
                        std::uint64_t, SharedTilePoints>> cached_desired;
                    cached_desired.reserve(tile_result.tile_ids.size());
                    for (const auto tile_id : tile_result.tile_ids) {
                        auto pts = tile_point_cache.find(tile_id);
                        if (pts) {
                            cached_desired.emplace_back(tile_id, pts);
                        }
                    }

                    if (!cached_desired.empty()) {
                        renderer.wait_for_in_flight_fences();
                        gs3d::util::Stopwatch upload_timer;
                        const auto upload_views =
                            make_cached_tile_views(cached_desired);
                        const auto sync =
                            tile_gpu_cloud->sync_from_cached_tiles(
                                context,
                                renderer.command_pool(),
                                context.graphics_queue(),
                                upload_views,
                                config_.tile_gpu_upload_budget_bytes
                            );
                        benchmark_upload_record_ms_frame +=
                            upload_timer.elapsed_milliseconds();
                        benchmark_lod_tile_timer.reset();

                        if (config_.tile_verbose &&
                            sync.uploaded_bytes > 0) {
                            std::cout
                                << "[TILE] upload slice: bytes="
                                << sync.uploaded_bytes
                                << ", resident="
                                << sync.resident_tile_count
                                << ", desired="
                                << tile_result.tile_ids.size()
                                << ", complete="
                                << (sync.complete ? "true" : "false")
                                << '\n';
                        }

                        if (sync.complete && config_.tile_verbose) {
                            const double reload_total_seconds =
                                tile_async_cycle_timer.elapsed_seconds();
                            log_tile_upload(
                                tile_gpu_cloud->stats(),
                                sync,
                                reload_total_seconds
                            );
                            if (config_.benchmark_mode) {
                                benchmark_reload_seconds.push_back(
                                    reload_total_seconds
                                );
                            }
                        }
                    }

                    // Pin all desired resident tiles to prevent LRU
                    // eviction of tiles whose PointDataView wasn't passed
                    // to sync_from_cached_tiles (e.g. not in CPU cache yet).
                    for (const auto tile_id : tile_result.tile_ids) {
                        tile_gpu_cloud->touch_tile(tile_id);
                    }

                    // Incrementally update viewport: show whatever is
                    // resident right now (strict subset of desired set).
                    auto& vp_ids = viewport_tile_ids[view_index];
                    vp_ids.clear();
                    for (const auto tile_id : tile_result.tile_ids) {
                        if (tile_gpu_cloud->has_resident_tile(tile_id)) {
                            vp_ids.push_back(tile_id);
                        }
                    }

                    // Incrementally update clip bbox from currently
                    // resident tiles.  Clip itself is gated on
                    // all_desired_resident (see render section) so a
                    // partial bbox is never used for clipping.
                    if (!vp_ids.empty()) {
                        viewport_tile_query_boxes[view_index] =
                            compute_tiles_bbox(vp_ids);
                    } else {
                        viewport_tile_query_boxes[view_index].reset();
                    }
                }

                    /*
                     * Dispatch async disk reads for desired tiles that
                     * are not yet in the CPU cache.  The per-frame upload
                     * loop above picks them up as soon as they arrive.
                     */
                    const bool load_in_progress =
                        tile_load_future.valid();
                    const double selection_stable_seconds =
                        std::chrono::duration<double>(
                            current_time - tile_selection_changed_at
                        ).count();
                    const bool selection_stable =
                        selection_stable_seconds >=
                        kTileSelectionDebounceSeconds;

                    const bool dispatch_needed =
                        selection_stable &&
                        !load_in_progress &&
                        !debounced_tile_ids.empty() &&
                        tile_loading_ids != debounced_tile_ids;

                    if (dispatch_needed) {
                        // Only read tiles not already in CPU cache
                        std::vector<std::uint64_t> missing_tile_ids;
                        missing_tile_ids.reserve(
                            debounced_tile_ids.size());
                        std::size_t cache_hit_tiles = 0;
                        for (const auto tile_id : debounced_tile_ids) {
                            if (tile_point_cache.find(tile_id)) {
                                ++cache_hit_tiles;
                            } else {
                                missing_tile_ids.push_back(tile_id);
                            }
                        }

                        tile_loading_ids = debounced_tile_ids;
                        tile_async_cycle_timer.reset();
                        const auto& reader = *tile_reader;
                        const auto ids = debounced_tile_ids;
                        const auto candidate_tile_count =
                            static_cast<std::size_t>(
                                tile_result.total_candidate_tiles
                            );

                        if (missing_tile_ids.empty()) {
                            // All tiles already in CPU cache; the upload
                            // loop above will pick them up incrementally.
                            tile_loading_ids.clear();
                            if (config_.tile_verbose) {
                                std::cout
                                    << "[TILE] all " << ids.size()
                                    << " desired tiles cached"
                                    << " (candidates="
                                    << candidate_tile_count
                                    << "), uploading incrementally.\n";
                            }
                        } else {
                            tile_load_future = std::async(
                                std::launch::async,
                                [&reader,
                                 &tile_point_cache,
                                 &load_tile_points_with_ids,
                                 ids,
                                 missing_tile_ids =
                                    std::move(missing_tile_ids),
                                 cache_hit_tiles,
                                 candidate_tile_count]()
                                    -> TileLoadResult {
                                    gs3d::util::Stopwatch read_timer;
                                    for (const auto tile_id :
                                         missing_tile_ids) {
                                        auto pts =
                                            load_tile_points_with_ids(
                                                tile_id);
                                        tile_point_cache.put(
                                            tile_id,
                                            std::move(pts));
                                    }
                                    TileLoadResult loaded;
                                    loaded.tile_ids = ids;
                                    loaded.cache_hit_tiles =
                                        cache_hit_tiles;
                                    loaded.cache_miss_tiles =
                                        missing_tile_ids.size();
                                    loaded.candidate_tiles =
                                        candidate_tile_count;
                                    loaded.read_seconds =
                                        read_timer.elapsed_seconds();
                                    return loaded;
                                });

                            if (config_.tile_verbose) {
                                std::cout
                                    << "[TILE] async load dispatched, "
                                    << ids.size() << " tiles"
                                    << " (candidates="
                                    << candidate_tile_count
                                    << ", cache_hit="
                                    << cache_hit_tiles
                                    << ", cache_miss="
                                    << missing_tile_ids.size()
                                    << ").\n";
                            }
                        }
                    }

            }
            // Select LOD level once per frame (not per-viewport) so all views
            // use the same level and the verbose log fires at most once.
            if (config_.lod_enabled && lod_gpu_cloud) {
                const auto level_count = lod_gpu_cloud->level_count();

                // --- Spatial: what level does the current zoom need? ---
                // 交互中冻结，停手后更新——与 tile 选择的 !interacting
                // 门控语义一致，避免缩放中 LOD 硬切导致抽稀跳变。
                const auto voxel_sizes = lod_gpu_cloud->voxel_sizes();
                float world_per_pixel = 0.0f;
                float spatial_ortho_h = 0.0f;
                {
                    const auto& cam =
                        viewport_manager.camera(streaming_viewport_index);
                    spatial_ortho_h = cam.ortho_height();
                    const float vp_h =
                        static_cast<float>(cam.viewport_height());
                    if (vp_h > 0.0f) {
                        world_per_pixel = spatial_ortho_h / vp_h;
                    }
                }

                std::size_t spatial_level;
                if (interacting) {
                    // 交互中冻结：首帧正常计算，后续用冻结值
                    if (frozen_spatial_level >= level_count) {
                        frozen_spatial_level =
                            gs3d::render::LodSelector::select_level_by_spacing(
                                world_per_pixel,
                                voxel_sizes,
                                last_spatial_level
                            );
                    }
                    spatial_level = frozen_spatial_level;
                } else {
                    // 停手后更新到当前 zoom 对应的目标层
                    spatial_level =
                        gs3d::render::LodSelector::select_level_by_spacing(
                            world_per_pixel,
                            voxel_sizes,
                            last_spatial_level
                        );
                    frozen_spatial_level = spatial_level;
                }
                last_spatial_level = spatial_level;

                // --- Temporal: existing time / frame-rate logic ---
                const auto temporal_level =
                    lod_selector.select_level(level_count);

                // --- Combine: coarser of the two constraints ---
                // spatial 定"当前缩放需要多精"，temporal 定"当前性能允许多精"
                // 取 max = 更粗的那个，既是空间底线也是性能保护
                const auto requested =
                    std::max(spatial_level, temporal_level);

                // 安全网：select_level 理论上永远返回有效值（所有 LOD
                // 级别都在启动时一次性上传到 GPU），但万一索引越界退回到
                // last_valid，绝不画空帧。
                std::size_t resolved_lod;
                if (requested < level_count) {
                    resolved_lod = requested;
                } else {
                    resolved_lod = last_valid_lod_level;
                    std::cerr << "[WARN] LOD level out of range: "
                              << requested << " >= " << level_count
                              << ", falling back to "
                              << last_valid_lod_level << '\n';
                }

                /*
                 * KeepStableHighQuality: frozen 是显示质量地板，交互中锁定，
                 * 空闲时向更精细方向更新。新增：空间缩放允许 coarsening
                 * —— 缩小后经 high_delay 延迟才降质，防止缩放刚停就闪跳。
                 */
                if (config_.interactive_display_mode ==
                        gs3d::app::InteractiveDisplayMode::AllowCoarseLOD) {
                    lod_level_for_frame = resolved_lod;
                } else {
                    if (!interacting) {
                        // 传统：temporal 改善时 frozen 跟踪到更精细
                        if (resolved_lod < frozen_display_lod) {
                            frozen_display_lod = resolved_lod;
                        }
                        // 新增：缩小后空间缩放允许 coarsening
                        // （仅在 idle ≥ high_delay 后，防松手瞬间跳粗）
                        if (lod_selector.idle_seconds() >=
                            config_.lod_high_delay_seconds) {
                            frozen_display_lod =
                                std::max(frozen_display_lod, spatial_level);
                        }
                    }
                    lod_level_for_frame =
                        std::min(resolved_lod, frozen_display_lod);
                }
                last_valid_lod_level = lod_level_for_frame;

                if (lod_level_for_frame != last_lod_level) {
                    if (config_.lod_verbose) {
                        const auto& level =
                            lod_gpu_cloud->level(lod_level_for_frame);
                        std::cout << "[LOD] active level = "
                                  << lod_level_for_frame
                                  << ", points = "
                                  << level.gpu_point_count
                                  << ", idle_seconds = "
                                  << lod_selector.idle_seconds()
                                  << ", ortho_h = "
                                  << world_per_pixel
                                  << " m/px"
                                  << '\n';
                    }

                    // 空间选层调试（GS3D_LOD_DEBUG=1）
                    if (const char* env =
                            std::getenv("GS3D_LOD_DEBUG")) {
                        if (env[0] == '1') {
                            std::fprintf(
                                stderr,
                                "[LODDBG] ortho_h=%.1f wpix=%.3f "
                                "spatial=%zu%s temporal=%zu → level=%zu "
                                "frozen=%zu %s\n",
                                static_cast<double>(spatial_ortho_h),
                                static_cast<double>(world_per_pixel),
                                spatial_level,
                                interacting ? "(frozen)" : "",
                                temporal_level,
                                lod_level_for_frame,
                                frozen_display_lod,
                                interacting ? "(interacting)" : "(idle)");
                        }
                    }

                    last_lod_level = lod_level_for_frame;
                }
            }

            flush_benchmark_lod_tile_stage();
            for (auto& request : gpu_pick_requests) {
                request = {};
            }
            // Clear hover data only for views that did not render this
            // frame — not based on ImGui hover state (which can be wrong
            // when ghost viewports consume the hover hit-test).
            for (int i = 0; i < viewport_manager.active_count(); ++i) {
                if (!app_state.render_views[static_cast<std::size_t>(i)]
                         .render_requested) {
                    latest_gpu_hover_points[static_cast<std::size_t>(i)]
                        .reset();
                }
            }
            for (const auto& frame : gui_cmds.viewport_frames) {
                if (frame.index < 0 ||
                    frame.index >= static_cast<int>(gpu_pick_requests.size())) {
                    continue;
                }

                auto& request =
                    gpu_pick_requests[static_cast<std::size_t>(frame.index)];
                request.viewport_index = frame.index;
                request.viewport_width = frame.width;
                request.viewport_height = frame.height;

                if (frame.box_select_completed && frame.mouse_on_image) {
                    request.kind = GpuPickRequestKind::BoxSelectAnchor;
                    request.mouse_x =
                        0.5f * (frame.box_select_min_x + frame.box_select_max_x);
                    request.mouse_y =
                        0.5f * (frame.box_select_min_y + frame.box_select_max_y);
                    request.pick_radius_px =
                        compute_hover_pick_radius_px(push.point_size);
                    request.box_select_min_x = frame.box_select_min_x;
                    request.box_select_min_y = frame.box_select_min_y;
                    request.box_select_max_x = frame.box_select_max_x;
                    request.box_select_max_y = frame.box_select_max_y;
                    request.anchor_camera =
                        viewport_manager.camera(frame.index);
                    continue;
                }

                if (frame.point_double_clicked &&
                    frame.mouse_on_image) {
                    request.kind = GpuPickRequestKind::SetOrbitPivot;
                    request.mouse_x = frame.mouse_local_x;
                    request.mouse_y = frame.mouse_local_y;
                    request.pick_radius_px =
                        compute_hover_pick_radius_px(push.point_size);
                    continue;
                }

                // Measurement pick: middle-click uses the latest hover
                // pick result (zero-latency, same strategy as orbit pivot).
                if (frame.measure_pick_requested &&
                    app_state.measurement.measure_mode_active()) {
                    const auto idx =
                        static_cast<std::size_t>(frame.index);
                    if (idx < latest_gpu_hover_points.size() &&
                        latest_gpu_hover_points[idx].has_value()) {
                        app_state.measurement.add_point(
                            *latest_gpu_hover_points[idx]);
                    }
                }

                // Issue hover pick whenever the cursor is on the image,
                // regardless of ImGui hover state.  Ghost viewports can
                // consume the ImGui hit-test even though the cursor is
                // visually over the rendered data; the pick result's
                // has_hit is the ground truth for "cursor on data".
                // Reset hover timeout when a pick can be issued (cursor
                // on image, not dragging).  Increment when we skip.
                {
                    const auto idx =
                        static_cast<std::size_t>(frame.index);
                    if (idx < hover_timeout.size()) {
                        if (frame.active || !frame.mouse_on_image) {
                            ++hover_timeout[idx];
                        } else {
                            hover_timeout[idx] = 0;
                        }
                    }
                }

                if (frame.active || !frame.mouse_on_image) {
                    continue;
                }

                request.kind = GpuPickRequestKind::Hover;
                request.pick_radius_px =
                    compute_hover_pick_radius_px(push.point_size);
                if (benchmark_pick_enabled &&
                    frame.index == 0 &&
                    benchmark_pick_issue_index < benchmark_pick_queries.size()) {
                    request.benchmark_query_index = static_cast<int>(
                        benchmark_pick_queries[benchmark_pick_issue_index]
                            .query_index
                    );
                    ++benchmark_pick_issue_index;
                }
                request.mouse_x = frame.mouse_local_x;
                request.mouse_y = frame.mouse_local_y;
            }

            renderer.draw_frame(
                window,
                gs3d::render::VulkanRenderer::FrameDrawCallbacks{
                    .frame_ready = [&](std::uint32_t frame_slot) {
                        consume_ready_pick_frame_slot(frame_slot);
                    },
                    // pre_pass: all offscreen render passes execute here, before the
                    // swapchain render pass starts. Each viewport records its own
                    // vkCmdBeginRenderPass / draw / vkCmdEndRenderPass sequence into
                    // cmd; none of them nest inside each other or the swapchain pass.
                    .pre_pass = [&](VkCommandBuffer cmd) {
                        // ── 导航图缩略图重渲（着色属性变更时触发）──
                        if (nm.dirty && nav_thumbnail_fb.valid()) {
                            gs3d::camera::Camera nav_cam;
                            nav_cam.set_viewport(
                                static_cast<std::uint32_t>(nm.tex_w),
                                static_cast<std::uint32_t>(nm.tex_h)
                            );
                            const float nav_bbox_h =
                                nm.bbox_max_y - nm.bbox_min_y;
                            nav_cam.set_orthographic(
                                nav_bbox_h,
                                0.001f,
                                std::max(1.0f, nav_bbox_h * 10.0f)
                            );
                            const float nav_cx =
                                (nm.bbox_min_x + nm.bbox_max_x) * 0.5f;
                            const float nav_cy =
                                (nm.bbox_min_y + nm.bbox_max_y) * 0.5f;
                            nav_cam.look_at(
                                {nav_cx, nav_cy,
                                 dataset.bbox_max_z() + nav_bbox_h},
                                {nav_cx, nav_cy, 0.0f},
                                {0.0f, 1.0f, 0.0f}
                            );

                            const auto nav_mvp =
                                nav_cam.view_projection_matrix();
                            gs3d::render::PointPushConstants nav_push =
                                push;
                            std::memcpy(
                                nav_push.mvp,
                                nav_mvp.data(),
                                sizeof(nav_push.mvp)
                            );

                            const auto& nav_cloud =
                                lod_gpu_cloud
                                    ? lod_gpu_cloud->lowest_detail()
                                          .gpu_cloud
                                    : *full_gpu_cloud;

                            nav_thumbnail_fb.render(
                                cmd,
                                [&](VkCommandBuffer cb) {
                                    point_pipeline.draw(
                                        cb,
                                        nav_cloud,
                                        VkExtent2D{static_cast<std::uint32_t>(
                                             nm.tex_w),
                                         static_cast<std::uint32_t>(
                                             nm.tex_h)},
                                        nav_push
                                    );
                                }
                            );

                            nm.texture_descriptor =
                                nav_thumbnail_fb.imgui_descriptor();
                            nm.dirty = false;
                        }

                        bool pick_debug_dump_recorded_this_frame = false;
                        for (const int viewport_index :
                             visible_viewports) {
                            auto& framebuffer =
                                viewport_manager.framebuffer(
                                    viewport_index
                                );
                            const auto& viewport_camera =
                                viewport_manager.camera(
                                    viewport_index
                                );
                            const auto request_index =
                                static_cast<std::size_t>(viewport_index);
                            const auto& pick_request =
                                gpu_pick_requests[request_index];
                            const auto& selected_tile_ids =
                                viewport_tile_ids[request_index];
                            bool any_tile_resident = false;
                            bool all_tiles_resident =
                                !selected_tile_ids.empty();
                            std::vector<std::uint64_t> resident_tile_ids;
                            resident_tile_ids.reserve(
                                selected_tile_ids.size()
                            );
                            for (const auto tile_id : selected_tile_ids) {
                                const bool resident =
                                    tile_gpu_cloud &&
                                    tile_gpu_cloud
                                        ->has_resident_tile(tile_id);
                                any_tile_resident =
                                    any_tile_resident || resident;
                                all_tiles_resident =
                                    all_tiles_resident && resident;
                                if (resident) {
                                    resident_tile_ids.push_back(tile_id);
                                }
                            }
                            const bool tile_will_render =
                                (config_.interactive_display_mode !=
                                     gs3d::app::InteractiveDisplayMode::
                                         AllowCoarseLOD ||
                                 !interacting) &&
                                any_tile_resident;
                            framebuffer.render(
                                cmd,
                                [&](VkCommandBuffer c) {
                            // Per-viewport push: copy non-MVP fields from push,
                            // then fill in the per-viewport MVP matrix.
                            gs3d::render::PointPushConstants vp_push = push;
                            fill_push_constants(
                                vp_push,
                                viewport_camera
                            );

                            const VkExtent2D viewport_extent =
                                framebuffer.extent();

                            const auto view_index =
                                static_cast<std::size_t>(
                                    viewport_index
                                );
                            /*
                             * KeepStableHighQuality:交互期间继续绘制已驻留
                             * 的全分辨率 tile —— 新 tile 流式本就在交互期
                             * 冻结(见上方 !interacting 门控),所以这只是用
                             * 当前相机继续画"交互开始前已上传好的高质量
                             * buffer",无新上传、无中途驱逐,不空帧不闪烁。
                             * AllowCoarseLOD 才在交互期关掉 tile 叠加。
                             */
                            gs3d::render::PointPushConstants lod_push = vp_push;
                            /*
                             * Clip LOD inside tile-covered areas — but ONLY
                             * when every desired tile is resident, so a
                             * partial clip bbox never creates black holes.
                             * During incremental upload the clip stays off
                             * (LOD + tiles may overdraw, but no holes).
                             */
                            bool all_desired_resident =
                                !tile_result.tile_ids.empty();
                            if (all_desired_resident) {
                                for (const auto tid : tile_result.tile_ids) {
                                    if (!tile_gpu_cloud
                                            ->has_resident_tile(tid)) {
                                        all_desired_resident = false;
                                        break;
                                    }
                                }
                            }
                            if (tile_will_render &&
                                all_desired_resident &&
                                viewport_tile_query_boxes[view_index]
                                    .has_value()) {
                                const auto& b =
                                    *viewport_tile_query_boxes[view_index];
                                lod_push.flags |= gs3d::render::PointFlags::kSpatialClip;
                                lod_push.clip_min[0] = b.min_x;
                                lod_push.clip_min[1] = b.min_y;
                                lod_push.clip_min[2] = b.min_z;
                                // lod_push.clip_min[3] 保留 value_clip_min（可能已设置）
                                lod_push.clip_max[0] = b.max_x;
                                lod_push.clip_max[1] = b.max_y;
                                lod_push.clip_max[2] = b.max_z;
                                // lod_push.clip_max[3] 保留 value_clip_max（可能已设置）
                            }

                            point_pipeline.bind_for_viewport(
                                c,
                                viewport_extent
                            );

                            // --- LOD safety net: coarsest level, always drawn ---
                            // spatial_clip=0 so it is never clipped
                            // — guarantees no clear-colour holes.
                            if (config_.lod_enabled) {
                                gs3d::render::PointPushConstants safety_push =
                                    lod_push;
                                safety_push.flags &= ~gs3d::render::PointFlags::kSpatialClip;
                                point_pipeline.draw_per_tile(
                                    c,
                                    lod_gpu_cloud->lowest_detail().gpu_cloud,
                                    safety_push
                                );
                            }

                            if (config_.lod_enabled) {
                                point_pipeline.draw_per_tile(
                                    c,
                                    lod_gpu_cloud->gpu_cloud(lod_level_for_frame),
                                    lod_push
                                );
                            } else {
                                point_pipeline.draw_per_tile(
                                    c,
                                    *full_gpu_cloud,
                                    lod_push
                                );
                            }

                            if (tile_will_render) {
                                for (const auto tile_id :
                                     selected_tile_ids) {
                                    if (!tile_gpu_cloud
                                            ->has_resident_tile(tile_id)) {
                                        continue;
                                    }
                                    point_pipeline.draw_per_tile(
                                        c,
                                        tile_gpu_cloud->gpu_cloud_for_tile(tile_id),
                                        vp_push
                                    );
                                }
                            }
                                }
                            );
                            if (pick_request.valid()) {
                                gs3d::util::Stopwatch issue_timer;
                                gpu_pick_readback.record_request(
                                    cmd,
                                    gpu_pick_frame_slot,
                                    framebuffer,
                                    pick_request
                                );
                                if (benchmark_pick_enabled &&
                                    pick_request.benchmark_query_index >= 0) {
                                    const auto query_index =
                                        static_cast<std::size_t>(
                                            pick_request.benchmark_query_index
                                        );
                                    if (query_index <
                                        benchmark_pick_issue_cpu_ms.size()) {
                                        benchmark_pick_issue_cpu_ms[query_index] =
                                            issue_timer
                                                .elapsed_milliseconds();
                                    }
                                    if (query_index <
                                        benchmark_pick_issue_metadata.size()) {
                                        auto& metadata =
                                            benchmark_pick_issue_metadata[
                                                query_index
                                            ];
                                        metadata.all_tiles_resident =
                                            all_tiles_resident;
                                        metadata.resident_tile_ids.clear();
                                        metadata.resident_tile_ids =
                                            resident_tile_ids;
                                    }
                                }
                                const bool should_dump_pick_debug =
                                    config_.pick_debug_dump_enabled &&
                                    pick_request.kind ==
                                        GpuPickRequestKind::Hover &&
                                    !pick_debug_dump_recorded_this_frame &&
                                    (!config_.pick_debug_dump_once_on_hover ||
                                     !pick_debug_dump_completed ||
                                     pending_hover_miss_dump[viewport_index]);
                                if (should_dump_pick_debug) {
                                    const int cursor_x = std::clamp(
                                        static_cast<int>(
                                            std::floor(pick_request.mouse_x)
                                        ),
                                        0,
                                        static_cast<int>(
                                            pick_request.viewport_width
                                        ) - 1
                                    );
                                    const int cursor_y = std::clamp(
                                        static_cast<int>(
                                            std::floor(pick_request.mouse_y)
                                        ),
                                        0,
                                        static_cast<int>(
                                            pick_request.viewport_height
                                        ) - 1
                                    );
                                    const std::uint32_t sample_left =
                                        static_cast<std::uint32_t>(
                                            std::max(
                                                0,
                                                cursor_x - 5
                                            )
                                        );
                                    const std::uint32_t sample_top =
                                        static_cast<std::uint32_t>(
                                            std::max(
                                                0,
                                                cursor_y - 5
                                            )
                                        );
                                    const std::uint32_t sample_right =
                                        static_cast<std::uint32_t>(
                                            std::min(
                                                static_cast<int>(
                                                    pick_request.viewport_width
                                                ) - 1,
                                                cursor_x + 5
                                            )
                                        );
                                    const std::uint32_t sample_bottom =
                                        static_cast<std::uint32_t>(
                                            std::min(
                                                static_cast<int>(
                                                    pick_request.viewport_height
                                                ) - 1,
                                                cursor_y + 5
                                            )
                                        );

                                    PickDebugDumpMetadata debug_metadata;
                                    debug_metadata.dump_index =
                                        pick_debug_dump_count++;
                                    debug_metadata.frame_index =
                                        app_frame_index;
                                    debug_metadata.viewport_index =
                                        viewport_index;
                                    debug_metadata.viewport_width =
                                        pick_request.viewport_width;
                                    debug_metadata.viewport_height =
                                        pick_request.viewport_height;
                                    debug_metadata.mouse_x =
                                        pick_request.mouse_x;
                                    debug_metadata.mouse_y =
                                        pick_request.mouse_y;
                                    debug_metadata.sample_left =
                                        sample_left;
                                    debug_metadata.sample_top =
                                        sample_top;
                                    debug_metadata.sample_width =
                                        sample_right - sample_left + 1;
                                    debug_metadata.sample_height =
                                        sample_bottom - sample_top + 1;
                                    debug_metadata.active_lod_level =
                                        lod_level_for_frame;
                                    debug_metadata.tile_overlay_rendered =
                                        tile_will_render;
                                    debug_metadata.all_tiles_resident =
                                        all_tiles_resident;
                                    debug_metadata.render_source =
                                        tile_will_render
                                            ? "tile_overlay+lod_level_" +
                                                  std::to_string(
                                                      lod_level_for_frame
                                                  )
                                            : "lod_level_" +
                                                  std::to_string(
                                                      lod_level_for_frame
                                                  );
                                    debug_metadata.trigger_reason =
                                        pending_hover_miss_dump[viewport_index]
                                            ? "followup_after_hover_miss"
                                            : "hover_frame";
                                    debug_metadata.selected_tile_ids =
                                        selected_tile_ids;
                                    debug_metadata.resident_tile_ids =
                                        resident_tile_ids;
                                    if (pick_debug_frame_dumper.record_request(
                                            cmd,
                                            gpu_pick_frame_slot,
                                            framebuffer,
                                            debug_metadata
                                        )) {
                                        pick_debug_dump_recorded_this_frame =
                                            true;
                                        pending_hover_miss_dump[viewport_index] =
                                            false;
                                        if (config_
                                                .pick_debug_dump_once_on_hover) {
                                            pick_debug_dump_completed = true;
                                        }
                                    }
                                }
                            }
                        }
                    },
                    // in_pass: only ImGui runs in the swapchain render pass.
                    // Each ImGui::Image() samples its viewport's offscreen texture.
                    .in_pass = [&](VkCommandBuffer cmd) {
                        imgui_layer.render(cmd);
                    },
                    // post_pass: after the swapchain render pass ends, copy
                    // the viewport region to a staging buffer for screenshots.
                    .post_pass = [&](VkCommandBuffer cmd, std::uint32_t image_index) {
                        if (!screenshot_pending) return;

                        const VkDeviceSize buf_size =
                            static_cast<VkDeviceSize>(
                                screenshot_extent.width) *
                            static_cast<VkDeviceSize>(
                                screenshot_extent.height) * 4;

                        // Create staging buffer on first use (or re-create if
                        // extent changed since last screenshot).
                        if (screenshot_staging_buf == VK_NULL_HANDLE) {
                            VkBufferCreateInfo buf_info{};
                            buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                            buf_info.size = buf_size;
                            buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                            buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                            if (vkCreateBuffer(context.device(), &buf_info,
                                               nullptr, &screenshot_staging_buf) != VK_SUCCESS) {
                                std::cerr << "[SCREENSHOT] buffer create failed\n";
                                screenshot_pending = false;
                                return;
                            }

                            VkMemoryRequirements mem_req{};
                            vkGetBufferMemoryRequirements(context.device(),
                                                          screenshot_staging_buf,
                                                          &mem_req);
                            VkMemoryAllocateInfo alloc_info{};
                            alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                            alloc_info.allocationSize = mem_req.size;

                            VkPhysicalDeviceMemoryProperties mem_props{};
                            vkGetPhysicalDeviceMemoryProperties(
                                context.physical_device(), &mem_props);
                            std::uint32_t mem_type_idx = 0;
                            for (; mem_type_idx < mem_props.memoryTypeCount; ++mem_type_idx) {
                                if ((mem_req.memoryTypeBits & (1u << mem_type_idx)) &&
                                    (mem_props.memoryTypes[mem_type_idx].propertyFlags &
                                     (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                                        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                                    break;
                                }
                            }
                            alloc_info.memoryTypeIndex = mem_type_idx;
                            if (vkAllocateMemory(context.device(), &alloc_info,
                                                 nullptr, &screenshot_staging_mem) != VK_SUCCESS) {
                                std::cerr << "[SCREENSHOT] memory alloc failed\n";
                                vkDestroyBuffer(context.device(), screenshot_staging_buf, nullptr);
                                screenshot_staging_buf = VK_NULL_HANDLE;
                                screenshot_pending = false;
                                return;
                            }
                            if (vkBindBufferMemory(context.device(),
                                                   screenshot_staging_buf,
                                                   screenshot_staging_mem, 0) != VK_SUCCESS) {
                                std::cerr << "[SCREENSHOT] bind memory failed\n";
                                vkFreeMemory(context.device(), screenshot_staging_mem, nullptr);
                                vkDestroyBuffer(context.device(), screenshot_staging_buf, nullptr);
                                screenshot_staging_buf = VK_NULL_HANDLE;
                                screenshot_staging_mem = VK_NULL_HANDLE;
                                screenshot_pending = false;
                                return;
                            }
                        }

                        const VkImage src_img = swapchain.images()[image_index];

                        // PRESENT_SRC_KHR → TRANSFER_SRC_OPTIMAL
                        {
                            VkImageMemoryBarrier barrier{};
                            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                            barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
                            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                            barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                            barrier.image = src_img;
                            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                            vkCmdPipelineBarrier(cmd,
                                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                0, 0, nullptr, 0, nullptr, 1, &barrier);
                        }

                        // Copy viewport region
                        {
                            VkBufferImageCopy region{};
                            region.bufferOffset = 0;
                            region.bufferRowLength = 0;
                            region.bufferImageHeight = 0;
                            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                            region.imageOffset = {
                                static_cast<std::int32_t>(screenshot_offset.width),
                                static_cast<std::int32_t>(screenshot_offset.height),
                                0
                            };
                            region.imageExtent = {
                                screenshot_extent.width,
                                screenshot_extent.height,
                                1
                            };
                            vkCmdCopyImageToBuffer(cmd, src_img,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                screenshot_staging_buf, 1, &region);
                        }

                        // TRANSFER_SRC_OPTIMAL → PRESENT_SRC_KHR
                        {
                            VkImageMemoryBarrier barrier{};
                            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
                            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                            barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                            barrier.image = src_img;
                            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                            vkCmdPipelineBarrier(cmd,
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                0, 0, nullptr, 0, nullptr, 1, &barrier);
                        }
                    }
                }
            );
            const double benchmark_draw_record_ms_frame =
                renderer.last_draw_record_cpu_ms();
            const double benchmark_acquire_wait_ms_frame =
                renderer.last_acquire_wait_ms();
            const double benchmark_frame_fence_wait_ms_frame =
                renderer.last_frame_fence_wait_ms();
            const double benchmark_upload_fence_wait_ms_frame =
                renderer.last_upload_fence_wait_ms();
            benchmark_cpu_frame_ms =
                benchmark_camera_update_ms_frame +
                benchmark_lod_tile_select_ms_frame +
                benchmark_cpu_cull_ms_frame +
                benchmark_upload_record_ms_frame +
                benchmark_draw_record_ms_frame;
            // Render ImGui platform windows (docked panels torn out to separate
            // OS windows). Must happen outside the main render pass.
            imgui_layer.render_platform_windows();
            // If draw_frame() returned early (minimized / swapchain out-of-date)
            // the draw callback was never invoked, so close the dangling ImGui frame.
            imgui_layer.discard_frame();

            // ── Screenshot PNG write ──────────────────────────────────
            if (screenshot_pending &&
                screenshot_staging_buf != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(context.device());

                const auto w = static_cast<int>(screenshot_extent.width);
                const auto h = static_cast<int>(screenshot_extent.height);
                const VkDeviceSize buf_size =
                    static_cast<VkDeviceSize>(w) * static_cast<VkDeviceSize>(h) * 4;

                void* mapped = nullptr;
                vkMapMemory(context.device(), screenshot_staging_mem,
                            0, buf_size, 0, &mapped);
                auto* pixels = static_cast<std::uint8_t*>(mapped);

                // BGR→RGB swizzle if swapchain uses B8G8R8A8 format
                const VkFormat fmt = swapchain.image_format();
                if (fmt == VK_FORMAT_B8G8R8A8_UNORM ||
                    fmt == VK_FORMAT_B8G8R8A8_SRGB) {
                    for (int i = 0; i < w * h; ++i) {
                        std::swap(pixels[i * 4], pixels[i * 4 + 2]);
                    }
                }

                // Resolve output path: zenity dialog → fallback.
                // If zenity is available and the user confirms, use that
                // path. If the user cancels (zenity exit ≠ 0), skip save.
                // If zenity is not available, fall back to timestamped file.
                std::string out_path;
                bool user_cancelled = false;
                FILE* zf = popen(
                    "zenity --file-selection --save "
                    "--confirm-overwrite "
                    "--filename=screenshot.png "
                    "--file-filter='PNG Images | *.png' "
                    "2>/dev/null", "r");
                if (zf) {
                    char buf[4096];
                    if (fgets(buf, sizeof(buf), zf)) {
                        out_path.assign(buf);
                        while (!out_path.empty() &&
                               (out_path.back() == '\n' ||
                                out_path.back() == '\r')) {
                            out_path.pop_back();
                        }
                    }
                    int zr = pclose(zf);
                    user_cancelled = (out_path.empty() && zr != 0);
                }
                if (!user_cancelled && out_path.empty()) {
                    std::filesystem::create_directories("screenshots");
                    const auto now = std::chrono::system_clock::now();
                    const auto tt = std::chrono::system_clock::to_time_t(now);
                    char ts[64];
                    std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S",
                                  std::localtime(&tt));
                    out_path = std::string("screenshots/screenshot_") +
                               ts + ".png";
                }
                if (user_cancelled || out_path.empty()) {
                    std::cout << "[SCREENSHOT] cancelled.\n";
                } else if (!stbi_write_png(out_path.c_str(), w, h, 4,
                                           pixels, w * 4)) {
                    std::cerr << "[SCREENSHOT] stbi_write_png failed: "
                              << out_path << '\n';
                } else {
                    std::cout << "[SCREENSHOT] saved: " << out_path << '\n';
                }

                vkUnmapMemory(context.device(), screenshot_staging_mem);
                vkDestroyBuffer(context.device(),
                                screenshot_staging_buf, nullptr);
                vkFreeMemory(context.device(),
                             screenshot_staging_mem, nullptr);
                screenshot_staging_buf = VK_NULL_HANDLE;
                screenshot_staging_mem = VK_NULL_HANDLE;
                screenshot_pending = false;
            }

            // Rebuild framebuffer resources only after the user stops resizing.
            // All ready viewports share one device-idle synchronization point.
            const auto ready_resizes =
                viewport_resize_scheduler.take_ready(now_seconds);
            std::vector<gs3d::render::ViewportResizeRequest>
                resize_requests;
            resize_requests.reserve(ready_resizes.size());
            for (const auto& resize : ready_resizes) {
                resize_requests.push_back({
                    resize.index,
                    {resize.width, resize.height}
                });
                if (resize.index == streaming_viewport_index) {
                    tile_selection_dirty = true;
                }
            }
            viewport_manager.resize_many(resize_requests);

            if (config_.benchmark_mode) {
                benchmark_wall_frame_times_ms.push_back(
                    benchmark_frame_timer.elapsed_milliseconds()
                );
                benchmark_cpu_frame_times_ms.push_back(
                    benchmark_cpu_frame_ms
                );
                benchmark_camera_update_ms.push_back(
                    benchmark_camera_update_ms_frame
                );
                benchmark_lod_tile_select_ms.push_back(
                    benchmark_lod_tile_select_ms_frame
                );
                benchmark_cpu_cull_ms.push_back(
                    benchmark_cpu_cull_ms_frame
                );
                benchmark_upload_record_ms.push_back(
                    benchmark_upload_record_ms_frame
                );
                benchmark_draw_record_ms.push_back(
                    benchmark_draw_record_ms_frame
                );
                benchmark_acquire_wait_ms.push_back(
                    benchmark_acquire_wait_ms_frame
                );
                benchmark_frame_fence_wait_ms.push_back(
                    benchmark_frame_fence_wait_ms_frame
                );
                benchmark_upload_fence_wait_ms.push_back(
                    benchmark_upload_fence_wait_ms_frame
                );
                if (renderer.has_last_gpu_frame_ms()) {
                    benchmark_gpu_frame_times_ms.push_back(
                        renderer.last_gpu_frame_ms()
                    );
                }
                ++benchmark_frame_index;
            }
            ++app_frame_index;
        }

        vkDeviceWaitIdle(context.device());

        if (config_.benchmark_mode) {
            const auto present_mode_label =
                [](VkPresentModeKHR present_mode) -> const char* {
                    switch (present_mode) {
                    case VK_PRESENT_MODE_IMMEDIATE_KHR:
                        return "IMMEDIATE";
                    case VK_PRESENT_MODE_MAILBOX_KHR:
                        return "MAILBOX";
                    case VK_PRESENT_MODE_FIFO_KHR:
                        return "FIFO";
                    default:
                        return "OTHER";
                    }
                };
            std::cout << "[BENCH] frame_count = "
                      << benchmark_wall_frame_times_ms.size() << '\n';
            std::cout << "[BENCH] present_mode = "
                      << present_mode_label(swapchain.present_mode())
                      << '\n';
            print_benchmark_percentiles(
                "wall_frame_ms",
                benchmark_wall_frame_times_ms
            );
            print_benchmark_percentiles(
                "cpu_frame_ms",
                benchmark_cpu_frame_times_ms
            );
            print_benchmark_percentiles(
                "gpu_frame_ms",
                benchmark_gpu_frame_times_ms
            );
            print_benchmark_percentiles(
                "camera_update_ms",
                benchmark_camera_update_ms
            );
            print_benchmark_percentiles(
                "lod_tile_select_ms",
                benchmark_lod_tile_select_ms
            );
            print_benchmark_percentiles(
                "cpu_cull_ms",
                benchmark_cpu_cull_ms
            );
            print_benchmark_percentiles(
                "upload_record_ms",
                benchmark_upload_record_ms
            );
            print_benchmark_percentiles(
                "draw_record_ms",
                benchmark_draw_record_ms
            );
            print_benchmark_percentiles(
                "acquire_wait_ms",
                benchmark_acquire_wait_ms
            );
            print_benchmark_percentiles(
                "frame_fence_wait_ms",
                benchmark_frame_fence_wait_ms
            );
            print_benchmark_percentiles(
                "upload_fence_wait_ms",
                benchmark_upload_fence_wait_ms
            );
            std::cout << "[BENCH] hover_pick = skipped "
                      << "(cursor-dependent, not part of fixed benchmark path)\n";
            if (!benchmark_reload_seconds.empty()) {
                std::cout << "[BENCH] reload_latency_seconds_p50 = "
                          << gs3d::util::percentile(
                                 benchmark_reload_seconds, 50.0)
                          << '\n';
                std::cout << "[BENCH] reload_latency_seconds_p95 = "
                          << gs3d::util::percentile(
                                 benchmark_reload_seconds, 95.0)
                          << '\n';
            } else {
                std::cout
                    << "[BENCH] reload_latency: "
                    << "no completed tile uploads captured.\n";
            }
        }

        if (benchmark_pick_enabled &&
            !config_.benchmark_pick_result_path.empty()) {
            write_benchmark_pick_results(
                config_.benchmark_pick_result_path,
                benchmark_pick_results
            );
            std::cout << "[BENCH] pick_result_path = "
                      << config_.benchmark_pick_result_path.string()
                      << '\n';
            std::cout << "[BENCH] pick_result_count = "
                      << benchmark_pick_results.size()
                      << '\n';
        }

        std::cout << "[PASS] ViewerApp finished.\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}

} // namespace gs3d::app
