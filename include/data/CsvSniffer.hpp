#pragma once

#include "data/CsvStreamReader.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gs3d::data {

struct CsvSniffResult {
    bool has_header = true;
    DelimiterMode delimiter_mode = DelimiterMode::Auto;

    std::string header_line;
    std::vector<std::string> header_fields;
    ResolvedDataSchema resolved_schema;

    std::uint64_t header_begin_offset = 0;
    std::uint64_t header_end_offset = 0;

    bool uses_crlf = false;
};

class CsvSniffer {
public:
    explicit CsvSniffer(CsvReadConfig config = {});

    [[nodiscard]]
    CsvSniffResult sniff(
        const std::filesystem::path& path
    ) const;

private:
    CsvReadConfig config_;
};

} // namespace gs3d::data
