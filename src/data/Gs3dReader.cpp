#include "data/Gs3dReader.hpp"
#include "platform/MemoryMappedFile.hpp"

#include <bit>
#include <cstring>
#include <fstream>
#include <future>
#include <limits>
#include <stdexcept>
#include <thread>

namespace gs3d::data {

Gs3dHeader Gs3dReader::read_header(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error(
            "Gs3dReader: failed to open file: " + path.string()
        );
    }

    Gs3dHeader header{};
    if (!Gs3dFormat::read_header(in, header)) {
        throw std::runtime_error("Gs3dReader: failed to read GS3D header");
    }

    if (!Gs3dFormat::is_valid_header(header)) {
        throw std::runtime_error(
            "Gs3dReader: invalid GS3D header: " +
            Gs3dFormat::describe_header_error(header)
        );
    }

    if (std::filesystem::file_size(path) < header.point_data_offset) {
        throw std::runtime_error(
            "Gs3dReader: file is shorter than the GS3D header"
        );
    }

    return header;
}

Gs3dReadResult Gs3dReader::read_all(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error(
            "Gs3dReader: failed to open file: " + path.string()
        );
    }

    Gs3dReadResult result;

    if (!Gs3dFormat::read_header(in, result.header)) {
        throw std::runtime_error("Gs3dReader: failed to read GS3D header");
    }

    if (!Gs3dFormat::is_valid_header(result.header)) {
        throw std::runtime_error(
            "Gs3dReader: invalid GS3D header: " +
            Gs3dFormat::describe_header_error(result.header)
        );
    }

    const auto actual_size = std::filesystem::file_size(path);
    const auto expected_size =
        Gs3dFormat::expected_file_size(result.header);
    if (actual_size != expected_size) {
        throw std::runtime_error(
            "Gs3dReader: file size does not match GS3D header"
        );
    }

    if (result.header.point_count >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()
        )) {
        throw std::runtime_error(
            "Gs3dReader: point count exceeds addressable memory"
        );
    }

    const auto point_bytes_u64 =
        result.header.point_count *
        static_cast<std::uint64_t>(GS3D_POINT_SIZE);
    if (point_bytes_u64 >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::streamsize>::max()
        )) {
        throw std::runtime_error(
            "Gs3dReader: point data exceeds stream read limit"
        );
    }

    result.points.resize(static_cast<std::size_t>(result.header.point_count));
    const auto count = result.points.size();

    auto mmap = platform::MemoryMappedFile::open_read(path);
    if (mmap && mmap->is_open() &&
        result.header.point_data_offset + count * sizeof(Gs3dPoint) <= mmap->size()) {
        if constexpr (std::endian::native == std::endian::little) {
            const auto* src = reinterpret_cast<const Gs3dPoint*>(
                mmap->data() + result.header.point_data_offset
            );
            const auto hw = std::thread::hardware_concurrency();
            if (count >= 500'000 && hw > 1) {
                const unsigned int num_threads = std::min<unsigned int>(hw, 32);
                const std::size_t chunk = (count + num_threads - 1) / num_threads;
                std::vector<std::future<void>> futures;
                futures.reserve(num_threads);
                for (unsigned int t = 0; t < num_threads; ++t) {
                    const std::size_t begin = t * chunk;
                    const std::size_t end = std::min(count, begin + chunk);
                    if (begin < end) {
                        futures.push_back(std::async(std::launch::async, [&result, src, begin, end]() {
                            std::memcpy(
                                result.points.data() + begin,
                                src + begin,
                                (end - begin) * sizeof(Gs3dPoint)
                            );
                        }));
                    }
                }
                for (auto& f : futures) {
                    f.get();
                }
            } else {
                std::memcpy(result.points.data(), src, count * sizeof(Gs3dPoint));
            }
            return result;
        }
    }

    in.seekg(
        static_cast<std::streamoff>(result.header.point_data_offset),
        std::ios::beg
    );

    if (!in.good()) {
        throw std::runtime_error("Gs3dReader: failed to seek point data");
    }

    if (!Gs3dFormat::read_points(in, result.header, result.points)) {
        throw std::runtime_error("Gs3dReader: failed to read GS3D point data");
    }

    return result;
}

bool Gs3dReader::validate_file_size(const std::filesystem::path& path) {
    const auto header = read_header(path);

    const auto actual_size = std::filesystem::file_size(path);
    const auto expected_size = Gs3dFormat::expected_file_size(header);

    return actual_size == expected_size;
}

} // namespace gs3d::data
