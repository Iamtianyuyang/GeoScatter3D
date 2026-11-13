#include "pipeline/stages/BundleBuild.hpp"

#include "data/csv/CsvStreamReader.hpp"
#include "data/format/Manifest.hpp"
#include "foundation/io/Sha256.hpp"
#include "pipeline/compute/StatisticsPass.hpp"
#include "pipeline/stages/PointStage.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace gs3d::pipeline {
namespace {

namespace fs = std::filesystem;

std::string options_hash(const BundleBuildOptions& options) {
    return "v3/points-per-chunk=" + std::to_string(options.points_per_chunk) +
           "/value-field=" + options.value_field;
}

bool stage_done(const gs3d::data::Manifest& manifest, const char* stage) {
    const auto found = manifest.job.stages.find(stage);
    return found != manifest.job.stages.end() && found->second == "done";
}

// Resume is only safe when the bundle came from this exact source and options.
bool fingerprints_match(const gs3d::data::Manifest& manifest,
                        std::uint64_t source_bytes,
                        const std::string& sha256,
                        const std::string& hash) {
    return manifest.job.source_bytes == source_bytes && manifest.job.source_sha256 == sha256 &&
           manifest.job.options_hash == hash;
}

} // namespace

BundleBuildResult build_bundle(const BundleBuildOptions& options,
                               const std::function<void(const char*)>& on_stage) {
    if (on_stage) {
        on_stage("fingerprint");
    }
    if (options.source.empty() || options.bundle_dir.empty()) {
        throw std::runtime_error("build_bundle: source and bundle paths are required");
    }
    if (options.points_per_chunk == 0 || options.points_per_chunk > 65536) {
        throw std::runtime_error("build_bundle: points_per_chunk must be 1..65536");
    }
    const fs::path points_dir = options.bundle_dir / "points";
    const fs::path bundle_path = fs::weakly_canonical(fs::absolute(options.bundle_dir));
    fs::path source_parent = fs::weakly_canonical(fs::absolute(options.source).parent_path());
    if (fs::exists(bundle_path)) {
        while (!source_parent.empty()) {
            if (fs::equivalent(bundle_path, source_parent)) {
                throw std::runtime_error("build_bundle: source must be outside the bundle directory");
            }
            const fs::path parent = source_parent.parent_path();
            if (parent == source_parent) {
                break;
            }
            source_parent = parent;
        }
    }
    std::error_code ec;
    const std::uint64_t source_bytes = fs::file_size(options.source, ec);
    if (ec) {
        throw std::runtime_error("build_bundle: cannot size " + options.source.string() + ": " +
                                 ec.message());
    }
    const std::string hash = options_hash(options);
    const std::string sha256 = gs3d::foundation::sha256_file_hex(options.source);

    gs3d::data::Manifest manifest;
    bool have_manifest = fs::exists(options.bundle_dir / "manifest.json", ec);
    if (ec) {
        throw std::runtime_error("build_bundle: cannot inspect existing bundle: " + ec.message());
    }
    if (!have_manifest && fs::exists(points_dir)) {
        for (const auto& entry : fs::directory_iterator(points_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".chunk") {
                throw std::runtime_error("build_bundle: existing point chunks have no manifest");
            }
        }
    }
    if (have_manifest) {
        manifest = gs3d::data::load_bundle_manifest(options.bundle_dir);
        if (!fingerprints_match(manifest, source_bytes, sha256, hash)) {
            throw std::runtime_error(
                "build_bundle: source or options do not match the existing bundle");
        }
    }

    BundleBuildResult result;
    if (have_manifest && stage_done(manifest, kPointStageName)) {
        std::string error;
        if (!verify_point_chunks(options.bundle_dir, &error)) {
            throw std::runtime_error("build_bundle: existing bundle failed verification: " + error);
        }
        result.points = manifest.point_count;
        result.chunks = manifest.chunks.size();
        result.stages_skipped = {kStatsStageName, kPointStageName};
        return result;
    }

    manifest.job.id = manifest.job.id.empty() ? "job-" + sha256.substr(0, 12) : manifest.job.id;
    const auto source_name = options.source.filename().u8string();
    manifest.job.source_path.assign(source_name.begin(), source_name.end());
    manifest.job.source_bytes = source_bytes;
    manifest.job.source_sha256 = sha256;
    manifest.job.options_hash = hash;
    const auto source_path = fs::absolute(options.source).u8string();
    manifest.job.resume_source_path.assign(source_path.begin(), source_path.end());
    manifest.job.resume_points_per_chunk = options.points_per_chunk;
    manifest.job.resume_value_field = options.value_field;
    gs3d::data::save_bundle_manifest(options.bundle_dir, manifest);

    if (have_manifest && stage_done(manifest, kStatsStageName)) {
        result.stages_skipped.push_back(kStatsStageName);
    } else {
        if (on_stage) {
            on_stage(kStatsStageName);
        }
        gs3d::data::CsvReadConfig config;
        if (!options.value_field.empty()) {
            config.schema.primary_value_field = options.value_field;
        }
        const gs3d::preprocess::StatisticsPass statistics(config);
        const gs3d::preprocess::StatisticsResult stats = statistics.run(options.source);
        if (stats.point_count == 0) {
            throw std::runtime_error("build_bundle: no valid points in " + options.source.string());
        }
        manifest.origin = {stats.origin_x, stats.origin_y, stats.origin_z};
        manifest.bounds_min = {stats.xmin - stats.origin_x, stats.ymin - stats.origin_y,
                               stats.zmin - stats.origin_z};
        manifest.bounds_max = {stats.xmax - stats.origin_x, stats.ymax - stats.origin_y,
                               stats.zmax - stats.origin_z};
        manifest.point_count = stats.point_count;
        manifest.job.stages[kStatsStageName] = "done";
        gs3d::data::save_bundle_manifest(options.bundle_dir, manifest);
        result.stages_run.push_back(kStatsStageName);
    }

    if (on_stage) {
        on_stage(kPointStageName);
    }
    manifest.attributes = {gs3d::data::ManifestAttribute{
        options.value_field.empty() ? "value" : options.value_field, "f32", "", true, 0.0}};
    gs3d::data::save_bundle_manifest(options.bundle_dir, manifest);

    PointStageOptions stage;
    stage.points_per_chunk = options.points_per_chunk;
    stage.origin = manifest.origin;
    stage.bounds_min = manifest.bounds_min;
    stage.bounds_max = manifest.bounds_max;
    stage.job_id = manifest.job.id;
    stage.source_path = manifest.job.source_path;
    stage.source_bytes = manifest.job.source_bytes;
    stage.source_sha256 = manifest.job.source_sha256;
    stage.options_hash = manifest.job.options_hash;

    gs3d::data::CsvReadConfig config;
    if (!options.value_field.empty()) {
        config.schema.primary_value_field = options.value_field;
    }

    PointChunkWriter writer(options.bundle_dir, stage);
    std::vector<gs3d::data::Gs3dPoint> batch;
    batch.reserve(8192);
    const double origin_x = manifest.origin[0];
    const double origin_y = manifest.origin[1];
    const double origin_z = manifest.origin[2];
    gs3d::data::CsvStreamReader reader(config);
    reader.read(options.source, [&](const gs3d::data::CsvPointRecord& record, std::uint64_t) {
        batch.push_back(gs3d::data::Gs3dPoint{static_cast<float>(record.x - origin_x),
                                              static_cast<float>(record.y - origin_y),
                                              static_cast<float>(record.z - origin_z),
                                              record.primary_value});
        if (batch.size() == batch.capacity()) {
            writer.write(batch);
            batch.clear();
        }
    });
    if (!batch.empty()) {
        writer.write(batch);
    }
    const PointStageResult stage_result = writer.finish();
    result.stages_run.push_back(kPointStageName);
    result.points = stage_result.points;
    result.chunks = stage_result.chunks;

    std::string error;
    if (!verify_point_chunks(options.bundle_dir, &error)) {
        throw std::runtime_error("build_bundle: written bundle failed verification: " + error);
    }
    return result;
}

BundleBuildResult resume_bundle(const fs::path& bundle_dir,
                                const std::function<void(const char*)>& on_stage) {
    const auto manifest = gs3d::data::load_bundle_manifest(bundle_dir);
    const auto& job = manifest.job;
    const fs::path source(
        std::u8string(job.resume_source_path.begin(), job.resume_source_path.end()));
    if (job.id.empty() || job.source_sha256.empty() || job.options_hash.empty() ||
        job.resume_source_path.empty() || !source.is_absolute() ||
        job.resume_points_per_chunk == 0) {
        throw std::runtime_error("resume_bundle: checkpoint lacks resume information");
    }
    BundleBuildOptions options;
    options.source = source;
    options.bundle_dir = bundle_dir;
    options.points_per_chunk = job.resume_points_per_chunk;
    options.value_field = job.resume_value_field;
    return build_bundle(options, on_stage);
}

} // namespace gs3d::pipeline
