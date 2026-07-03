#pragma once

#include "data/DataSchema.hpp"
#include "data/Delimiter.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace gs3d::data {

struct CsvPointRecord {
    // Source coordinates stay double precision until the dataset origin is
    // subtracted.  Parsing large map coordinates directly to float loses
    // metre-scale detail before origin rebasing can preserve it.
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float primary_value = 0.0f;

    std::vector<float> extra_values;
};

struct CsvReadConfig {
    bool has_header = true;
    bool skip_empty_lines = true;
    bool allow_comment_lines = true;

    char comment_char = '#';

    DelimiterMode delimiter_mode = DelimiterMode::Auto;
    DataSchema schema = DataSchema::default_fold_elevation();
};

struct CsvReadStats {
    std::uint64_t total_lines = 0;
    std::uint64_t header_lines = 0;
    std::uint64_t data_lines = 0;
    std::uint64_t valid_records = 0;
    std::uint64_t invalid_records = 0;
};

class CsvStreamReader {
public:
    using RecordCallback =
        std::function<void(const CsvPointRecord& record, std::uint64_t line_number)>;

public:
    explicit CsvStreamReader(CsvReadConfig config = {});

    [[nodiscard]]
    CsvReadStats read(
        const std::filesystem::path& path,
        const RecordCallback& callback
    ) const;

private:
    CsvReadConfig config_;

private:
    [[nodiscard]]
    static bool is_blank_line(const std::string& line);

    [[nodiscard]]
    bool is_comment_line(const std::string& line) const;

    [[nodiscard]]
    static bool parse_float(std::string_view text, float& value);

    [[nodiscard]]
    static bool parse_double(std::string_view text, double& value);

    [[nodiscard]]
    static std::unordered_map<std::string, std::size_t> build_field_map(
        const std::vector<std::string>& header_fields,
        bool case_insensitive
    );

    [[nodiscard]]
    bool parse_record(
        const std::vector<std::string_view>& fields,
        const ResolvedDataSchema& schema,
        CsvPointRecord& record
    ) const;
};

} // namespace gs3d::data
