#include "data/Delimiter.hpp"

#include <algorithm>
#include <cctype>

namespace gs3d::data {

DelimiterMode Delimiter::detect(std::string_view line) {
    const auto comma_count = std::count(line.begin(), line.end(), ',');
    const auto tab_count = std::count(line.begin(), line.end(), '\t');

    if (comma_count > 0) {
        return DelimiterMode::Comma;
    }

    if (tab_count > 0) {
        return DelimiterMode::Tab;
    }

    return DelimiterMode::Whitespace;
}

std::vector<std::string_view> Delimiter::split(
    std::string_view line,
    DelimiterMode mode
) {
    if (mode == DelimiterMode::Auto) {
        mode = detect(line);
    }

    switch (mode) {
    case DelimiterMode::Comma:
        return split_by_char(line, ',');
    case DelimiterMode::Tab:
        return split_by_char(line, '\t');
    case DelimiterMode::Whitespace:
        return split_by_whitespace(line);
    case DelimiterMode::Auto:
    default:
        return split_by_whitespace(line);
    }
}

std::string Delimiter::trim_copy(std::string_view text) {
    const auto trimmed = trim_view(text);
    return std::string(trimmed);
}

std::string Delimiter::normalize_field_name(
    std::string_view text,
    bool case_insensitive
) {
    std::string result = trim_copy(text);

    if (case_insensitive) {
        std::transform(
            result.begin(),
            result.end(),
            result.begin(),
            [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            }
        );
    }

    return result;
}

std::vector<std::string_view> Delimiter::split_by_char(
    std::string_view line,
    char delimiter
) {
    std::vector<std::string_view> fields;

    std::size_t start = 0;

    while (start <= line.size()) {
        const std::size_t pos = line.find(delimiter, start);

        if (pos == std::string_view::npos) {
            fields.push_back(trim_view(line.substr(start)));
            break;
        }

        fields.push_back(trim_view(line.substr(start, pos - start)));
        start = pos + 1;
    }

    return fields;
}

std::vector<std::string_view> Delimiter::split_by_whitespace(
    std::string_view line
) {
    std::vector<std::string_view> fields;

    std::size_t i = 0;

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

        fields.push_back(line.substr(start, i - start));
    }

    return fields;
}

std::string_view Delimiter::trim_view(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();

    while (begin < end &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }

    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(begin, end - begin);
}

} // namespace gs3d::data