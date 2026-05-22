#include "data/CsvStreamReader.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace gs3d::data {

CsvStreamReader::CsvStreamReader(CsvReadConfig config)
    : config_(std::move(config))
{
}

CsvReadStats CsvStreamReader::read(
    const std::filesystem::path& path,
    const RecordCallback& callback
) const {
    if (!callback) {
        throw std::invalid_argument("CsvStreamReader: callback is empty");
    }

    if (!config_.has_header) {
        throw std::runtime_error(
            "CsvStreamReader: current version requires a header line"
        );
    }

    std::ifstream file(path, std::ios::in);
    if (!file.is_open()) {
        throw std::runtime_error(
            "CsvStreamReader: failed to open file: " + path.string()
        );
    }

    constexpr std::size_t kReadBufSize = 16 * 1024 * 1024;
    std::vector<char> read_buf(kReadBufSize);
    file.rdbuf()->pubsetbuf(
        read_buf.data(),
        static_cast<std::streamsize>(kReadBufSize)
    );

    CsvReadStats stats;

    std::string line;
    std::string header_line;

    while (std::getline(file, line)) {
        ++stats.total_lines;

        if (config_.skip_empty_lines && is_blank_line(line)) {
            continue;
        }

        if (config_.allow_comment_lines && is_comment_line(line)) {
            continue;
        }

        header_line = line;
        ++stats.header_lines;
        break;
    }

    if (header_line.empty()) {
        throw std::runtime_error("CsvStreamReader: header line not found");
    }

    DelimiterMode delimiter_mode = config_.delimiter_mode;
    if (delimiter_mode == DelimiterMode::Auto) {
        delimiter_mode = Delimiter::detect(header_line);
    }

    const auto header_views = Delimiter::split(header_line, delimiter_mode);

    std::vector<std::string> header_fields;
    header_fields.reserve(header_views.size());

    for (const auto field : header_views) {
        header_fields.push_back(Delimiter::trim_copy(field));
    }

    const auto field_map = build_field_map(
        header_fields,
        config_.schema.case_insensitive
    );

    const ResolvedDataSchema resolved_schema =
        config_.schema.resolve(field_map, header_fields);

    while (std::getline(file, line)) {
        ++stats.total_lines;

        if (config_.skip_empty_lines && is_blank_line(line)) {
            continue;
        }

        if (config_.allow_comment_lines && is_comment_line(line)) {
            continue;
        }

        ++stats.data_lines;

        const auto fields = Delimiter::split(line, delimiter_mode);

        CsvPointRecord record;
        if (parse_record(fields, resolved_schema, record)) {
            callback(record, stats.total_lines);
            ++stats.valid_records;
        } else {
            ++stats.invalid_records;
        }
    }

    return stats;
}

bool CsvStreamReader::is_blank_line(const std::string& line) {
    return std::all_of(
        line.begin(),
        line.end(),
        [](unsigned char c) {
            return std::isspace(c);
        }
    );
}

bool CsvStreamReader::is_comment_line(const std::string& line) const {
    const auto trimmed = Delimiter::trim_copy(line);

    if (trimmed.empty()) {
        return false;
    }

    return trimmed.front() == config_.comment_char;
}

bool CsvStreamReader::parse_float(std::string_view text, float& value) {
    const auto trimmed = Delimiter::trim_view(text);

    if (trimmed.empty()) {
        return false;
    }

    const auto result = std::from_chars(
        trimmed.data(),
        trimmed.data() + trimmed.size(),
        value
    );

    if (result.ec != std::errc{}) {
        return false;
    }

    return result.ptr == trimmed.data() + trimmed.size();
}

std::unordered_map<std::string, std::size_t> CsvStreamReader::build_field_map(
    const std::vector<std::string>& header_fields,
    bool case_insensitive
) {
    std::unordered_map<std::string, std::size_t> field_map;

    for (std::size_t i = 0; i < header_fields.size(); ++i) {
        const std::string key = Delimiter::normalize_field_name(
            header_fields[i],
            case_insensitive
        );

        if (key.empty()) {
            throw std::runtime_error(
                "CsvStreamReader: empty field name in header"
            );
        }

        if (field_map.contains(key)) {
            throw std::runtime_error(
                "CsvStreamReader: duplicated field name in header: " + key
            );
        }

        field_map.emplace(key, i);
    }

    return field_map;
}

bool CsvStreamReader::parse_record(
    const std::vector<std::string_view>& fields,
    const ResolvedDataSchema& schema,
    CsvPointRecord& record
) const {
    const auto has_col = [&](std::size_t col) -> bool {
        return col < fields.size();
    };

    if (!has_col(schema.x_col) ||
        !has_col(schema.y_col) ||
        !has_col(schema.z_col) ||
        !has_col(schema.primary_value_col)) {
        return false;
    }

    if (!parse_float(fields[schema.x_col], record.x)) {
        return false;
    }

    if (!parse_float(fields[schema.y_col], record.y)) {
        return false;
    }

    if (!parse_float(fields[schema.z_col], record.z)) {
        return false;
    }

    if (!parse_float(fields[schema.primary_value_col], record.primary_value)) {
        return false;
    }

    record.extra_values.clear();
    record.extra_values.reserve(schema.extra_attribute_cols.size());

    for (const auto col : schema.extra_attribute_cols) {
        if (!has_col(col)) {
            return false;
        }

        float value = 0.0f;
        if (!parse_float(fields[col], value)) {
            return false;
        }

        record.extra_values.push_back(value);
    }

    return true;
}

} // namespace gs3d::data