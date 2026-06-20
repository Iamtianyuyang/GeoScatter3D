#include "data/Gs3dReader.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>

namespace gs3d::data {

Gs3dHeader Gs3dReader::read_header(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error(
            "Gs3dReader: failed to open file: " + path.string()
        );
    }

    Gs3dHeader header{};

    in.read(
        reinterpret_cast<char*>(&header),
        static_cast<std::streamsize>(sizeof(Gs3dHeader))
    );

    if (!in.good()) {
        throw std::runtime_error("Gs3dReader: failed to read GS3D header");
    }

    if (!Gs3dFormat::is_valid_header(header)) {
        throw std::runtime_error(
            "Gs3dReader: invalid GS3D header: " +
            Gs3dFormat::describe_header_error(header)
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

    in.read(
        reinterpret_cast<char*>(&result.header),
        static_cast<std::streamsize>(sizeof(Gs3dHeader))
    );

    if (!in.good()) {
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
        static_cast<std::uint64_t>(sizeof(Gs3dPoint));
    if (point_bytes_u64 >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::streamsize>::max()
        )) {
        throw std::runtime_error(
            "Gs3dReader: point data exceeds stream read limit"
        );
    }

    in.seekg(
        static_cast<std::streamoff>(result.header.point_data_offset),
        std::ios::beg
    );

    if (!in.good()) {
        throw std::runtime_error("Gs3dReader: failed to seek point data");
    }

    result.points.resize(static_cast<std::size_t>(result.header.point_count));

    const auto point_bytes =
        static_cast<std::streamsize>(point_bytes_u64);

    if (point_bytes > 0) {
        in.read(
            reinterpret_cast<char*>(result.points.data()),
            point_bytes
        );

        if (!in.good()) {
            throw std::runtime_error("Gs3dReader: failed to read GS3D point data");
        }
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
