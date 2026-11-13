#include "pipeline/stages/PointStage.hpp"

#include "data/format/PointChunk.hpp"
#include "foundation/io/AtomicFile.hpp"
#include "foundation/io/Crc32c.hpp"

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

std::uint32_t payload_crc(std::span<const std::byte> chunk_bytes) {
    if (chunk_bytes.size() < gs3d::data::GS3D_POINT_CHUNK_HEADER_SIZE) {
        throw std::runtime_error("point chunk: truncated header");
    }
    return gs3d::util::crc32c(chunk_bytes.subspan(gs3d::data::GS3D_POINT_CHUNK_HEADER_SIZE));
}

} // namespace

PointStageResult write_point_stage(const fs::path& bundle_dir,
                                   std::span<const gs3d::data::Gs3dPoint> points,
                                   const PointStageOptions& options) {
    if (options.points_per_chunk == 0 ||
        options.points_per_chunk > gs3d::data::GS3D_POINT_CHUNK_MAX_POINTS) {
        throw std::runtime_error("point stage: points_per_chunk must be 1..65536");
    }

    const fs::path points_dir = bundle_dir / "points";
    std::error_code ec;
    fs::create_directories(points_dir, ec);
    if (ec) {
        throw std::runtime_error("point stage: cannot create " + points_dir.string() + ": " +
                                 ec.message());
    }
    // A restarted stage rewrites every chunk; leftovers from an interrupted run
    // must not survive because the manifest would not list them.
    for (const fs::directory_entry& entry : fs::directory_iterator(points_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".chunk") {
            fs::remove(entry.path(), ec);
        }
    }

    gs3d::data::Manifest manifest;
    if (fs::exists(bundle_dir / "manifest.json")) {
        manifest = gs3d::data::load_bundle_manifest(bundle_dir);
    }

    PointStageResult result;
    const std::size_t total = points.size();
    const std::size_t per_chunk = options.points_per_chunk;
    std::vector<gs3d::data::ManifestChunk> written;
    for (std::size_t offset = 0; offset < total; offset += per_chunk) {
        const std::size_t count = std::min(per_chunk, total - offset);
        const auto encoded = gs3d::data::encode_point_chunk(points.subspan(offset, count));
        gs3d::foundation::atomic_write_file(bundle_dir / chunk_path(written.size()),
                                            std::span<const std::byte>(encoded));

        written.push_back(gs3d::data::ManifestChunk{
            chunk_path(written.size()), static_cast<std::uint64_t>(count),
            static_cast<std::uint64_t>(encoded.size()), payload_crc(encoded)});
        result.points += count;
    }
    result.chunks = written.size();

    manifest.chunks.erase(
        std::remove_if(manifest.chunks.begin(), manifest.chunks.end(),
                       [](const gs3d::data::ManifestChunk& chunk) {
                           return chunk.path.rfind("points/", 0) == 0;
                       }),
        manifest.chunks.end());
    manifest.chunks.insert(manifest.chunks.end(), written.begin(), written.end());
    manifest.point_count = result.points;
    manifest.stride = static_cast<std::uint32_t>(sizeof(gs3d::data::Gs3dPoint));
    manifest.job.id = options.job_id;
    manifest.job.source_path = options.source_path;
    manifest.job.source_bytes = options.source_bytes;
    manifest.job.source_sha256 = options.source_sha256;
    manifest.job.options_hash = options.options_hash;
    manifest.job.stages[kPointStageName] = "done";
    gs3d::data::save_bundle_manifest(bundle_dir, manifest);
    return result;
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
    for (const gs3d::data::ManifestChunk& chunk : manifest.chunks) {
        const fs::path file = bundle_dir / chunk.path;
        if (!fs::exists(file, ec) || fs::file_size(file, ec) != chunk.bytes) {
            return false;
        }
    }
    return true;
}

bool verify_point_chunks(const fs::path& bundle_dir, std::string* error) {
    const gs3d::data::Manifest manifest = gs3d::data::load_bundle_manifest(bundle_dir);
    for (const gs3d::data::ManifestChunk& chunk : manifest.chunks) {
        if (chunk.path.rfind("points/", 0) != 0) {
            continue;
        }
        try {
            const std::vector<std::byte> bytes = read_file(bundle_dir / chunk.path);
            if (bytes.size() != chunk.bytes || payload_crc(bytes) != chunk.crc32c) {
                if (error != nullptr) {
                    *error = "chunk " + chunk.path + " does not match the manifest";
                }
                return false;
            }
        } catch (const std::exception& failure) {
            if (error != nullptr) {
                *error = failure.what();
            }
            return false;
        }
    }
    return true;
}

std::vector<gs3d::data::Gs3dPoint> read_point_chunk_file(const fs::path& file) {
    const std::vector<std::byte> bytes = read_file(file);
    return gs3d::data::decode_point_chunk(bytes);
}

} // namespace gs3d::pipeline
