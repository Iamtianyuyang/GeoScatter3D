#include "data/csv/CsvStreamReader.hpp"
#include "data/format/Manifest.hpp"
#include "data/format/Point.hpp"
#include "foundation/diagnostics/Log.hpp"
#include "foundation/io/Sha256.hpp"
#include "pipeline/compute/StatisticsPass.hpp"
#include "pipeline/stages/PointStage.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <exception>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path csv;
    std::filesystem::path out;
    std::uint32_t chunk_points = 65536;
    std::string value_column;
};

void usage() {
    std::printf("usage: gs3d_pack --csv <file> --out <bundle-dir> [--chunk-points N]\n"
                "                [--value-column NAME]\n"
                "Writes the v3 bundle manifest and point chunks. Stages already recorded as\n"
                "done in the same bundle directory are skipped, so an interrupted run resumes\n"
                "from the last finished stage.\n");
}

bool parse_args(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--csv" && has_value) {
            options.csv = argv[++i];
        } else if (arg == "--out" && has_value) {
            options.out = argv[++i];
        } else if (arg == "--chunk-points" && has_value) {
            options.chunk_points = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--value-column" && has_value) {
            options.value_column = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return false;
        } else {
            gs3d::util::log::error() << "gs3d_pack: unknown argument " << arg;
            usage();
            return false;
        }
    }
    if (options.csv.empty() || options.out.empty()) {
        usage();
        return false;
    }
    return true;
}

std::string make_options_hash(const Options& options) {
    return "v3/points-per-chunk=" + std::to_string(options.chunk_points) +
           "/value-column=" + options.value_column;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_args(argc, argv, options)) {
        return 2;
    }

    try {
        const auto started = std::chrono::steady_clock::now();
        const std::uint64_t source_bytes = std::filesystem::file_size(options.csv);

        gs3d::data::CsvReadConfig config;
        if (!options.value_column.empty()) {
            config.schema.primary_value_field = options.value_column;
        }

        gs3d::pipeline::PointStageOptions stage;
        stage.points_per_chunk = options.chunk_points;
        stage.source_path = options.csv.filename().string();
        stage.source_bytes = source_bytes;
        stage.options_hash = make_options_hash(options);

        // Resume: only continue when the bundle was produced from this exact
        // source with these exact options; otherwise start over.
        bool resumable = gs3d::pipeline::point_stage_complete(options.out);
        if (resumable) {
            const gs3d::data::Manifest existing =
                gs3d::data::load_bundle_manifest(options.out);
            stage.job_id = existing.job.id;
            stage.source_sha256 = existing.job.source_sha256;
            resumable = existing.job.source_bytes == source_bytes &&
                        existing.job.options_hash == stage.options_hash;
            if (resumable && !existing.job.source_sha256.empty()) {
                resumable = gs3d::foundation::sha256_file_hex(options.csv) ==
                            existing.job.source_sha256;
            }
            if (resumable) {
                std::printf("resume: point stage already complete (%llu points, %zu chunks)\n",
                            static_cast<unsigned long long>(existing.point_count),
                            existing.chunks.size());
                return 0;
            }
            gs3d::util::log::warning() << "gs3d_pack: bundle does not match this source; rebuilding";
        }

        const gs3d::preprocess::StatisticsPass statistics(config);
        const gs3d::preprocess::StatisticsResult stats = statistics.run(options.csv);
        if (stats.point_count == 0) {
            gs3d::util::log::error() << "gs3d_pack: no valid points in " << options.csv.string();
            return 1;
        }

        stage.origin = {stats.origin_x, stats.origin_y, stats.origin_z};
        stage.bounds_min = {stats.xmin - stats.origin_x, stats.ymin - stats.origin_y,
                            stats.zmin - stats.origin_z};
        stage.bounds_max = {stats.xmax - stats.origin_x, stats.ymax - stats.origin_y,
                            stats.zmax - stats.origin_z};
        stage.source_sha256 = gs3d::foundation::sha256_file_hex(options.csv);

        gs3d::pipeline::PointChunkWriter writer(options.out, stage);
        std::vector<gs3d::data::Gs3dPoint> batch;
        batch.reserve(8192);
        const double origin_x = stats.origin_x;
        const double origin_y = stats.origin_y;
        const double origin_z = stats.origin_z;

        gs3d::data::CsvStreamReader reader(config);
        const gs3d::data::CsvReadStats read = reader.read(
            options.csv, [&](const gs3d::data::CsvPointRecord& record, std::uint64_t) {
                batch.push_back(gs3d::data::Gs3dPoint{
                    static_cast<float>(record.x - origin_x), static_cast<float>(record.y - origin_y),
                    static_cast<float>(record.z - origin_z), record.primary_value});
                if (batch.size() == batch.capacity()) {
                    writer.write(batch);
                    batch.clear();
                }
            });
        if (!batch.empty()) {
            writer.write(batch);
        }
        const gs3d::pipeline::PointStageResult result = writer.finish();

        std::string error;
        const bool verified = gs3d::pipeline::verify_point_chunks(options.out, &error);
        if (!verified) {
            gs3d::util::log::error() << "gs3d_pack: written bundle failed verification: " << error;
            return 1;
        }

        gs3d::data::Manifest manifest = gs3d::data::load_bundle_manifest(options.out);
        manifest.origin = stage.origin;
        manifest.bounds_min = stage.bounds_min;
        manifest.bounds_max = stage.bounds_max;
        manifest.attributes = {gs3d::data::ManifestAttribute{
            options.value_column.empty() ? "value" : options.value_column, "f32", "", true, 0.0}};
        gs3d::data::save_bundle_manifest(options.out, manifest);

        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::printf("packed %llu points (%llu read, %llu invalid) into %llu chunks in %.2f s\n",
                    static_cast<unsigned long long>(result.points),
                    static_cast<unsigned long long>(read.valid_records),
                    static_cast<unsigned long long>(read.invalid_records),
                    static_cast<unsigned long long>(result.chunks), seconds);
        return 0;
    } catch (const std::exception& error) {
        gs3d::util::log::error() << "gs3d_pack: " << error.what();
        return 1;
    }
}
