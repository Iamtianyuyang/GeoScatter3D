#include "data/Gs3dTileReader.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace gs3d::data {

namespace {

template <typename T>
T read_binary(
    std::ifstream& file,
    const char* error_message
) {
    T value{};

    file.read(
        reinterpret_cast<char*>(&value),
        static_cast<std::streamsize>(sizeof(T))
    );

    if (!file.good()) {
        throw std::runtime_error(error_message);
    }

    return value;
}

std::vector<Gs3dTileRecord> read_tile_records(
    std::ifstream& file,
    std::uint64_t tile_count
) {
    if (tile_count == 0) {
        throw std::runtime_error(
            "Gs3dTileReader: tile_count is zero"
        );
    }

    if (tile_count >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()
        )) {
        throw std::runtime_error(
            "Gs3dTileReader: tile_count exceeds size_t range"
        );
    }

    std::vector<Gs3dTileRecord> records;
    records.reserve(
        static_cast<std::size_t>(tile_count)
    );

    for (std::uint64_t i = 0; i < tile_count; ++i) {
        auto record =
            read_binary<Gs3dTileRecord>(
                file,
                "Gs3dTileReader: failed to read tile record"
            );

        Gs3dTileFormat::validate_tile_record(
            record
        );

        records.push_back(record);
    }

    return records;
}

std::uint64_t file_size_or_zero(
    const std::filesystem::path& path
) {
    std::error_code ec;

    const auto size =
        std::filesystem::file_size(path, ec);

    if (ec) {
        return 0;
    }

    return static_cast<std::uint64_t>(size);
}

void validate_paths(
    const std::filesystem::path& index_path,
    const std::filesystem::path& data_path
) {
    if (index_path.empty()) {
        throw std::runtime_error(
            "Gs3dTileReader: index_path is empty"
        );
    }

    if (data_path.empty()) {
        throw std::runtime_error(
            "Gs3dTileReader: data_path is empty"
        );
    }
}

void validate_records_against_headers(
    const Gs3dTileIndexFileHeader& index_header,
    const Gs3dTileDataFileHeader& data_header,
    const std::vector<Gs3dTileRecord>& records
) {
    if (records.empty()) {
        throw std::runtime_error(
            "Gs3dTileReader: records are empty"
        );
    }

    if (records.size() != index_header.tile_count) {
        throw std::runtime_error(
            "Gs3dTileReader: record count does not match index header"
        );
    }

    std::uint64_t accumulated_points = 0;
    std::uint64_t accumulated_bytes = 0;

    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto& record = records[i];

        if (record.tile_id != static_cast<std::uint64_t>(i)) {
            throw std::runtime_error(
                "Gs3dTileReader: tile_id is not sequential"
            );
        }

        accumulated_points += record.point_count;
        accumulated_bytes += record.point_data_bytes;

        if (record.point_data_offset < sizeof(Gs3dTileDataFileHeader)) {
            throw std::runtime_error(
                "Gs3dTileReader: tile point_data_offset is before data payload"
            );
        }

        const std::uint64_t end_offset =
            record.point_data_offset +
            record.point_data_bytes;

        if (end_offset < record.point_data_offset) {
            throw std::runtime_error(
                "Gs3dTileReader: tile data offset overflow"
            );
        }
    }

    if (accumulated_points != index_header.total_point_count) {
        throw std::runtime_error(
            "Gs3dTileReader: accumulated point count mismatch"
        );
    }

    if (accumulated_points != data_header.total_point_count) {
        throw std::runtime_error(
            "Gs3dTileReader: accumulated point count does not match data header"
        );
    }

    if (accumulated_bytes != data_header.total_point_bytes) {
        throw std::runtime_error(
            "Gs3dTileReader: accumulated point bytes mismatch"
        );
    }
}

Gs3dTileDataFileHeader read_data_header(
    const std::filesystem::path& data_path
) {
    std::ifstream data_file(
        data_path,
        std::ios::binary
    );

    if (!data_file.is_open()) {
        throw std::runtime_error(
            "Gs3dTileReader: failed to open tile data file: " +
            data_path.string()
        );
    }

    const auto data_header =
        read_binary<Gs3dTileDataFileHeader>(
            data_file,
            "Gs3dTileReader: failed to read tile data header"
        );

    Gs3dTileFormat::validate_data_file_header(
        data_header
    );

    return data_header;
}

Gs3dHeader make_source_header_from_index_header(
    const Gs3dTileIndexFileHeader& index_header
) {
    Gs3dHeader header{};

    header.point_count =
        index_header.source_point_count;

    header.bbox_min_x = index_header.bbox_min_x;
    header.bbox_min_y = index_header.bbox_min_y;
    header.bbox_min_z = index_header.bbox_min_z;

    header.bbox_max_x = index_header.bbox_max_x;
    header.bbox_max_y = index_header.bbox_max_y;
    header.bbox_max_z = index_header.bbox_max_z;

    header.value_min = index_header.value_min;
    header.value_max = index_header.value_max;

    return header;
}

} // namespace

Gs3dTileReader Gs3dTileReader::open(
    const std::filesystem::path& index_path,
    const std::filesystem::path& data_path,
    const Gs3dHeader& source_header
) {
    validate_paths(
        index_path,
        data_path
    );

    std::ifstream index_file(
        index_path,
        std::ios::binary
    );

    if (!index_file.is_open()) {
        throw std::runtime_error(
            "Gs3dTileReader: failed to open tile index file: " +
            index_path.string()
        );
    }

    const auto index_header =
        read_binary<Gs3dTileIndexFileHeader>(
            index_file,
            "Gs3dTileReader: failed to read tile index header"
        );

    Gs3dTileFormat::validate_index_file_header(
        index_header
    );

    Gs3dTileFormat::validate_against_source(
        index_header,
        source_header
    );

    auto records =
        read_tile_records(
            index_file,
            index_header.tile_count
        );

    index_file.close();

    const auto data_header =
        read_data_header(data_path);

    Gs3dTileFormat::validate_index_against_data(
        index_header,
        data_header
    );

    validate_records_against_headers(
        index_header,
        data_header,
        records
    );

    Gs3dTileReader reader;
    reader.index_path_ = index_path;
    reader.data_path_ = data_path;
    reader.index_header_ = index_header;
    reader.data_header_ = data_header;
    reader.records_ = std::move(records);

    return reader;
}

Gs3dTileReader Gs3dTileReader::open_without_source_validation(
    const std::filesystem::path& index_path,
    const std::filesystem::path& data_path
) {
    validate_paths(
        index_path,
        data_path
    );

    std::ifstream index_file(
        index_path,
        std::ios::binary
    );

    if (!index_file.is_open()) {
        throw std::runtime_error(
            "Gs3dTileReader: failed to open tile index file: " +
            index_path.string()
        );
    }

    const auto index_header =
        read_binary<Gs3dTileIndexFileHeader>(
            index_file,
            "Gs3dTileReader: failed to read tile index header"
        );

    Gs3dTileFormat::validate_index_file_header(
        index_header
    );

    auto records =
        read_tile_records(
            index_file,
            index_header.tile_count
        );

    index_file.close();

    const auto data_header =
        read_data_header(data_path);

    Gs3dTileFormat::validate_index_against_data(
        index_header,
        data_header
    );

    const auto source_header =
        make_source_header_from_index_header(
            index_header
        );

    (void) source_header;

    validate_records_against_headers(
        index_header,
        data_header,
        records
    );

    Gs3dTileReader reader;
    reader.index_path_ = index_path;
    reader.data_path_ = data_path;
    reader.index_header_ = index_header;
    reader.data_header_ = data_header;
    reader.records_ = std::move(records);

    return reader;
}

bool Gs3dTileReader::valid() const noexcept {
    return !records_.empty() &&
           index_header_.tile_count == records_.size() &&
           data_header_.tile_count == records_.size();
}

const std::filesystem::path&
Gs3dTileReader::index_path() const noexcept {
    return index_path_;
}

const std::filesystem::path&
Gs3dTileReader::data_path() const noexcept {
    return data_path_;
}

const Gs3dTileIndexFileHeader&
Gs3dTileReader::index_header() const noexcept {
    return index_header_;
}

const Gs3dTileDataFileHeader&
Gs3dTileReader::data_header() const noexcept {
    return data_header_;
}

const std::vector<Gs3dTileRecord>&
Gs3dTileReader::records() const noexcept {
    return records_;
}

std::uint64_t Gs3dTileReader::tile_count() const noexcept {
    return static_cast<std::uint64_t>(
        records_.size()
    );
}

std::uint64_t Gs3dTileReader::total_point_count() const noexcept {
    return data_header_.total_point_count;
}

std::vector<Gs3dPoint> Gs3dTileReader::read_tile_points(
    std::uint64_t tile_id
) const {
    const auto& tile_record =
        record(tile_id);

    if (tile_record.point_count >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()
        )) {
        throw std::runtime_error(
            "Gs3dTileReader: tile point_count exceeds size_t range"
        );
    }

    std::ifstream data_file(
        data_path_,
        std::ios::binary
    );

    if (!data_file.is_open()) {
        throw std::runtime_error(
            "Gs3dTileReader: failed to open tile data file: " +
            data_path_.string()
        );
    }

    data_file.seekg(
        static_cast<std::streamoff>(tile_record.point_data_offset),
        std::ios::beg
    );

    if (!data_file.good()) {
        throw std::runtime_error(
            "Gs3dTileReader: failed to seek tile data"
        );
    }

    std::vector<Gs3dPoint> points(
        static_cast<std::size_t>(
            tile_record.point_count
        )
    );

    data_file.read(
        reinterpret_cast<char*>(points.data()),
        static_cast<std::streamsize>(
            tile_record.point_data_bytes
        )
    );

    if (!data_file.good()) {
        throw std::runtime_error(
            "Gs3dTileReader: failed to read tile points"
        );
    }

    return points;
}

std::vector<Gs3dTileRecord> Gs3dTileReader::query_records_by_bbox(
    const Gs3dTileQueryBox& box
) const {
    validate_query_box(box);

    std::vector<Gs3dTileRecord> result;

    for (const auto& record : records_) {
        if (intersects(record, box)) {
            result.push_back(record);
        }
    }

    return result;
}

std::vector<std::uint64_t> Gs3dTileReader::query_tile_ids_by_bbox(
    const Gs3dTileQueryBox& box
) const {
    validate_query_box(box);

    std::vector<std::uint64_t> result;

    for (const auto& record : records_) {
        if (intersects(record, box)) {
            result.push_back(record.tile_id);
        }
    }

    return result;
}

const Gs3dTileRecord& Gs3dTileReader::record(
    std::uint64_t tile_id
) const {
    if (tile_id >= records_.size()) {
        throw std::out_of_range(
            "Gs3dTileReader: tile_id out of range"
        );
    }

    return records_[static_cast<std::size_t>(tile_id)];
}

const Gs3dTileRecord&
Gs3dTileReader::max_point_count_record() const {
    if (records_.empty()) {
        throw std::runtime_error(
            "Gs3dTileReader: no tile records available"
        );
    }

    return *std::max_element(
        records_.begin(),
        records_.end(),
        [](const Gs3dTileRecord& a, const Gs3dTileRecord& b) {
            return a.point_count < b.point_count;
        }
    );
}

Gs3dTileReaderStats Gs3dTileReader::stats() const {
    Gs3dTileReaderStats s;

    s.index_path = index_path_;
    s.data_path = data_path_;
    s.tile_count = tile_count();
    s.total_point_count = data_header_.total_point_count;
    s.total_point_bytes = data_header_.total_point_bytes;
    s.index_file_bytes = file_size_or_zero(index_path_);
    s.data_file_bytes = file_size_or_zero(data_path_);
    s.success = valid();

    return s;
}

void Gs3dTileReader::validate_query_box(
    const Gs3dTileQueryBox& box
) {
    if (box.max_x < box.min_x ||
        box.max_y < box.min_y ||
        box.max_z < box.min_z) {
        throw std::runtime_error(
            "Gs3dTileReader: invalid query bbox"
        );
    }
}

bool Gs3dTileReader::intersects(
    const Gs3dTileRecord& record,
    const Gs3dTileQueryBox& box
) noexcept {
    if (record.bbox_max_x < box.min_x ||
        record.bbox_min_x > box.max_x) {
        return false;
    }

    if (record.bbox_max_y < box.min_y ||
        record.bbox_min_y > box.max_y) {
        return false;
    }

    if (record.bbox_max_z < box.min_z ||
        record.bbox_min_z > box.max_z) {
        return false;
    }

    return true;
}

} // namespace gs3d::data