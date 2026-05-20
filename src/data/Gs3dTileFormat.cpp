#include "data/Gs3dTileFormat.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace gs3d::data {

bool Gs3dTileFormat::is_valid_index_magic(
    const std::array<char, 8>& magic
) noexcept {
    return magic == GS3D_TILE_INDEX_MAGIC;
}

bool Gs3dTileFormat::is_valid_data_magic(
    const std::array<char, 8>& magic
) noexcept {
    return magic == GS3D_TILE_DATA_MAGIC;
}

bool Gs3dTileFormat::is_supported_version(
    std::uint32_t version
) noexcept {
    return version == GS3D_TILE_VERSION;
}

Gs3dTileIndexFileHeader Gs3dTileFormat::make_index_file_header(
    const Gs3dHeader& source_header,
    std::uint64_t tile_count,
    std::uint64_t total_point_count,
    float tile_size_x,
    float tile_size_y,
    float grid_origin_x,
    float grid_origin_y,
    std::uint32_t grid_count_x,
    std::uint32_t grid_count_y,
    Gs3dTileSplitMode split_mode
) {
    Gs3dTileIndexFileHeader header;

    header.magic = GS3D_TILE_INDEX_MAGIC;
    header.version = GS3D_TILE_VERSION;
    header.header_size =
        static_cast<std::uint32_t>(
            sizeof(Gs3dTileIndexFileHeader)
        );

    header.source_point_count = source_header.point_count;
    header.tile_count = tile_count;
    header.total_point_count = total_point_count;

    header.point_stride =
        static_cast<std::uint32_t>(sizeof(Gs3dPoint));

    header.tile_record_size =
        static_cast<std::uint32_t>(sizeof(Gs3dTileRecord));

    header.tile_size_x = tile_size_x;
    header.tile_size_y = tile_size_y;

    header.grid_origin_x = grid_origin_x;
    header.grid_origin_y = grid_origin_y;

    header.grid_count_x = grid_count_x;
    header.grid_count_y = grid_count_y;

    header.bbox_min_x = source_header.bbox_min_x;
    header.bbox_min_y = source_header.bbox_min_y;
    header.bbox_min_z = source_header.bbox_min_z;

    header.bbox_max_x = source_header.bbox_max_x;
    header.bbox_max_y = source_header.bbox_max_y;
    header.bbox_max_z = source_header.bbox_max_z;

    header.value_min = source_header.value_min;
    header.value_max = source_header.value_max;

    header.split_mode =
        static_cast<std::uint32_t>(split_mode);

    validate_index_file_header(header);

    return header;
}

Gs3dTileDataFileHeader Gs3dTileFormat::make_data_file_header(
    const Gs3dHeader& source_header,
    std::uint64_t tile_count,
    std::uint64_t total_point_count
) {
    Gs3dTileDataFileHeader header;

    header.magic = GS3D_TILE_DATA_MAGIC;
    header.version = GS3D_TILE_VERSION;
    header.header_size =
        static_cast<std::uint32_t>(
            sizeof(Gs3dTileDataFileHeader)
        );

    header.source_point_count = source_header.point_count;
    header.tile_count = tile_count;
    header.total_point_count = total_point_count;

    header.total_point_bytes =
        total_point_count *
        static_cast<std::uint64_t>(sizeof(Gs3dPoint));

    header.point_stride =
        static_cast<std::uint32_t>(sizeof(Gs3dPoint));

    validate_data_file_header(header);

    return header;
}

Gs3dTileRecord Gs3dTileFormat::make_tile_record(
    std::uint64_t tile_id,
    std::uint32_t tile_x,
    std::uint32_t tile_y,
    std::uint64_t point_count,
    std::uint64_t point_data_offset,
    float bbox_min_x,
    float bbox_min_y,
    float bbox_min_z,
    float bbox_max_x,
    float bbox_max_y,
    float bbox_max_z,
    float value_min,
    float value_max
) {
    Gs3dTileRecord record;

    record.tile_id = tile_id;
    record.tile_x = tile_x;
    record.tile_y = tile_y;

    record.point_count = point_count;
    record.point_data_offset = point_data_offset;

    record.point_data_bytes =
        point_count *
        static_cast<std::uint64_t>(sizeof(Gs3dPoint));

    record.bbox_min_x = bbox_min_x;
    record.bbox_min_y = bbox_min_y;
    record.bbox_min_z = bbox_min_z;

    record.bbox_max_x = bbox_max_x;
    record.bbox_max_y = bbox_max_y;
    record.bbox_max_z = bbox_max_z;

    record.value_min = value_min;
    record.value_max = value_max;

    validate_tile_record(record);

    return record;
}

void Gs3dTileFormat::validate_index_file_header(
    const Gs3dTileIndexFileHeader& header
) {
    if (!is_valid_index_magic(header.magic)) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid tile index magic"
        );
    }

    if (!is_supported_version(header.version)) {
        throw std::runtime_error(
            "Gs3dTileFormat: unsupported tile index version"
        );
    }

    if (header.header_size != sizeof(Gs3dTileIndexFileHeader)) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid tile index header size"
        );
    }

    if (header.source_point_count == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: source_point_count is zero"
        );
    }

    if (header.tile_count == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: tile_count is zero"
        );
    }

    if (header.total_point_count == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: total_point_count is zero"
        );
    }

    if (header.point_stride != sizeof(Gs3dPoint)) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid point stride"
        );
    }

    if (header.tile_record_size != sizeof(Gs3dTileRecord)) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid tile record size"
        );
    }

    if (header.tile_size_x <= 0.0f ||
        header.tile_size_y <= 0.0f) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid tile size"
        );
    }

    if (header.grid_count_x == 0 ||
        header.grid_count_y == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid grid count"
        );
    }

    if (header.bbox_max_x < header.bbox_min_x ||
        header.bbox_max_y < header.bbox_min_y ||
        header.bbox_max_z < header.bbox_min_z) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid source bbox"
        );
    }

    if (header.value_max < header.value_min) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid value range"
        );
    }

    (void) from_format_split_mode(header.split_mode);
}

void Gs3dTileFormat::validate_data_file_header(
    const Gs3dTileDataFileHeader& header
) {
    if (!is_valid_data_magic(header.magic)) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid tile data magic"
        );
    }

    if (!is_supported_version(header.version)) {
        throw std::runtime_error(
            "Gs3dTileFormat: unsupported tile data version"
        );
    }

    if (header.header_size != sizeof(Gs3dTileDataFileHeader)) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid tile data header size"
        );
    }

    if (header.source_point_count == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: data source_point_count is zero"
        );
    }

    if (header.tile_count == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: data tile_count is zero"
        );
    }

    if (header.total_point_count == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: data total_point_count is zero"
        );
    }

    if (header.point_stride != sizeof(Gs3dPoint)) {
        throw std::runtime_error(
            "Gs3dTileFormat: data invalid point stride"
        );
    }

    const std::uint64_t expected_bytes =
        header.total_point_count *
        static_cast<std::uint64_t>(sizeof(Gs3dPoint));

    if (header.total_point_bytes != expected_bytes) {
        throw std::runtime_error(
            "Gs3dTileFormat: data total_point_bytes mismatch"
        );
    }
}

void Gs3dTileFormat::validate_tile_record(
    const Gs3dTileRecord& record
) {
    if (record.point_count == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: tile point_count is zero"
        );
    }

    const std::uint64_t expected_bytes =
        record.point_count *
        static_cast<std::uint64_t>(sizeof(Gs3dPoint));

    if (record.point_data_bytes != expected_bytes) {
        throw std::runtime_error(
            "Gs3dTileFormat: tile point_data_bytes mismatch"
        );
    }

    if (record.bbox_max_x < record.bbox_min_x ||
        record.bbox_max_y < record.bbox_min_y ||
        record.bbox_max_z < record.bbox_min_z) {
        throw std::runtime_error(
            "Gs3dTileFormat: tile invalid bbox"
        );
    }

    if (record.value_max < record.value_min) {
        throw std::runtime_error(
            "Gs3dTileFormat: tile invalid value range"
        );
    }
}

void Gs3dTileFormat::validate_index_against_data(
    const Gs3dTileIndexFileHeader& index_header,
    const Gs3dTileDataFileHeader& data_header
) {
    validate_index_file_header(index_header);
    validate_data_file_header(data_header);

    if (index_header.source_point_count !=
        data_header.source_point_count) {
        throw std::runtime_error(
            "Gs3dTileFormat: source_point_count mismatch"
        );
    }

    if (index_header.tile_count != data_header.tile_count) {
        throw std::runtime_error(
            "Gs3dTileFormat: tile_count mismatch"
        );
    }

    if (index_header.total_point_count !=
        data_header.total_point_count) {
        throw std::runtime_error(
            "Gs3dTileFormat: total_point_count mismatch"
        );
    }

    if (index_header.point_stride != data_header.point_stride) {
        throw std::runtime_error(
            "Gs3dTileFormat: point_stride mismatch"
        );
    }
}

void Gs3dTileFormat::validate_against_source(
    const Gs3dTileIndexFileHeader& index_header,
    const Gs3dHeader& source_header
) {
    validate_index_file_header(index_header);

    if (index_header.source_point_count !=
        source_header.point_count) {
        throw std::runtime_error(
            "Gs3dTileFormat: source point_count mismatch"
        );
    }

    if (!approximately_equal(index_header.bbox_min_x, source_header.bbox_min_x) ||
        !approximately_equal(index_header.bbox_min_y, source_header.bbox_min_y) ||
        !approximately_equal(index_header.bbox_min_z, source_header.bbox_min_z) ||
        !approximately_equal(index_header.bbox_max_x, source_header.bbox_max_x) ||
        !approximately_equal(index_header.bbox_max_y, source_header.bbox_max_y) ||
        !approximately_equal(index_header.bbox_max_z, source_header.bbox_max_z)) {
        throw std::runtime_error(
            "Gs3dTileFormat: source bbox mismatch"
        );
    }

    if (!approximately_equal(index_header.value_min, source_header.value_min) ||
        !approximately_equal(index_header.value_max, source_header.value_max)) {
        throw std::runtime_error(
            "Gs3dTileFormat: source value range mismatch"
        );
    }
}

Gs3dTileSplitMode Gs3dTileFormat::from_format_split_mode(
    std::uint32_t mode
) {
    const auto typed_mode =
        static_cast<Gs3dTileSplitMode>(mode);

    switch (typed_mode) {
    case Gs3dTileSplitMode::XY:
        return Gs3dTileSplitMode::XY;

    default:
        throw std::runtime_error(
            "Gs3dTileFormat: unsupported tile split mode"
        );
    }
}

const char* Gs3dTileFormat::split_mode_name(
    std::uint32_t mode
) noexcept {
    const auto typed_mode =
        static_cast<Gs3dTileSplitMode>(mode);

    switch (typed_mode) {
    case Gs3dTileSplitMode::XY:
        return "XY";

    default:
        return "Unknown";
    }
}

bool Gs3dTileFormat::approximately_equal(
    float a,
    float b,
    float epsilon
) noexcept {
    return std::abs(a - b) <= epsilon;
}

std::string Gs3dTileFormat::index_file_header_summary(
    const Gs3dTileIndexFileHeader& header
) {
    std::ostringstream oss;

    oss << "Gs3dTileIndexFileHeader\n";

    oss << "  magic: ";
    for (const char c : header.magic) {
        if (c == '\0') {
            break;
        }

        oss << c;
    }
    oss << '\n';

    oss << "  version: "
        << header.version
        << '\n';

    oss << "  header_size: "
        << header.header_size
        << '\n';

    oss << "  source_point_count: "
        << header.source_point_count
        << '\n';

    oss << "  tile_count: "
        << header.tile_count
        << '\n';

    oss << "  total_point_count: "
        << header.total_point_count
        << '\n';

    oss << "  point_stride: "
        << header.point_stride
        << '\n';

    oss << "  tile_record_size: "
        << header.tile_record_size
        << '\n';

    oss << "  tile_size: ["
        << header.tile_size_x
        << ", "
        << header.tile_size_y
        << "]\n";

    oss << "  grid_origin: ["
        << header.grid_origin_x
        << ", "
        << header.grid_origin_y
        << "]\n";

    oss << "  grid_count: ["
        << header.grid_count_x
        << ", "
        << header.grid_count_y
        << "]\n";

    oss << "  bbox_min: ["
        << header.bbox_min_x << ", "
        << header.bbox_min_y << ", "
        << header.bbox_min_z << "]\n";

    oss << "  bbox_max: ["
        << header.bbox_max_x << ", "
        << header.bbox_max_y << ", "
        << header.bbox_max_z << "]\n";

    oss << "  value_range: ["
        << header.value_min
        << ", "
        << header.value_max
        << "]\n";

    oss << "  split_mode: "
        << split_mode_name(header.split_mode)
        << '\n';

    return oss.str();
}

std::string Gs3dTileFormat::data_file_header_summary(
    const Gs3dTileDataFileHeader& header
) {
    std::ostringstream oss;

    oss << "Gs3dTileDataFileHeader\n";

    oss << "  magic: ";
    for (const char c : header.magic) {
        if (c == '\0') {
            break;
        }

        oss << c;
    }
    oss << '\n';

    oss << "  version: "
        << header.version
        << '\n';

    oss << "  header_size: "
        << header.header_size
        << '\n';

    oss << "  source_point_count: "
        << header.source_point_count
        << '\n';

    oss << "  tile_count: "
        << header.tile_count
        << '\n';

    oss << "  total_point_count: "
        << header.total_point_count
        << '\n';

    oss << "  total_point_bytes: "
        << header.total_point_bytes
        << '\n';

    oss << "  point_stride: "
        << header.point_stride
        << '\n';

    return oss.str();
}

std::string Gs3dTileFormat::tile_record_summary(
    const Gs3dTileRecord& record
) {
    std::ostringstream oss;

    oss << "Gs3dTileRecord\n";

    oss << "  tile_id: "
        << record.tile_id
        << '\n';

    oss << "  tile_xy: ["
        << record.tile_x
        << ", "
        << record.tile_y
        << "]\n";

    oss << "  point_count: "
        << record.point_count
        << '\n';

    oss << "  point_data_offset: "
        << record.point_data_offset
        << '\n';

    oss << "  point_data_bytes: "
        << record.point_data_bytes
        << '\n';

    oss << "  bbox_min: ["
        << record.bbox_min_x << ", "
        << record.bbox_min_y << ", "
        << record.bbox_min_z << "]\n";

    oss << "  bbox_max: ["
        << record.bbox_max_x << ", "
        << record.bbox_max_y << ", "
        << record.bbox_max_z << "]\n";

    oss << "  value_range: ["
        << record.value_min
        << ", "
        << record.value_max
        << "]\n";

    return oss.str();
}

} // namespace gs3d::data