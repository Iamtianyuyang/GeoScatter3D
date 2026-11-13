#include "pipeline/stages/PointStage.hpp"

#include "foundation/io/AtomicFile.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace gs3d::pipeline {
namespace {

namespace fs = std::filesystem;

std::string chunk_path(std::size_t index) {
    char name[32];
    std::snprintf(name, sizeof(name), "points/%06zu.chunk", index);
    return name;
}

std::vector<std::byte> read_file(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("point chunk: cannot open " + file.string());
    }
    const std::streamoff size = stream.tellg();
    if (size < 0) {
        throw std::runtime_error("point chunk: cannot size " + file.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        throw std::runtime_error("point chunk: short read on " + file.string());
    }
    return bytes;
}

} // namespace

PointChunkWriter::PointChunkWriter(fs::path bundle_dir, PointStageOptions options)
    : bundle_dir_(std::move(bundle_dir)), options_(std::move(options)) {
    if (options_.points_per_chunk == 0 ||
        options_.points_per_chunk > gs3d::data::GS3D_POINT_CHUNK_MAX_POINTS) {
        throw std::runtime_error("point stage: points_per_chunk must be 1..65536");
    }

    const fs::path points_dir = bundle_dir_ / "points";
    std::error_code ec;
    fs::create_directories(points_dir, ec);
    if (ec) {
        throw std::runtime_error("point stage: cannot create " + points_dir.string() + ": " +
                                 ec.message());
    }
    // The cleanup below deletes files, so the directory must not be a link out
    // of the bundle; resolve_chunk_file throws when it is.
    gs3d::data::resolve_chunk_file(bundle_dir_, "points");
    // A restarted stage rewrites every chunk; the manifest would not list
    // leftovers from an interrupted run, so they must not survive.
    for (const fs::directory_entry& entry : fs::directory_iterator(points_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".chunk") {
            fs::remove(entry.path(), ec);
        }
    }
    pending_.reserve(options_.points_per_chunk);
}

PointChunkWriter::~PointChunkWriter() = default;

void PointChunkWriter::write(std::span<const gs3d::data::Gs3dPoint> points) {
    if (finished_) {
        throw std::runtime_error("point stage: write after finish");
    }
    std::size_t offset = 0;
    while (offset < points.size()) {
        const std::size_t take =
            std::min(points.size() - offset, options_.points_per_chunk - pending_.size());
        pending_.insert(pending_.end(), points.begin() + static_cast<std::ptrdiff_t>(offset),
                        points.begin() + static_cast<std::ptrdiff_t>(offset + take));
        offset += take;
        flush_full_chunks();
    }
}

void PointChunkWriter::flush_full_chunks() {
    while (pending_.size() >= options_.points_per_chunk) {
        const auto encoded = gs3d::data::encode_point_chunk(pending_);
        const std::string path = chunk_path(chunks_.size());
        gs3d::foundation::atomic_write_file(bundle_dir_ / path, std::span<const std::byte>(encoded));
        chunks_.push_back(gs3d::data::ManifestChunk{path,
                                                    static_cast<std::uint64_t>(pending_.size()),
                                                    static_cast<std::uint64_t>(encoded.size()),
                                                    gs3d::data::point_chunk_header_checksum(encoded)});
        written_points_ += pending_.size();
        pending_.clear();
    }
}

PointStageResult PointChunkWriter::finish() {
    if (finished_) {
        throw std::runtime_error("point stage: finish called twice");
    }
    if (!pending_.empty()) {
        const auto encoded = gs3d::data::encode_point_chunk(pending_);
        const std::string path = chunk_path(chunks_.size());
        gs3d::foundation::atomic_write_file(bundle_dir_ / path, std::span<const std::byte>(encoded));
        chunks_.push_back(gs3d::data::ManifestChunk{path,
                                                    static_cast<std::uint64_t>(pending_.size()),
                                                    static_cast<std::uint64_t>(encoded.size()),
                                                    gs3d::data::point_chunk_header_checksum(encoded)});
        written_points_ += pending_.size();
        pending_.clear();
    }

    gs3d::data::Manifest manifest;
    if (fs::exists(bundle_dir_ / "manifest.json")) {
        manifest = gs3d::data::load_bundle_manifest(bundle_dir_);
    }
    manifest.chunks.erase(
        std::remove_if(manifest.chunks.begin(), manifest.chunks.end(),
                       [](const gs3d::data::ManifestChunk& chunk) {
                           return chunk.path.rfind("points/", 0) == 0;
                       }),
        manifest.chunks.end());
    manifest.chunks.insert(manifest.chunks.end(), chunks_.begin(), chunks_.end());
    manifest.point_count = written_points_;
    manifest.stride = static_cast<std::uint32_t>(sizeof(gs3d::data::Gs3dPoint));
    manifest.origin = options_.origin;
    manifest.bounds_min = options_.bounds_min;
    manifest.bounds_max = options_.bounds_max;
    manifest.job.id = options_.job_id;
    manifest.job.source_path = options_.source_path;
    manifest.job.source_bytes = options_.source_bytes;
    manifest.job.source_sha256 = options_.source_sha256;
    manifest.job.options_hash = options_.options_hash;
    manifest.job.stages[kPointStageName] = "done";
    gs3d::data::save_bundle_manifest(bundle_dir_, manifest);
    finished_ = true;

    return PointStageResult{static_cast<std::uint64_t>(chunks_.size()), written_points_};
}

PointStageResult write_point_stage(const fs::path& bundle_dir,
                                   std::span<const gs3d::data::Gs3dPoint> points,
                                   const PointStageOptions& options) {
    PointChunkWriter writer(bundle_dir, options);
    writer.write(points);
    return writer.finish();
}

bool point_stage_complete(const fs::path& bundle_dir) {
    std::error_code ec;
    if (!fs::exists(bundle_dir / "manifest.json", ec)) {
        return false;
    }
    gs3d::data::Manifest manifest;
    try {
        manifest = gs3d::data::load_bundle_manifest(bundle_dir);
    } catch (const std::exception&) {
        return false;
    }
    const auto stage = manifest.job.stages.find(kPointStageName);
    if (stage == manifest.job.stages.end() || stage->second != "done") {
        return false;
    }
    try {
        for (const gs3d::data::ManifestChunk& chunk : manifest.chunks) {
            const fs::path file = gs3d::data::resolve_chunk_file(bundle_dir, chunk.path);
            if (!fs::exists(file, ec) || fs::file_size(file, ec) != chunk.bytes) {
                return false;
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool verify_point_chunks(const fs::path& bundle_dir, std::string* error) {
    const gs3d::data::Manifest manifest = gs3d::data::load_bundle_manifest(bundle_dir);
    std::uint64_t points = 0;
    for (const gs3d::data::ManifestChunk& chunk : manifest.chunks) {
        if (chunk.path.rfind("points/", 0) != 0) {
            continue;
        }
        try {
            const std::vector<std::byte> bytes =
                read_file(gs3d::data::resolve_chunk_file(bundle_dir, chunk.path));
            const auto decoded = gs3d::data::decode_point_chunk(bytes);
            if (bytes.size() != chunk.bytes ||
                gs3d::data::point_chunk_header_checksum(bytes) != chunk.crc32c ||
                decoded.size() != chunk.points || chunk.points > manifest.point_count - points) {
                if (error != nullptr) {
                    *error = "chunk " + chunk.path + " does not match the manifest";
                }
                return false;
            }
            points += chunk.points;
        } catch (const std::exception& failure) {
            if (error != nullptr) {
                *error = failure.what();
            }
            return false;
        }
    }
    if (points != manifest.point_count || points == 0) {
        if (error != nullptr) {
            *error = "point count does not match the manifest";
        }
        return false;
    }
    return true;
}

std::vector<gs3d::data::Gs3dPoint> read_point_chunk_file(const fs::path& file) {
    const std::vector<std::byte> bytes = read_file(file);
    return gs3d::data::decode_point_chunk(bytes);
}

} // namespace gs3d::pipeline
