#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace gs3d::data {

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