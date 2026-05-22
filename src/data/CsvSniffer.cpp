#include "data/CsvSniffer.hpp"

#include "data/Delimiter.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

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

std::unordered_map<std::string, std::size_t> build_field_map(
    const std::vector<std::string>& header_fields,
    bool case_insensitive
) {
    std::unordered_map<std::string, std::size_t> field_map;

    for (std::size_t i = 0; i < header_fields.size(); ++i) {
        const auto key = Delimiter::normalize_field_name(
            header_fields[i],
            case_insensitive
        );

        if (key.empty()) {
            throw std::runtime_error(
                "CsvSniffer: empty field name in header"
            );
        }

        if (field_map.contains(key)) {
            throw std::runtime_error(
                "CsvSniffer: duplicated field name in header: " + key
            );
        }

        field_map.emplace(key, i);
    }

    return field_map;
}

} // namespace

CsvSniffer::CsvSniffer(CsvReadConfig config)
    : config_(std::move(config))
{
}

CsvSniffResult CsvSniffer::sniff(
    const std::filesystem::path& path
) const {
    if (!config_.has_header) {
        throw std::runtime_error(
            "CsvSniffer: current version requires a header line"
        );
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            "CsvSniffer: failed to open file: " + path.string()
        );
    }

    CsvSniffResult result;
    result.has_header = config_.has_header;

    std::string line;
    while (true) {
        const auto begin_offset = static_cast<std::uint64_t>(file.tellg());
        if (!std::getline(file, line)) {
            break;
        }

        const auto end_stream_pos = file.tellg();
        const auto end_offset = end_stream_pos >= 0
            ? static_cast<std::uint64_t>(end_stream_pos)
            : static_cast<std::uint64_t>(std::filesystem::file_size(path));

        if (config_.skip_empty_lines && is_blank_line(line)) {
            continue;
        }

        if (config_.allow_comment_lines && is_comment_line(line, config_)) {
            continue;
        }

        result.header_begin_offset = begin_offset;
        result.header_end_offset = end_offset;
        result.header_line = line;
        break;
    }

    if (result.header_line.empty()) {
        throw std::runtime_error("CsvSniffer: header line not found");
    }

    result.uses_crlf = !result.header_line.empty() &&
                       result.header_line.back() == '\r';

    result.delimiter_mode = config_.delimiter_mode;
    if (result.delimiter_mode == DelimiterMode::Auto) {
        result.delimiter_mode = Delimiter::detect(result.header_line);
    }

    const auto header_views =
        Delimiter::split(result.header_line, result.delimiter_mode);
    result.header_fields.reserve(header_views.size());
    for (const auto field : header_views) {
        result.header_fields.push_back(Delimiter::trim_copy(field));
    }

    const auto field_map = build_field_map(
        result.header_fields,
        config_.schema.case_insensitive
    );
    result.resolved_schema = config_.schema.resolve(
        field_map,
        result.header_fields
    );

    return result;
}

} // namespace gs3d::data
