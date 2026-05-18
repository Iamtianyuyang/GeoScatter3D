#include "data/Gs3dReader.hpp"

#include <fstream>
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

    in.seekg(
        static_cast<std::streamoff>(result.header.point_data_offset),
        std::ios::beg
    );

    if (!in.good()) {
        throw std::runtime_error("Gs3dReader: failed to seek point data");
    }

    result.points.resize(static_cast<std::size_t>(result.header.point_count));

    const auto point_bytes =
        static_cast<std::streamsize>(
            result.points.size() * sizeof(Gs3dPoint)
        );

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