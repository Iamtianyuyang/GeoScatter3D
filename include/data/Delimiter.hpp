#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gs3d::data {

enum class DelimiterMode {
    Auto,
    Comma,
    Tab,
    Whitespace
};

class Delimiter {
public:
    static DelimiterMode detect(std::string_view line);

    static std::vector<std::string_view> split(
        std::string_view line,
        DelimiterMode mode
    );

    static std::string trim_copy(std::string_view text);

    static std::string_view trim_view(std::string_view text);

    static std::string normalize_field_name(
        std::string_view text,
        bool case_insensitive = true
    );

private:
    static std::vector<std::string_view> split_by_char(
        std::string_view line,
        char delimiter
    );

    static std::vector<std::string_view> split_by_whitespace(
        std::string_view line
    );
};

} // namespace gs3d::data