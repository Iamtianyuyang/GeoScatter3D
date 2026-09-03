#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gs3d::data {

struct StatisticsResult {
    std::uint64_t point_count = 0;
    std::uint64_t total_lines = 0;
    std::uint64_t data_lines = 0;
    std::uint64_t valid_records = 0;
    std::uint64_t invalid_records = 0;

    double xmin = 0.0;
    double xmax = 0.0;

    double ymin = 0.0;
    double ymax = 0.0;

    double zmin = 0.0;
    double zmax = 0.0;

    float value_min = 0.0f;
    float value_max = 0.0f;

    double origin_x = 0.0;
    double origin_y = 0.0;
    double origin_z = 0.0;

    bool empty = true;
};

struct ResolvedDataSchema {
    std::size_t x_col = 0;
    std::size_t y_col = 0;
    std::size_t z_col = 0;
    std::size_t primary_value_col = 0;

    std::vector<std::string> extra_attribute_names;
    std::vector<std::size_t> extra_attribute_cols;
};

class DataSchema {
public:
    std::string x_field = "x";
    std::string y_field = "y";
    std::string z_field = "elevation";
    std::string primary_value_field = "fold";

    bool case_insensitive = true;
    bool include_extra_attributes = true;

    std::vector<std::string> extra_attribute_fields;

public:
    static DataSchema default_fold_elevation();

    [[nodiscard]]
    ResolvedDataSchema resolve(
        const std::unordered_map<std::string, std::size_t>& field_to_column,
        const std::vector<std::string>& header_fields
    ) const;

private:
    [[nodiscard]]
    std::string normalize(std::string_view field_name) const;

    [[nodiscard]]
    static bool contains_column(
        const std::vector<std::size_t>& columns,
        std::size_t column
    );
};

} // namespace gs3d::data