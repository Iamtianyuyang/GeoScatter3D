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
    auto find_with_fallback = [&](const std::string& field, const std::vector<std::string>& aliases, std::size_t fallback_pos) -> std::size_t {
        const std::string key = normalize(field);
        const auto it = field_to_column.find(key);
        if (it != field_to_column.end()) {
            return it->second;
        }
        for (const auto& alias : aliases) {
            const auto it_alias = field_to_column.find(normalize(alias));
            if (it_alias != field_to_column.end()) {
                return it_alias->second;
            }
        }
        if (fallback_pos < header_fields.size()) {
            return fallback_pos;
        }
        throw std::runtime_error(
            "DataSchema: required field not found: " + field
        );
    };

    ResolvedDataSchema resolved;

    // X and Y are required by name or aliases, with position fallback
    resolved.x_col = find_with_fallback(x_field, {"x", "easting", "east", "lon", "longitude", "x_coord"}, 0);
    resolved.y_col = find_with_fallback(y_field, {"y", "northing", "north", "lat", "latitude", "y_coord"}, 1);

    // Z and primary value: try the configured name first.
    // If not found, fall back by position — column 2 (third) for Z,
    // then the first unused column for the value.  This handles .dat
    // files whose 3rd/4th column names vary (elevation / height / z,
    // field_statics / fold / value / ...) without manual config.
    {
        const std::string z_key = normalize(z_field);
        const auto z_it = field_to_column.find(z_key);
        resolved.z_col = (z_it != field_to_column.end())
            ? z_it->second
            : (header_fields.size() > 2 ? std::size_t{2} : std::size_t{0});
    }

    {
        std::size_t primary_col = resolved.z_col;
        const std::string pv_key = normalize(primary_value_field);
        const auto pv_it = field_to_column.find(pv_key);
        if (pv_it != field_to_column.end()) {
            primary_col = pv_it->second;
        } else {
            // First column that isn't x, y, or z.
            for (std::size_t col = 0;
                 col < header_fields.size(); ++col) {
                if (col != resolved.x_col &&
                    col != resolved.y_col &&
                    col != resolved.z_col) {
                    primary_col = col;
                    break;
                }
            }
        }
        resolved.primary_value_col = primary_col;
    }

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