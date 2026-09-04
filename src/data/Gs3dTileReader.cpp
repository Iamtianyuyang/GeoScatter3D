#include "data/Gs3dTileReader.hpp"
#include "platform/MemoryMappedFile.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace gs3d::data {

namespace {

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
        Gs3dTileRecord record{};

        Gs3dTileFormat::read_tile_record(
            file,
            record,
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

    const std::uint64_t payload_begin =
        sizeof(Gs3dTileDataFileHeader);
    const std::uint64_t payload_end =
        payload_begin + data_header.total_point_bytes;

    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto& record = records[i];

        if (record.tile_id != static_cast<std::uint64_t>(i)) {
            throw std::runtime_error(
                "Gs3dTileReader: tile_id is not sequential"
            );
        }

        /*
         * 逐记录强校验：每条记录必须严格符合 data header 声明的 stride。
         * 聚合校验（累计字节数 == total_point_bytes）无法发现混合 stride
         * 的损坏/手工拼装文件，这里逐记录拒绝，禁止读取端按错误 stride
         * 解码导致尾部点静默置零。
         */
        Gs3dTileFormat::validate_tile_record(
            record,
            data_header.point_stride
        );

        accumulated_points += record.point_count;
        accumulated_bytes += record.point_data_bytes;

        if (record.point_data_offset < payload_begin) {
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

        /*
         * 记录的数据区间必须落在 data payload 范围内，且相邻记录必须
         * 连续铺满 payload，不允许空洞/重叠（损坏或手工拼装文件的典型
         * 特征，旧代码会在读取时静默读到空洞处的零字节）。
         */
        if (end_offset > payload_end) {
            throw std::runtime_error(
                "Gs3dTileReader: tile data range exceeds data payload"
            );
        }

        if (i > 0) {
            const auto& previous = records[i - 1];
            const std::uint64_t previous_end =
                previous.point_data_offset +
                previous.point_data_bytes;

            if (record.point_data_offset != previous_end) {
                throw std::runtime_error(
                    "Gs3dTileReader: tile data ranges are not contiguous"
                );
            }
        } else if (record.point_data_offset != payload_begin) {
            throw std::runtime_error(
                "Gs3dTileReader: first tile data offset is not at payload start"
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

    if (records.back().point_data_offset +
            records.back().point_data_bytes !=
        payload_end) {
        throw std::runtime_error(
            "Gs3dTileReader: tile data does not cover the full payload"
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

    Gs3dTileDataFileHeader data_header{};

    Gs3dTileFormat::read_data_file_header(
        data_file,
        data_header,
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

    Gs3dTileIndexFileHeader index_header{};
    Gs3dTileFormat::read_index_file_header(
        index_file,
        index_header,
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
    reader.mmap_data_ =
        gs3d::platform::MemoryMappedFile::open_read(data_path);
    reader.build_grid_map();

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

    Gs3dTileIndexFileHeader index_header{};
    Gs3dTileFormat::read_index_file_header(
        index_file,
        index_header,
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
    reader.mmap_data_ =
        gs3d::platform::MemoryMappedFile::open_read(data_path);
    reader.build_grid_map();

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

    validate_record_stride(
        tile_record
    );

    if (tile_record.point_count >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()
        )) {
        throw std::runtime_error(
            "Gs3dTileReader: tile point_count exceeds size_t range"
        );
    }

    /*
     * v2 format stores Gs3dPointWithId (20 bytes) interleaved.
     * read_tile_points_with_ids handles the split correctly.
     * Delegate to avoid a heap buffer overflow from reading
     * 20-byte records into a 16-byte Gs3dPoint buffer.
     */
    if (has_embedded_point_ids()) {
        auto block = read_tile_points_with_ids(tile_id);
        return std::move(block.points);
    }

    const auto count =
        static_cast<std::size_t>(tile_record.point_count);

    if (mmap_data_ && mmap_data_->is_open() &&
        tile_record.point_data_offset + tile_record.point_data_bytes <= mmap_data_->size()) {
        if constexpr (std::endian::native == std::endian::little) {
            const auto* raw = reinterpret_cast<const Gs3dPoint*>(
                mmap_data_->data() + tile_record.point_data_offset
            );
            return std::vector<Gs3dPoint>(raw, raw + count);
        }
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

    std::vector<Gs3dPoint> points(count);

    Gs3dTileFormat::read_points(
        data_file,
        points,
        "Gs3dTileReader: failed to read tile points"
    );

    return points;
}

bool Gs3dTileReader::has_embedded_point_ids() const noexcept {
    return Gs3dTileFormat::has_embedded_point_ids(
        data_header_.version
    );
}

Gs3dTilePointBlock Gs3dTileReader::read_tile_points_with_ids(
    std::uint64_t tile_id
) const {
    const auto& tile_record =
        record(tile_id);

    validate_record_stride(
        tile_record
    );

    if (tile_record.point_count >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()
        )) {
        throw std::runtime_error(
            "Gs3dTileReader: tile point_count exceeds size_t range"
        );
    }

    const auto count =
        static_cast<std::size_t>(tile_record.point_count);

    Gs3dTilePointBlock result;
    result.points.reserve(count);
    result.point_ids.reserve(count);

    if (mmap_data_ && mmap_data_->is_open() &&
        tile_record.point_data_offset + tile_record.point_data_bytes <= mmap_data_->size()) {
        const auto* base =
            mmap_data_->data() + tile_record.point_data_offset;
        if (has_embedded_point_ids()) {
            if constexpr (std::endian::native == std::endian::little) {
                const auto* raw =
                    reinterpret_cast<const Gs3dPointWithId*>(base);
                result.points.resize(count);
                result.point_ids.resize(count);
                for (std::size_t i = 0; i < count; ++i) {
                    result.points[i] = {
                        raw[i].x,
                        raw[i].y,
                        raw[i].z,
                        raw[i].value
                    };
                    result.point_ids[i] = raw[i].point_id;
                }
                return result;
            }
        } else {
            if constexpr (std::endian::native == std::endian::little) {
                const auto* raw =
                    reinterpret_cast<const Gs3dPoint*>(base);
                result.points.assign(raw, raw + count);
                return result;
            }
        }
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

    if (has_embedded_point_ids()) {
        /*
         * v2 interleaved format:  Gs3dPointWithId (20 bytes).
         * Read all elements in one shot, then split into
         * separate points / point_ids vectors.
         */
        std::vector<Gs3dPointWithId> raw(count);

        Gs3dTileFormat::read_points(
            data_file,
            raw,
            "Gs3dTileReader: failed to read tile points with ids"
        );

        result.points.resize(count);
        result.point_ids.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            result.points[i] = {
                raw[i].x,
                raw[i].y,
                raw[i].z,
                raw[i].value
            };
            result.point_ids[i] = raw[i].point_id;
        }
    } else {
        /*
         * v1 format:  Gs3dPoint (16 bytes), no embedded IDs.
         * Read points only; point_ids stays empty.
         */
        result.points.resize(count);

        Gs3dTileFormat::read_points(
            data_file,
            result.points,
            "Gs3dTileReader: failed to read tile points (v1)"
        );
    }

    return result;
}

void Gs3dTileReader::validate_record_stride(
    const Gs3dTileRecord& record
) const {
    const std::uint64_t expected_bytes =
        record.point_count *
        static_cast<std::uint64_t>(data_header_.point_stride);

    if (record.point_data_bytes != expected_bytes) {
        throw std::runtime_error(
            "Gs3dTileReader: tile record point_data_bytes does not match "
            "the data header stride"
        );
    }
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

void Gs3dTileReader::build_grid_map() noexcept {
    grid_to_tile_id_.clear();
    grid_to_tile_id_.reserve(records_.size());

    const std::uint32_t gx = index_header_.grid_count_x;

    for (const auto& rec : records_) {
        const std::uint64_t key =
            static_cast<std::uint64_t>(rec.tile_y) * gx +
            static_cast<std::uint64_t>(rec.tile_x);

        grid_to_tile_id_.emplace(key, rec.tile_id);
    }
}

std::vector<std::uint64_t> Gs3dTileReader::query_tile_ids_by_grid(
    const Gs3dTileQueryBox& box
) const {
    validate_query_box(box);

    const auto& h = index_header_;

    if (h.tile_size_x <= 0.0f || h.tile_size_y <= 0.0f ||
        h.grid_count_x == 0 || h.grid_count_y == 0) {
        return {};
    }

    const float grid_max_x =
        h.grid_origin_x + static_cast<float>(h.grid_count_x) * h.tile_size_x;
    const float grid_max_y =
        h.grid_origin_y + static_cast<float>(h.grid_count_y) * h.tile_size_y;

    if (box.max_x < h.grid_origin_x || box.min_x >= grid_max_x ||
        box.max_y < h.grid_origin_y || box.min_y >= grid_max_y) {
        return {};
    }

    auto cell_coord = [](
        float value,
        float origin,
        float tile_size,
        std::uint32_t grid_count
    ) -> std::uint32_t {
        const auto raw = static_cast<std::int64_t>(
            std::floor((value - origin) / tile_size)
        );

        if (raw < 0) {
            return 0;
        }

        const auto upper = static_cast<std::int64_t>(grid_count - 1);

        if (raw > upper) {
            return grid_count - 1;
        }

        return static_cast<std::uint32_t>(raw);
    };

    const std::uint32_t tx_min =
        cell_coord(box.min_x, h.grid_origin_x, h.tile_size_x, h.grid_count_x);
    const std::uint32_t tx_max =
        cell_coord(box.max_x, h.grid_origin_x, h.tile_size_x, h.grid_count_x);
    const std::uint32_t ty_min =
        cell_coord(box.min_y, h.grid_origin_y, h.tile_size_y, h.grid_count_y);
    const std::uint32_t ty_max =
        cell_coord(box.max_y, h.grid_origin_y, h.tile_size_y, h.grid_count_y);

    std::vector<std::uint64_t> result;

    for (std::uint32_t ty = ty_min; ty <= ty_max; ++ty) {
        for (std::uint32_t tx = tx_min; tx <= tx_max; ++tx) {
            const std::uint64_t key =
                static_cast<std::uint64_t>(ty) * h.grid_count_x +
                static_cast<std::uint64_t>(tx);

            const auto it = grid_to_tile_id_.find(key);

            if (it != grid_to_tile_id_.end()) {
                result.push_back(it->second);
            }
        }
    }

    return result;
}

} // namespace gs3d::data