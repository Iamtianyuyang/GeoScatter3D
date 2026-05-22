#include "data/CsvChunkReader.hpp"

#include "data/Delimiter.hpp"
#include "preprocess/StatisticsPass.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <vector>

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

struct RequiredFieldViews {
    std::string_view x;
    std::string_view y;
    std::string_view z;
    std::string_view value;
};

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

bool scan_required_fields_char_delimited(
    std::string_view line,
    const ResolvedDataSchema& schema,
    char delimiter,
    RequiredFieldViews& fields
) {
    const std::size_t required_max =
        std::max({schema.x_col, schema.y_col, schema.z_col, schema.primary_value_col});

    bool found_x = false;
    bool found_y = false;
    bool found_z = false;
    bool found_value = false;

    std::size_t field_index = 0;
    std::size_t start = 0;

    while (start <= line.size()) {
        const std::size_t pos = line.find(delimiter, start);
        const std::size_t field_end =
            pos == std::string_view::npos ? line.size() : pos;
        const auto field =
            Delimiter::trim_view(line.substr(start, field_end - start));

        if (field_index == schema.x_col) {
            fields.x = field;
            found_x = true;
        }
        if (field_index == schema.y_col) {
            fields.y = field;
            found_y = true;
        }
        if (field_index == schema.z_col) {
            fields.z = field;
            found_z = true;
        }
        if (field_index == schema.primary_value_col) {
            fields.value = field;
            found_value = true;
        }

        if (field_index >= required_max &&
            found_x && found_y && found_z && found_value) {
            break;
        }

        if (pos == std::string_view::npos) {
            break;
        }

        start = pos + 1;
        ++field_index;
    }

    return found_x && found_y && found_z && found_value;
}

bool scan_required_fields_whitespace(
    std::string_view line,
    const ResolvedDataSchema& schema,
    RequiredFieldViews& fields
) {
    const std::size_t required_max =
        std::max({schema.x_col, schema.y_col, schema.z_col, schema.primary_value_col});

    bool found_x = false;
    bool found_y = false;
    bool found_z = false;
    bool found_value = false;

    std::size_t i = 0;
    std::size_t field_index = 0;

    while (i < line.size()) {
        while (i < line.size() &&
               std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }

        if (i >= line.size()) {
            break;
        }

        const std::size_t start = i;
        while (i < line.size() &&
               !std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }

        const auto field = line.substr(start, i - start);

        if (field_index == schema.x_col) {
            fields.x = field;
            found_x = true;
        }
        if (field_index == schema.y_col) {
            fields.y = field;
            found_y = true;
        }
        if (field_index == schema.z_col) {
            fields.z = field;
            found_z = true;
        }
        if (field_index == schema.primary_value_col) {
            fields.value = field;
            found_value = true;
        }

        if (field_index >= required_max &&
            found_x && found_y && found_z && found_value) {
            break;
        }

        ++field_index;
    }

    return found_x && found_y && found_z && found_value;
}

bool scan_required_fields(
    std::string_view line,
    DelimiterMode delimiter_mode,
    const ResolvedDataSchema& schema,
    RequiredFieldViews& fields
) {
    switch (delimiter_mode) {
    case DelimiterMode::Comma:
        return scan_required_fields_char_delimited(line, schema, ',', fields);
    case DelimiterMode::Tab:
        return scan_required_fields_char_delimited(line, schema, '\t', fields);
    case DelimiterMode::Whitespace:
    case DelimiterMode::Auto:
    default:
        return scan_required_fields_whitespace(line, schema, fields);
    }
}

bool parse_record_from_line(
    std::string_view line,
    DelimiterMode delimiter_mode,
    const ResolvedDataSchema& schema,
    CsvPointRecord& record,
    CsvParseErrorCode& error_code
) {
    RequiredFieldViews fields;
    if (!scan_required_fields(
            line,
            delimiter_mode,
            schema,
            fields
        )) {
        error_code = CsvParseErrorCode::TooFewFields;
        return false;
    }

    if (!parse_float(fields.x, record.x) ||
        !parse_float(fields.y, record.y) ||
        !parse_float(fields.z, record.z) ||
        !parse_float(fields.value, record.primary_value)) {
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

std::vector<char> read_chunk_buffer(
    const std::filesystem::path& path,
    const CsvByteChunk& chunk
) {
    if (chunk.aligned_end <= chunk.aligned_begin) {
        return {};
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            "CsvChunkReader: failed to open file: " + path.string()
        );
    }

    constexpr std::size_t kReadBufSize = 16 * 1024 * 1024;
    std::vector<char> stream_buf(kReadBufSize);
    file.rdbuf()->pubsetbuf(
        stream_buf.data(),
        static_cast<std::streamsize>(stream_buf.size())
    );

    const auto byte_count = chunk.aligned_end - chunk.aligned_begin;
    if (byte_count >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error(
            "CsvChunkReader: chunk byte size exceeds size_t range"
        );
    }

    std::vector<char> buffer(static_cast<std::size_t>(byte_count));
    file.seekg(static_cast<std::streamoff>(chunk.aligned_begin));
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));

    const auto read_count = file.gcount();
    if (read_count < 0) {
        throw std::runtime_error("CsvChunkReader: failed to read chunk buffer");
    }

    buffer.resize(static_cast<std::size_t>(read_count));
    return buffer;
}

template <typename RowCallback>
void for_each_chunk_line(
    const std::vector<char>& buffer,
    std::uint64_t chunk_begin_offset,
    const RowCallback& callback
) {
    std::size_t line_start = 0;
    std::uint64_t local_line_number = 0;

    while (line_start < buffer.size()) {
        std::size_t line_end = line_start;
        while (line_end < buffer.size() && buffer[line_end] != '\n') {
            ++line_end;
        }

        std::string_view line(
            buffer.data() + line_start,
            line_end - line_start
        );

        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        ++local_line_number;
        callback(
            line,
            chunk_begin_offset + static_cast<std::uint64_t>(line_start),
            local_line_number
        );

        if (line_end >= buffer.size()) {
            break;
        }

        line_start = line_end + 1;
    }
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
    CsvChunkStatsResult result;
    result.chunk_id = chunk.chunk_id;

    const auto buffer = read_chunk_buffer(path, chunk);

    for_each_chunk_line(
        buffer,
        chunk.aligned_begin,
        [&](std::string_view line,
            std::uint64_t line_begin,
            std::uint64_t local_line_number) {
            ++result.total_lines;

            if (read_config_.skip_empty_lines && is_blank_line(line)) {
                return;
            }

            if (read_config_.allow_comment_lines &&
                is_comment_line(line, read_config_)) {
                return;
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
    );

    return result;
}

CsvChunkPointResult CsvChunkReader::parse_chunk_for_points(
    const std::filesystem::path& path,
    const CsvSniffResult& sniff,
    const CsvByteChunk& chunk,
    const gs3d::preprocess::StatisticsResult& statistics
) const {
    CsvChunkPointResult result;
    result.chunk_id = chunk.chunk_id;

    const auto buffer = read_chunk_buffer(path, chunk);

    for_each_chunk_line(
        buffer,
        chunk.aligned_begin,
        [&](std::string_view line,
            std::uint64_t line_begin,
            std::uint64_t local_line_number) {
            if (read_config_.skip_empty_lines && is_blank_line(line)) {
                return;
            }

            if (read_config_.allow_comment_lines &&
                is_comment_line(line, read_config_)) {
                return;
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
    );

    return result;
}

CsvChunkBufferedPointResult CsvChunkReader::parse_chunk_buffered_points(
    const std::filesystem::path& path,
    const CsvSniffResult& sniff,
    const CsvByteChunk& chunk
) const {
    CsvChunkBufferedPointResult result;
    result.chunk_id = chunk.chunk_id;

    const auto buffer = read_chunk_buffer(path, chunk);

    for_each_chunk_line(
        buffer,
        chunk.aligned_begin,
        [&](std::string_view line,
            std::uint64_t line_begin,
            std::uint64_t local_line_number) {
            if (read_config_.skip_empty_lines && is_blank_line(line)) {
                return;
            }

            if (read_config_.allow_comment_lines &&
                is_comment_line(line, read_config_)) {
                return;
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
    );

    return result;
}

} // namespace gs3d::data
