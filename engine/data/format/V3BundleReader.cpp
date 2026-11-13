#include "data/format/V3BundleReader.hpp"

#include "data/format/PointChunk.hpp"

#include "foundation/concurrency/ThreadPool.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <fstream>
#include <future>
#include <stdexcept>
#include <thread>

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
    data.points.resize(static_cast<std::size_t>(data.manifest.point_count));

    // 每个点块解码到自己那段，解码顺序不影响结果；50M 顺序解码约 17 s，
    // 是开包耗时里除 LOD 建链之外最大的一块。
    struct ChunkJob {
        const ManifestChunk* chunk = nullptr;
        std::size_t offset = 0;
    };
    std::vector<ChunkJob> jobs;
    std::size_t offset = 0;
    for (const ManifestChunk& chunk : data.manifest.chunks) {
        if (chunk.path.rfind("points/", 0) != 0) {
            continue;
        }
        jobs.push_back(ChunkJob{&chunk, offset});
        offset += static_cast<std::size_t>(chunk.points);
    }

    const auto decode_chunk = [&bundle_dir](const ChunkJob& job, Gs3dPoint* destination) {
        const ManifestChunk& chunk = *job.chunk;
        const std::vector<std::byte> bytes = read_file(resolve_chunk_file(bundle_dir, chunk.path));
        if (bytes.size() != chunk.bytes) {
            throw std::runtime_error("v3 bundle: " + chunk.path + " has " +
                                     std::to_string(bytes.size()) + " bytes, manifest says " +
                                     std::to_string(chunk.bytes));
        }
        if (bytes.size() < GS3D_POINT_CHUNK_HEADER_SIZE) {
            throw std::runtime_error("v3 bundle: " + chunk.path + " is shorter than a header");
        }
        std::vector<Gs3dPoint> decoded = decode_point_chunk(bytes);
        if (point_chunk_header_checksum(bytes) != chunk.crc32c) {
            throw std::runtime_error("v3 bundle: " + chunk.path + " fails its CRC check");
        }
        if (decoded.size() != chunk.points) {
            throw std::runtime_error("v3 bundle: " + chunk.path + " holds " +
                                     std::to_string(decoded.size()) + " points, manifest says " +
                                     std::to_string(chunk.points));
        }
        std::copy(decoded.begin(), decoded.end(), destination);
    };

    const auto available = std::thread::hardware_concurrency();
#ifdef __ANDROID__
    const std::size_t thread_cap = 4;
#else
    const std::size_t thread_cap = 8;
#endif
    const std::size_t workers = std::min<std::size_t>(
        jobs.size(), std::min<std::size_t>(thread_cap, std::max(1u, available))
    );
    if (jobs.size() > 1 && workers > 1) {
        util::ThreadPool pool(workers);
        std::vector<std::future<void>> futures;
        futures.reserve(workers);
        for (std::size_t worker = 0; worker < workers; ++worker) {
            futures.push_back(pool.submit([&jobs, &decode_chunk, &data, worker, workers] {
                for (std::size_t index = worker; index < jobs.size(); index += workers) {
                    decode_chunk(jobs[index], data.points.data() + jobs[index].offset);
                }
            }));
        }
        // 先收集全部结果再抛出，保证没有任务还在写 points。
        std::exception_ptr failure;
        for (auto& future : futures) {
            try {
                future.get();
            } catch (...) {
                if (!failure) {
                    failure = std::current_exception();
                }
            }
        }
        if (failure) {
            std::rethrow_exception(failure);
        }
    } else {
        for (const ChunkJob& job : jobs) {
            decode_chunk(job, data.points.data() + job.offset);
        }
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
