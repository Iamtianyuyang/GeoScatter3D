#include "data/DataSchema.hpp"

#include "data/Delimiter.hpp"

#include <stdexcept>

namespace gs3d::data {

DataSchema DataSchema::default_fold_elevation() {
    DataSchema schema;
    schema.x_field = "x";
    schema.y_field = "y";
    schema.z_field = "elevation";
    schema.primary_value_field = "fold";
    schema.case_insensitive = true;
    schema.include_extra_attributes = true;
    return schema;
}

ResolvedDataSchema DataSchema::resolve(
    const std::unordered_map<std::string, std::size_t>& field_to_column,
    const std::vector<std::string>& header_fields
) const {
    auto find_required = [&](const std::string& field) -> std::size_t {
        const std::string key = normalize(field);
        const auto it = field_to_column.find(key);

        if (it == field_to_column.end()) {
            throw std::runtime_error(
                "DataSchema: required field not found: " + field
            );
        }

        return it->second;
    };

    ResolvedDataSchema resolved;

    resolved.x_col = find_required(x_field);
    resolved.y_col = find_required(y_field);
    resolved.z_col = find_required(z_field);
    resolved.primary_value_col = find_required(primary_value_field);

    std::vector<std::size_t> used_columns = {
        resolved.x_col,
        resolved.y_col,
        resolved.z_col,
        resolved.primary_value_col
    };

    if (!extra_attribute_fields.empty()) {
        for (const auto& field : extra_attribute_fields) {
            const std::string key = normalize(field);
            const auto it = field_to_column.find(key);

            if (it == field_to_column.end()) {
                throw std::runtime_error(
                    "DataSchema: extra attribute field not found: " + field
                );
            }

            if (!contains_column(used_columns, it->second)) {
                resolved.extra_attribute_names.push_back(field);
                resolved.extra_attribute_cols.push_back(it->second);
                used_columns.push_back(it->second);
            }
        }

        return resolved;
    }

    if (include_extra_attributes) {
        for (std::size_t col = 0; col < header_fields.size(); ++col) {
            if (contains_column(used_columns, col)) {
                continue;
            }

            resolved.extra_attribute_names.push_back(header_fields[col]);
            resolved.extra_attribute_cols.push_back(col);
        }
    }

    return resolved;
}

std::string DataSchema::normalize(std::string_view field_name) const {
    return Delimiter::normalize_field_name(field_name, case_insensitive);
}

bool DataSchema::contains_column(
    const std::vector<std::size_t>& columns,
    std::size_t column
) {
    for (const auto c : columns) {
        if (c == column) {
            return true;
        }
    }

    return false;
}

} // namespace gs3d::data