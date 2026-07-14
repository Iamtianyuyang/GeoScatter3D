#pragma once

#include <toml++/toml.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace gs3d::app::detail {

[[nodiscard]] inline std::filesystem::path path_or_default(
    const toml::table& table,
    const std::string_view key,
    const std::filesystem::path& default_value
) {
    if (const auto value = table[key].value<std::string>()) {
        return std::filesystem::path(*value);
    }
    return default_value;
}

[[nodiscard]] inline std::string string_or_default(
    const toml::table& table,
    const std::string_view key,
    const std::string& default_value
) {
    if (const auto value = table[key].value<std::string>()) {
        return *value;
    }
    return default_value;
}

[[nodiscard]] inline bool bool_or_default(
    const toml::table& table,
    const std::string_view key,
    const bool default_value
) {
    if (const auto value = table[key].value<bool>()) {
        return *value;
    }
    return default_value;
}

[[nodiscard]] inline unsigned int uint_or_default(
    const toml::table& table,
    const std::string_view key,
    const unsigned int default_value
) {
    if (const auto value = table[key].value<std::int64_t>()) {
        if (*value < 0) {
            throw std::runtime_error(
                "AppConfig: unsigned integer field is negative"
            );
        }
        return static_cast<unsigned int>(*value);
    }
    return default_value;
}

[[nodiscard]] inline std::uint64_t uint64_or_default(
    const toml::table& table,
    const std::string_view key,
    const std::uint64_t default_value
) {
    if (const auto value = table[key].value<std::int64_t>()) {
        if (*value < 0) {
            throw std::runtime_error(
                "AppConfig: uint64 field is negative"
            );
        }
        return static_cast<std::uint64_t>(*value);
    }
    return default_value;
}

[[nodiscard]] inline float float_or_default(
    const toml::table& table,
    const std::string_view key,
    const float default_value
) {
    if (const auto value = table[key].value<double>()) {
        return static_cast<float>(*value);
    }
    if (const auto value = table[key].value<std::int64_t>()) {
        return static_cast<float>(*value);
    }
    return default_value;
}

template <std::size_t Count>
[[nodiscard]] inline std::array<float, Count> float_array_or_default(
    const toml::table& table,
    const std::string_view key,
    const std::array<float, Count>& default_value,
    const char* const size_error,
    const char* const element_error,
    const char* const value_error
) {
    const auto* array = table[key].as_array();
    if (!array) {
        return default_value;
    }
    if (array->size() != Count) {
        throw std::runtime_error(size_error);
    }

    std::array<float, Count> result{};
    for (std::size_t index = 0; index < Count; ++index) {
        const auto node = array->get(index);
        if (!node) {
            throw std::runtime_error(element_error);
        }
        if (const auto value = node->value<double>()) {
            result[index] = static_cast<float>(*value);
        } else if (const auto value = node->value<std::int64_t>()) {
            result[index] = static_cast<float>(*value);
        } else {
            throw std::runtime_error(value_error);
        }
    }
    return result;
}

[[nodiscard]] inline std::array<float, 3> float3_or_default(
    const toml::table& table,
    const std::string_view key,
    const std::array<float, 3>& default_value
) {
    return float_array_or_default(
        table,
        key,
        default_value,
        "AppConfig: expected array with 3 values",
        "AppConfig: invalid float3 array element",
        "AppConfig: float3 array contains non-numeric value"
    );
}

[[nodiscard]] inline std::array<float, 4> float4_or_default(
    const toml::table& table,
    const std::string_view key,
    const std::array<float, 4>& default_value
) {
    return float_array_or_default(
        table,
        key,
        default_value,
        "AppConfig: expected array with 4 values",
        "AppConfig: invalid float4 array element",
        "AppConfig: float4 array contains non-numeric value"
    );
}

} // namespace gs3d::app::detail
