#include "data/format/V3BundleReader.hpp"

#include "data/format/PointChunk.hpp"
#include "foundation/io/Crc32c.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <fstream>
#include <stdexcept>

namespace gs3d::data {
namespace {

namespace fs = std::filesystem;

std::vector<std::byte> read_file(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("v3 bundle: cannot open " + file.string());
    }
    const std::streamoff size = stream.tellg();
    if (size < 0) {
        throw std::runtime_error("v3 bundle: cannot size " + file.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        throw std::runtime_error("v3 bundle: short read on " + file.string());
    }
    return bytes;
}

} // namespace

bool is_v3_bundle(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_directory(path, ec) || !fs::exists(path / "manifest.json", ec)) {
        return false;
    }
    try {
        const Manifest manifest = load_bundle_manifest(path);
        return manifest.format_version == Manifest::kFormatVersion;
    } catch (const std::exception&) {
        return false;
    }
}

V3BundleData read_v3_bundle(const fs::path& bundle_dir) {
    V3BundleData data;
    data.manifest = load_bundle_manifest(bundle_dir);
    const auto stage = data.manifest.job.stages.find("points");
    if (!data.manifest.job.resume_source_path.empty() &&
        (stage == data.manifest.job.stages.end() || stage->second != "done")) {
        throw std::runtime_error("v3 bundle: point stage is incomplete");
    }

    std::uint64_t declared = 0;
    for (const ManifestChunk& chunk : data.manifest.chunks) {
        if (chunk.path.rfind("points/", 0) == 0) {
            declared += chunk.points;
        }
    }
    if (declared != data.manifest.point_count) {
        throw std::runtime_error("v3 bundle: chunk table declares " + std::to_string(declared) +
                                 " points but the manifest says " +
                                 std::to_string(data.manifest.point_count));
    }
    data.points.reserve(static_cast<std::size_t>(data.manifest.point_count));

    for (const ManifestChunk& chunk : data.manifest.chunks) {
        if (chunk.path.rfind("points/", 0) != 0) {
            continue;
        }
        const std::vector<std::byte> bytes = read_file(bundle_dir / chunk.path);
        if (bytes.size() != chunk.bytes) {
            throw std::runtime_error("v3 bundle: " + chunk.path + " has " +
                                     std::to_string(bytes.size()) + " bytes, manifest says " +
                                     std::to_string(chunk.bytes));
        }
        if (bytes.size() < GS3D_POINT_CHUNK_HEADER_SIZE) {
            throw std::runtime_error("v3 bundle: " + chunk.path + " is shorter than a header");
        }
        const std::uint32_t crc =
            gs3d::util::crc32c(std::span<const std::byte>(bytes).subspan(GS3D_POINT_CHUNK_HEADER_SIZE));
        if (crc != chunk.crc32c) {
            throw std::runtime_error("v3 bundle: " + chunk.path + " fails its CRC check");
        }
        std::vector<Gs3dPoint> decoded = decode_point_chunk(bytes);
        if (decoded.size() != chunk.points) {
            throw std::runtime_error("v3 bundle: " + chunk.path + " holds " +
                                     std::to_string(decoded.size()) + " points, manifest says " +
                                     std::to_string(chunk.points));
        }
        data.points.insert(data.points.end(), decoded.begin(), decoded.end());
    }

    if (data.points.size() != data.manifest.point_count) {
        throw std::runtime_error("v3 bundle: read " + std::to_string(data.points.size()) +
                                 " points, manifest says " +
                                 std::to_string(data.manifest.point_count));
    }
    return data;
}

Gs3dHeader v3_bundle_header(const V3BundleData& data) {
    const Manifest& manifest = data.manifest;
    Gs3dHeader header{};
    std::memcpy(header.magic, GS3D_MAGIC, sizeof(GS3D_MAGIC));
    header.version = GS3D_VERSION;
    header.header_size = GS3D_HEADER_V2_SIZE;
    header.point_count = manifest.point_count;
    header.point_data_offset = GS3D_HEADER_V2_SIZE;
    header.origin_x = manifest.origin[0];
    header.origin_y = manifest.origin[1];
    header.origin_z = manifest.origin[2];
    header.bbox_min_x = static_cast<float>(manifest.bounds_min[0]);
    header.bbox_min_y = static_cast<float>(manifest.bounds_min[1]);
    header.bbox_min_z = static_cast<float>(manifest.bounds_min[2]);
    header.bbox_max_x = static_cast<float>(manifest.bounds_max[0]);
    header.bbox_max_y = static_cast<float>(manifest.bounds_max[1]);
    header.bbox_max_z = static_cast<float>(manifest.bounds_max[2]);
    header.value_min = 0.0f;
    header.value_max = 0.0f;
    if (!data.points.empty()) {
        float low = data.points.front().value;
        float high = low;
        for (const Gs3dPoint& point : data.points) {
            low = std::min(low, point.value);
            high = std::max(high, point.value);
        }
        header.value_min = low;
        header.value_max = high;
    }
    return header;
}

} // namespace gs3d::data
