#include "data/Gs3dDataset.hpp"

#include "data/Gs3dReader.hpp"

#include <sstream>
#include <stdexcept>

namespace gs3d::data {

Gs3dDataset::Gs3dDataset(
    Gs3dHeader header,
    std::vector<Gs3dPoint> points,
    std::filesystem::path source_path
)
    : header_(header)
    , points_(std::move(points))
    , source_path_(std::move(source_path))
{
    if (!Gs3dFormat::is_valid_header(header_)) {
        throw std::runtime_error(
            "Gs3dDataset: invalid header: " +
            Gs3dFormat::describe_header_error(header_)
        );
    }

    if (points_.size() != header_.point_count) {
        throw std::runtime_error(
            "Gs3dDataset: point vector size does not match header point_count"
        );
    }
}

const Gs3dHeader& Gs3dDataset::header() const noexcept {
    return header_;
}

const std::vector<Gs3dPoint>& Gs3dDataset::points() const noexcept {
    return points_;
}

std::vector<Gs3dPoint>& Gs3dDataset::points() noexcept {
    return points_;
}

const Gs3dPoint* Gs3dDataset::point_data() const noexcept {
    return points_.empty() ? nullptr : points_.data();
}

std::uint64_t Gs3dDataset::point_count() const noexcept {
    return header_.point_count;
}

std::uint64_t Gs3dDataset::point_bytes() const noexcept {
    return static_cast<std::uint64_t>(points_.size()) *
           static_cast<std::uint64_t>(sizeof(Gs3dPoint));
}

bool Gs3dDataset::empty() const noexcept {
    return points_.empty();
}

const std::filesystem::path& Gs3dDataset::source_path() const noexcept {
    return source_path_;
}

double Gs3dDataset::origin_x() const noexcept {
    return header_.origin_x;
}

double Gs3dDataset::origin_y() const noexcept {
    return header_.origin_y;
}

double Gs3dDataset::origin_z() const noexcept {
    return header_.origin_z;
}

float Gs3dDataset::bbox_min_x() const noexcept {
    return header_.bbox_min_x;
}

float Gs3dDataset::bbox_min_y() const noexcept {
    return header_.bbox_min_y;
}

float Gs3dDataset::bbox_min_z() const noexcept {
    return header_.bbox_min_z;
}

float Gs3dDataset::bbox_max_x() const noexcept {
    return header_.bbox_max_x;
}

float Gs3dDataset::bbox_max_y() const noexcept {
    return header_.bbox_max_y;
}

float Gs3dDataset::bbox_max_z() const noexcept {
    return header_.bbox_max_z;
}

float Gs3dDataset::value_min() const noexcept {
    return header_.value_min;
}

float Gs3dDataset::value_max() const noexcept {
    return header_.value_max;
}

std::string Gs3dDataset::summary() const {
    std::ostringstream oss;

    oss << "Gs3dDataset\n";
    oss << "  source_path: " << source_path_.string() << '\n';
    oss << "  point_count: " << point_count() << '\n';
    oss << "  point_bytes: " << point_bytes() << '\n';

    oss << "  origin: ["
        << origin_x() << ", "
        << origin_y() << ", "
        << origin_z() << "]\n";

    oss << "  bbox_min: ["
        << bbox_min_x() << ", "
        << bbox_min_y() << ", "
        << bbox_min_z() << "]\n";

    oss << "  bbox_max: ["
        << bbox_max_x() << ", "
        << bbox_max_y() << ", "
        << bbox_max_z() << "]\n";

    oss << "  value_range: ["
        << value_min() << ", "
        << value_max() << "]\n";

    return oss.str();
}

bool Gs3dDataset::is_consistent() const noexcept {
    if (!Gs3dFormat::is_valid_header(header_)) {
        return false;
    }

    if (points_.size() != header_.point_count) {
        return false;
    }

    return true;
}

Gs3dDataset Gs3dDatasetLoader::load(const std::filesystem::path& path) {
    const auto read_result = Gs3dReader::read_all(path);

    return Gs3dDataset(
        read_result.header,
        read_result.points,
        path
    );
}

Gs3dDataset Gs3dDatasetLoader::load_header_only(
    const std::filesystem::path& path
) {
    const auto header = Gs3dReader::read_header(path);

    std::vector<Gs3dPoint> empty_points;

    if (header.point_count != 0) {
        throw std::runtime_error(
            "Gs3dDatasetLoader: header-only dataset cannot be constructed "
            "with nonzero point_count in current Gs3dDataset design"
        );
    }

    return Gs3dDataset(
        header,
        std::move(empty_points),
        path
    );
}

} // namespace gs3d::data