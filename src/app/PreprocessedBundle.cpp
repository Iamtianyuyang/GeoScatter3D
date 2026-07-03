#include "app/PreprocessedBundle.hpp"

#include <toml++/toml.hpp>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace gs3d::app {

namespace {

constexpr std::string_view kManifestFileName = "manifest.toml";
constexpr std::string_view kGs3dFileName = "source.gs3d";
constexpr std::string_view kLodFileName = "lod.gs3dlod";
constexpr std::string_view kTileDataFileName = "tiles.gs3dtiles";
constexpr std::string_view kTileIndexFileName = "tiles.gs3dtiles.index";

[[nodiscard]]
std::string toml_escape(const std::string& value) {
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
std::string toml_string(const std::filesystem::path& value) {
    return "\"" + toml_escape(value.string()) + "\"";
}

[[nodiscard]]
std::string toml_string(const std::string& value) {
    return "\"" + toml_escape(value) + "\"";
}

[[nodiscard]]
bool bool_or_default(
    const toml::table& table,
    std::string_view key,
    bool default_value
) {
    if (const auto value = table[key].value<bool>()) {
        return *value;
    }

    return default_value;
}

[[nodiscard]]
std::string file_name_or_throw(
    const toml::table& table,
    std::string_view key,
    std::string_view manifest_path
) {
    if (const auto value = table[key].value<std::string>()) {
        if (!value->empty()) {
            return *value;
        }
    }

    throw std::runtime_error(
        "PreprocessedBundle: missing files." +
        std::string(key) +
        " in " +
        std::string(manifest_path)
    );
}

[[nodiscard]]
std::filesystem::path require_existing_file(
    const std::filesystem::path& path,
    const std::string& label
) {
    if (!std::filesystem::exists(path) ||
        !std::filesystem::is_regular_file(path)) {
        throw std::runtime_error(
            "PreprocessedBundle: missing " +
            label +
            ": " +
            path.string()
        );
    }

    return path;
}

} // namespace

PreprocessedBundlePaths derive_bundle_paths(
    const std::filesystem::path& source_path
) {
    auto bundle_dir = source_path;
    bundle_dir.replace_extension(".gs3d.bundle");
    return make_bundle_paths(bundle_dir);
}

PreprocessedBundlePaths make_bundle_paths(
    const std::filesystem::path& bundle_dir
) {
    PreprocessedBundlePaths paths;
    paths.bundle_dir = bundle_dir;
    paths.manifest_path = bundle_dir / kManifestFileName;
    paths.gs3d_path = bundle_dir / kGs3dFileName;
    paths.lod_path = bundle_dir / kLodFileName;
    paths.tile_data_path = bundle_dir / kTileDataFileName;
    paths.tile_index_path = bundle_dir / kTileIndexFileName;
    return paths;
}

void apply_bundle_paths(
    ViewerAppConfig& viewer,
    const PreprocessedBundlePaths& paths
) {
    viewer.gs3d_path = paths.gs3d_path;
    viewer.lod_enabled = paths.lod_enabled;
    viewer.tile_enabled = paths.tile_enabled;
    viewer.lod_sidecar_path =
        paths.lod_enabled ? paths.lod_path : std::filesystem::path{};
    viewer.tile_data_path =
        paths.tile_enabled ? paths.tile_data_path : std::filesystem::path{};
    viewer.tile_index_path =
        paths.tile_enabled ? paths.tile_index_path : std::filesystem::path{};
}

void write_bundle_manifest(
    const PreprocessedBundlePaths& paths,
    const AppConfig& config,
    const gs3d::data::Gs3dDataset& dataset
) {
    std::filesystem::create_directories(paths.bundle_dir);

    std::ofstream out(paths.manifest_path);
    if (!out.is_open()) {
        throw std::runtime_error(
            "PreprocessedBundle: failed to open manifest for write: " +
            paths.manifest_path.string()
        );
    }

    std::uint64_t source_size = 0;
    if (!config.csv_input_path.empty() &&
        std::filesystem::exists(config.csv_input_path)) {
        std::error_code ec;
        source_size = std::filesystem::file_size(
            config.csv_input_path,
            ec
        );
        if (ec) {
            source_size = 0;
        }
    }

    out << "[bundle]\n";
    out << "version = 1\n";
    out << "source_mode = " << toml_string(config.input_mode) << "\n";
    out << "source_path = " << toml_string(config.csv_input_path) << "\n";
    out << "source_size_bytes = " << source_size << "\n";
    out << "\n";

    out << "[files]\n";
    out << "gs3d = " << toml_string(std::string(kGs3dFileName)) << "\n";
    out << "lod = " << toml_string(std::string(kLodFileName)) << "\n";
    out << "tile_data = " << toml_string(std::string(kTileDataFileName))
        << "\n";
    out << "tile_index = " << toml_string(std::string(kTileIndexFileName))
        << "\n";
    out << "\n";

    out << "[dataset]\n";
    out << "point_count = " << dataset.point_count() << "\n";
    out << "bbox_min = ["
        << dataset.bbox_min_x() << ", "
        << dataset.bbox_min_y() << ", "
        << dataset.bbox_min_z() << "]\n";
    out << "bbox_max = ["
        << dataset.bbox_max_x() << ", "
        << dataset.bbox_max_y() << ", "
        << dataset.bbox_max_z() << "]\n";
    out << "value_min = " << dataset.value_min() << "\n";
    out << "value_max = " << dataset.value_max() << "\n";
    out << "origin = ["
        << dataset.origin_x() << ", "
        << dataset.origin_y() << ", "
        << dataset.origin_z() << "]\n";
    out << "\n";

    out << "[csv_convert]\n";
    out << "x_field = " << toml_string(config.csv_convert.x_field) << "\n";
    out << "y_field = " << toml_string(config.csv_convert.y_field) << "\n";
    out << "z_field = " << toml_string(config.csv_convert.z_field) << "\n";
    out << "primary_value_field = "
        << toml_string(config.csv_convert.primary_value_field) << "\n";
    out << "num_threads = " << config.csv_convert.num_threads << "\n";
    out << "chunk_bytes = " << config.csv_convert.chunk_bytes << "\n";
    out << "min_parallel_file_bytes = "
        << config.csv_convert.min_parallel_file_bytes << "\n";
    out << "\n";

    out << "[lod]\n";
    out << "enabled = " << (config.viewer.lod_enabled ? "true" : "false")
        << "\n";
    out << "finest_target_points = "
        << config.viewer.lod_finest_target_points << "\n";
    out << "growth_factor = " << config.viewer.lod_growth_factor << "\n";
    out << "min_points_per_level = "
        << config.viewer.lod_min_points_per_level << "\n";
    out << "voxel_mode = " << toml_string(config.viewer.lod_voxel_mode)
        << "\n";
    out << "voxel_scale = " << config.viewer.lod_voxel_scale << "\n";
    out << "\n";

    out << "[tile]\n";
    out << "enabled = " << (config.viewer.tile_enabled ? "true" : "false")
        << "\n";
    out << "num_threads = " << config.tile_build.num_threads << "\n";
    out << "min_tile_pixel_size = " << config.viewer.tile_min_pixel_size
        << "\n";
    out << "max_visible_tiles = " << config.viewer.tile_max_visible_tiles
        << "\n";
}

PreprocessedBundlePaths load_bundle_manifest(
    const std::filesystem::path& bundle_dir
) {
    const auto default_paths = make_bundle_paths(bundle_dir);

    if (!std::filesystem::exists(default_paths.bundle_dir) ||
        !std::filesystem::is_directory(default_paths.bundle_dir)) {
        throw std::runtime_error(
            "PreprocessedBundle: bundle directory not found: " +
            default_paths.bundle_dir.string()
        );
    }

    toml::table root;
    try {
        root = toml::parse_file(default_paths.manifest_path.string());
    } catch (const toml::parse_error& e) {
        throw std::runtime_error(
            "PreprocessedBundle: failed to parse manifest: " +
            default_paths.manifest_path.string() +
            "\nReason: " +
            std::string(e.description())
        );
    }

    const auto* files = root["files"].as_table();
    if (!files) {
        throw std::runtime_error(
            "PreprocessedBundle: manifest missing [files]: " +
            default_paths.manifest_path.string()
        );
    }
    const auto* lod = root["lod"].as_table();
    const auto* tile = root["tile"].as_table();

    PreprocessedBundlePaths paths;
    paths.bundle_dir = default_paths.bundle_dir;
    paths.manifest_path = default_paths.manifest_path;
    paths.lod_enabled = lod ? bool_or_default(*lod, "enabled", false) : false;
    paths.tile_enabled =
        tile ? bool_or_default(*tile, "enabled", false) : false;
    paths.gs3d_path = paths.bundle_dir /
        file_name_or_throw(
            *files,
            "gs3d",
            paths.manifest_path.string()
        );

    [[maybe_unused]] const auto manifest_path =
        require_existing_file(paths.manifest_path, "bundle manifest");
    [[maybe_unused]] const auto gs3d_path =
        require_existing_file(paths.gs3d_path, "bundle gs3d");

    if (paths.lod_enabled) {
        paths.lod_path = paths.bundle_dir /
            file_name_or_throw(
                *files,
                "lod",
                paths.manifest_path.string()
            );
        [[maybe_unused]] const auto lod_path =
            require_existing_file(paths.lod_path, "bundle lod sidecar");
    }

    if (paths.tile_enabled) {
        paths.tile_data_path = paths.bundle_dir /
            file_name_or_throw(
                *files,
                "tile_data",
                paths.manifest_path.string()
            );
        paths.tile_index_path = paths.bundle_dir /
            file_name_or_throw(
                *files,
                "tile_index",
                paths.manifest_path.string()
            );

        [[maybe_unused]] const auto tile_data_path =
            require_existing_file(paths.tile_data_path, "bundle tile data");
        [[maybe_unused]] const auto tile_index_path =
            require_existing_file(paths.tile_index_path, "bundle tile index");
    }

    return paths;
}

} // namespace gs3d::app
