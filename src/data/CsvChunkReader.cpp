#include "data/CsvChunkReader.hpp"

#include "data/CsvChunkUtils.hpp"
#include "data/Delimiter.hpp"
#include "preprocess/StatisticsPass.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace gs3d::data {
namespace {

bool is_blank_line(std::string_view line) {
    return std::all_of(
        line.begin(),
        line.end(),
        [](unsigned char c) {
            return std::isspace(c);
        }
    );
}

bool is_comment_line(
    std::string_view line,
    const CsvReadConfig& config
) {
    const auto trimmed = Delimiter::trim_view(line);
    return !trimmed.empty() && trimmed.front() == config.comment_char;
}

bool parse_float(std::string_view text, float& value) {
    const auto trimmed = Delimiter::trim_view(text);
    if (trimmed.empty()) {
        return false;
    }

    const auto result = std::from_chars(
        trimmed.data(),
        trimmed.data() + trimmed.size(),
        value
    );

    return result.ec == std::errc{} &&
           result.ptr == trimmed.data() + trimmed.size();
}

std::string trim_line_preview(
    std::string_view line,
    std::size_t max_len = 120
) {
    const auto trimmed = Delimiter::trim_view(line);
    if (trimmed.size() <= max_len) {
        return std::string(trimmed);
    }

    return std::string(trimmed.substr(0, max_len));
}

void append_error(
    std::vector<CsvParseError>& errors,
    const CsvChunkReaderConfig& config,
    CsvParseError error
) {
    if (errors.size() >= config.max_error_records) {
        return;
    }

    errors.push_back(std::move(error));
}

bool parse_record_from_line(
    std::string_view line,
    DelimiterMode delimiter_mode,
    const ResolvedDataSchema& schema,
    CsvPointRecord& record,
    CsvParseErrorCode& error_code
) {
    const auto fields = Delimiter::split(line, delimiter_mode);

    const auto has_col = [&](std::size_t col) -> bool {
        return col < fields.size();
    };

    if (!has_col(schema.x_col) ||
        !has_col(schema.y_col) ||
        !has_col(schema.z_col) ||
        !has_col(schema.primary_value_col)) {
        error_code = CsvParseErrorCode::TooFewFields;
        return false;
    }

    if (!parse_float(fields[schema.x_col], record.x) ||
        !parse_float(fields[schema.y_col], record.y) ||
        !parse_float(fields[schema.z_col], record.z) ||
        !parse_float(fields[schema.primary_value_col], record.primary_value)) {
        error_code = CsvParseErrorCode::InvalidFloat;
        return false;
    }

    error_code = CsvParseErrorCode::Unknown;
    return true;
}

void update_chunk_bounds(
    CsvChunkStatsResult& result,
    const CsvPointRecord& record
) {
    const double x = static_cast<double>(record.x);
    const double y = static_cast<double>(record.y);
    const double z = static_cast<double>(record.z);
    const float value = record.primary_value;

    if (result.empty) {
        result.xmin = x;
        result.xmax = x;
        result.ymin = y;
        result.ymax = y;
        result.zmin = z;
        result.zmax = z;
        result.value_min = value;
        result.value_max = value;
        result.empty = false;
        return;
    }

    result.xmin = std::min(result.xmin, x);
    result.xmax = std::max(result.xmax, x);
    result.ymin = std::min(result.ymin, y);
    result.ymax = std::max(result.ymax, y);
    result.zmin = std::min(result.zmin, z);
    result.zmax = std::max(result.zmax, z);
    result.value_min = std::min(result.value_min, value);
    result.value_max = std::max(result.value_max, value);
}

Gs3dPoint make_gs3d_point(
    const CsvPointRecord& record,
    const gs3d::preprocess::StatisticsResult& statistics
) {
    Gs3dPoint point{};
    point.x = static_cast<float>(
        static_cast<double>(record.x) - statistics.origin_x
    );
    point.y = static_cast<float>(
        static_cast<double>(record.y) - statistics.origin_y
    );
    point.z = static_cast<float>(
        static_cast<double>(record.z) - statistics.origin_z
    );
    point.value = record.primary_value;
    return point;
}

} // namespace

CsvChunkReader::CsvChunkReader(
    CsvReadConfig read_config,
    CsvChunkReaderConfig chunk_config
)
    : read_config_(std::move(read_config))
    , chunk_config_(std::move(chunk_config))
{
}

CsvChunkStatsResult CsvChunkReader::parse_chunk_for_stats(
    const std::filesystem::path& path,
    const CsvSniffResult& sniff,
    const CsvByteChunk& chunk
) const {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            "CsvChunkReader: failed to open file: " + path.string()
        );
    }

    file.seekg(static_cast<std::streamoff>(chunk.aligned_begin));

    CsvChunkStatsResult result;
    result.chunk_id = chunk.chunk_id;

    std::string line;
    std::uint64_t line_begin = 0;
    std::uint64_t line_end = 0;
    std::uint64_t local_line_number = 0;

    while (read_line_at_current_position(
        file,
        line,
        line_begin,
        line_end
    )) {
        if (line_begin >= chunk.aligned_end) {
            break;
        }

        ++local_line_number;
        ++result.total_lines;

        if (read_config_.skip_empty_lines && is_blank_line(line)) {
            continue;
        }

        if (read_config_.allow_comment_lines &&
            is_comment_line(line, read_config_)) {
            continue;
        }

        ++result.data_lines;

        CsvPointRecord record;
        CsvParseErrorCode error_code = CsvParseErrorCode::Unknown;
        if (parse_record_from_line(
                line,
                sniff.delimiter_mode,
                sniff.resolved_schema,
                record,
                error_code
            )) {
            ++result.valid_records;
            update_chunk_bounds(result, record);
        } else {
            ++result.invalid_records;
            append_error(
                result.errors,
                chunk_config_,
                CsvParseError{
                    .chunk_id = chunk.chunk_id,
                    .byte_offset = line_begin,
                    .line_number = local_line_number,
                    .code = error_code,
                    .line_preview = trim_line_preview(line),
                }
            );
        }
    }

    return result;
}

CsvChunkPointResult CsvChunkReader::parse_chunk_for_points(
    const std::filesystem::path& path,
    const CsvSniffResult& sniff,
    const CsvByteChunk& chunk,
    const gs3d::preprocess::StatisticsResult& statistics
) const {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            "CsvChunkReader: failed to open file: " + path.string()
        );
    }

    file.seekg(static_cast<std::streamoff>(chunk.aligned_begin));

    CsvChunkPointResult result;
    result.chunk_id = chunk.chunk_id;

    std::string line;
    std::uint64_t line_begin = 0;
    std::uint64_t line_end = 0;
    std::uint64_t local_line_number = 0;

    while (read_line_at_current_position(
        file,
        line,
        line_begin,
        line_end
    )) {
        if (line_begin >= chunk.aligned_end) {
            break;
        }

        ++local_line_number;

        if (read_config_.skip_empty_lines && is_blank_line(line)) {
            continue;
        }

        if (read_config_.allow_comment_lines &&
            is_comment_line(line, read_config_)) {
            continue;
        }

        CsvPointRecord record;
        CsvParseErrorCode error_code = CsvParseErrorCode::Unknown;
        if (parse_record_from_line(
                line,
                sniff.delimiter_mode,
                sniff.resolved_schema,
                record,
                error_code
            )) {
            ++result.valid_records;
            result.points.push_back(
                make_gs3d_point(record, statistics)
            );
        } else {
            ++result.invalid_records;
            append_error(
                result.errors,
                chunk_config_,
                CsvParseError{
                    .chunk_id = chunk.chunk_id,
                    .byte_offset = line_begin,
                    .line_number = local_line_number,
                    .code = error_code,
                    .line_preview = trim_line_preview(line),
                }
            );
        }
    }

    return result;
}

CsvChunkBufferedPointResult CsvChunkReader::parse_chunk_buffered_points(
    const std::filesystem::path& path,
    const CsvSniffResult& sniff,
    const CsvByteChunk& chunk
) const {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            "CsvChunkReader: failed to open file: " + path.string()
        );
    }

    file.seekg(static_cast<std::streamoff>(chunk.aligned_begin));

    CsvChunkBufferedPointResult result;
    result.chunk_id = chunk.chunk_id;

    std::string line;
    std::uint64_t line_begin = 0;
    std::uint64_t line_end = 0;
    std::uint64_t local_line_number = 0;

    while (read_line_at_current_position(
        file,
        line,
        line_begin,
        line_end
    )) {
        if (line_begin >= chunk.aligned_end) {
            break;
        }

        ++local_line_number;

        if (read_config_.skip_empty_lines && is_blank_line(line)) {
            continue;
        }

        if (read_config_.allow_comment_lines &&
            is_comment_line(line, read_config_)) {
            continue;
        }

        CsvPointRecord record;
        CsvParseErrorCode error_code = CsvParseErrorCode::Unknown;
        if (parse_record_from_line(
                line,
                sniff.delimiter_mode,
                sniff.resolved_schema,
                record,
                error_code
            )) {
            ++result.valid_records;

            const double x = static_cast<double>(record.x);
            const double y = static_cast<double>(record.y);
            const double z = static_cast<double>(record.z);
            const float value = record.primary_value;

            if (result.empty) {
                result.xmin = x;
                result.xmax = x;
                result.ymin = y;
                result.ymax = y;
                result.zmin = z;
                result.zmax = z;
                result.value_min = value;
                result.value_max = value;
                result.empty = false;
            } else {
                result.xmin = std::min(result.xmin, x);
                result.xmax = std::max(result.xmax, x);
                result.ymin = std::min(result.ymin, y);
                result.ymax = std::max(result.ymax, y);
                result.zmin = std::min(result.zmin, z);
                result.zmax = std::max(result.zmax, z);
                result.value_min = std::min(result.value_min, value);
                result.value_max = std::max(result.value_max, value);
            }

            result.points.push_back({
                record.x,
                record.y,
                record.z,
                record.primary_value
            });
        } else {
            ++result.invalid_records;
            append_error(
                result.errors,
                chunk_config_,
                CsvParseError{
                    .chunk_id = chunk.chunk_id,
                    .byte_offset = line_begin,
                    .line_number = local_line_number,
                    .code = error_code,
                    .line_preview = trim_line_preview(line),
                }
            );
        }
    }

    return result;
}

} // namespace gs3d::data
