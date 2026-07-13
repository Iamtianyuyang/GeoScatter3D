#include "app/ViewerDatasetSession.hpp"

#include "app/ViewerApp.hpp"
#include "app/ViewerAppInternal.hpp"
#include "data/Gs3dLodReader.hpp"
#include "data/Gs3dReader.hpp"
#include "data/TileDataAdapters.hpp"
#include "util/Log.hpp"
#include "util/Stopwatch.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace gs3d::app {

namespace {

[[nodiscard]] gs3d::data::Gs3dLodDataset load_lod_dataset(
    const gs3d::data::Gs3dDataset& dataset,
    const ViewerAppConfig& config
) {
    if (!config.lod.enabled) {
        return {};
    }

    const auto& sidecar_path = config.lod.sidecar_path;
    if (!config.lod.auto_load_sidecar) {
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
    read_config.verbose = config.lod.verbose;
    const auto read_result = gs3d::data::Gs3dLodReader::read(
        sidecar_path,
        dataset.header(),
        read_config
    );

    gs3d::util::log::info() << "[OK] LOD sidecar loaded.\n";
    gs3d::util::log::info() << "path = " << sidecar_path.string() << '\n';
    return read_result.dataset;
}

void print_dataset_info(const gs3d::data::Gs3dDataset& dataset) {
    gs3d::util::log::info() << "[OK] Dataset loaded.\n";
    gs3d::util::log::info() << "point_count = " << dataset.point_count() << '\n';
    gs3d::util::log::info() << "loaded_point_bytes = "
                            << dataset.point_bytes() << '\n';
    gs3d::util::log::info() << "metadata_only = "
                            << (dataset.metadata_only() ? "true" : "false")
                            << '\n';
    gs3d::util::log::info() << "bbox_min = ["
                            << dataset.bbox_min_x() << ", "
                            << dataset.bbox_min_y() << ", "
                            << dataset.bbox_min_z() << "]\n";
    gs3d::util::log::info() << "bbox_max = ["
                            << dataset.bbox_max_x() << ", "
                            << dataset.bbox_max_y() << ", "
                            << dataset.bbox_max_z() << "]\n";
    gs3d::util::log::info() << "value_range = ["
                            << dataset.value_min() << ", "
                            << dataset.value_max() << "]\n";
}

} // namespace

std::optional<ViewerDatasetSession> prepare_viewer_dataset(
    const ViewerAppConfig& config
) {
    const bool can_start_from_metadata =
        config.lod.enabled &&
        !config.lod.keep_full_buffer &&
        config.lod.auto_load_sidecar &&
        !config.lod.sidecar_path.empty() &&
        std::filesystem::exists(config.lod.sidecar_path);

    gs3d::util::Stopwatch dataset_load_timer;
    ViewerDatasetSession session;
    session.dataset = can_start_from_metadata
        ? gs3d::data::Gs3dDatasetLoader::load_header_only(
            config.input.gs3d_path
        )
        : gs3d::data::Gs3dDatasetLoader::load(config.input.gs3d_path);
    gs3d::util::log::info() << "[TIME] viewer.dataset_load_seconds = "
                            << dataset_load_timer.elapsed_seconds() << '\n';

    if (!session.dataset.is_consistent()) {
        gs3d::util::log::error() << "[FAIL] dataset is inconsistent.\n";
        return std::nullopt;
    }
    if (session.dataset.point_count() == 0) {
        gs3d::util::log::error() << "[FAIL] dataset is empty.\n";
        return std::nullopt;
    }

    print_dataset_info(session.dataset);
    session.full_point_ids = make_runtime_point_ids(session.dataset.point_count());

    if (config.tile.enabled) {
        gs3d::util::Stopwatch tile_reader_timer;
        session.tile_reader = gs3d::data::Gs3dTileReader::open(
            config.tile.index_path,
            config.tile.data_path,
            session.dataset.header()
        );
        gs3d::util::log::info() << "[TIME] viewer.tile_reader_open_seconds = "
                                << tile_reader_timer.elapsed_seconds() << '\n';
        if (!session.tile_reader->valid()) {
            gs3d::util::log::error() << "[FAIL] TileReader is invalid.\n";
            return std::nullopt;
        }

        const auto tile_stats = session.tile_reader->stats();
        gs3d::util::log::info() << "[OK] TileReader opened.\n";
        gs3d::util::log::info() << "tile_count = "
                                << tile_stats.tile_count << '\n';
        gs3d::util::log::info() << "tile_total_point_count = "
                                << tile_stats.total_point_count << '\n';
        gs3d::util::log::info() << "tile_total_point_bytes = "
                                << tile_stats.total_point_bytes << '\n';
        session.tile_index_view =
            gs3d::data::make_tile_index_view(*session.tile_reader);

        if (session.tile_reader->has_embedded_point_ids()) {
            gs3d::util::log::info()
                << "[OK] Tile format v2 — embedded point IDs. "
                << "Fast startup (no source point scan needed).\n";
        } else {
            gs3d::util::log::info()
                << "[INFO] Tile format v1 — no embedded point IDs. "
                << "Using slow startup path.\n";
            if (session.dataset.metadata_only()) {
                gs3d::util::log::info()
                    << "[WARN] Metadata-only startup cannot build "
                    << "tile runtime ids; loading full GS3D data.\n";
                gs3d::util::Stopwatch fallback_load_timer;
                session.dataset = gs3d::data::Gs3dDatasetLoader::load(
                    config.input.gs3d_path
                );
                gs3d::util::log::info()
                    << "[TIME] viewer.dataset_fallback_load_seconds = "
                    << fallback_load_timer.elapsed_seconds() << '\n';
            }
            session.tile_point_ids_by_tile = build_runtime_tile_point_ids(
                session.dataset,
                *session.tile_reader,
                session.full_point_ids
            );
        }
    }

    if (config.lod.enabled && session.dataset.metadata_only()) {
        gs3d::util::log::info()
            << "[INFO] LOD enabled — loading full GS3D data "
            << "for point-id mapping.\n";
        gs3d::util::Stopwatch lod_load_timer;
        session.dataset = gs3d::data::Gs3dDatasetLoader::load(
            config.input.gs3d_path
        );
        gs3d::util::log::info() << "[TIME] viewer.lod_dataset_load_seconds = "
                                << lod_load_timer.elapsed_seconds() << '\n';
    }

    if (config.lod.enabled) {
        gs3d::util::Stopwatch lod_timer;
        session.lod_dataset = load_lod_dataset(session.dataset, config);
        gs3d::util::log::info() << "[TIME] viewer.lod_prepare_seconds = "
                                << lod_timer.elapsed_seconds() << '\n';
        session.lod_point_ids.reserve(session.lod_dataset.level_count());
        for (const auto& level : session.lod_dataset.levels()) {
            session.lod_point_ids.push_back(map_subsequence_point_ids(
                session.dataset.points(),
                session.full_point_ids,
                level.points,
                "LOD level"
            ));
        }
    }

    session.runtime_points_by_id.resize(session.full_point_ids.size() + 1);
    session.runtime_points_valid_by_id.assign(
        session.full_point_ids.size() + 1,
        0
    );
    if (session.dataset.has_point_data()) {
        register_runtime_point_lookup(
            session.dataset.points(),
            session.full_point_ids,
            session.runtime_points_by_id,
            session.runtime_points_valid_by_id
        );
    }
    for (std::size_t i = 0; i < session.lod_dataset.level_count(); ++i) {
        register_runtime_point_lookup(
            session.lod_dataset.level(i).points,
            session.lod_point_ids[i],
            session.runtime_points_by_id,
            session.runtime_points_valid_by_id
        );
    }

    return session;
}

} // namespace gs3d::app
