#pragma once

#include "data/CsvStreamReader.hpp"
#include "data/Gs3dFormat.hpp"

#include <cstdint>
#include <filesystem>

namespace gs3d::data {

struct Gs3dVerifyConfig {
    double position_epsilon = 1e-4;
    double value_epsilon = 1e-5;
    bool stop_on_first_mismatch = false;
};

struct Gs3dVerifyResult {
    std::uint64_t checked_points = 0;
    std::uint64_t mismatch_points = 0;

    double max_x_error = 0.0;
    double max_y_error = 0.0;
    double max_z_error = 0.0;
    double max_position_error = 0.0;
    double max_value_error = 0.0;

    std::uint64_t first_mismatch_index = 0;
    bool has_mismatch = false;
    bool passed = false;
};

class Gs3dVerifier {
public:
    Gs3dVerifier(
        CsvReadConfig csv_config = {},
        Gs3dVerifyConfig verify_config = {}
    );

    [[nodiscard]]
    Gs3dVerifyResult verify(
        const std::filesystem::path& csv_path,
        const std::filesystem::path& gs3d_path
    ) const;

private:
    CsvReadConfig csv_config_;
    Gs3dVerifyConfig verify_config_;

private:
    [[nodiscard]]
    static Gs3dPoint make_expected_point(
        const CsvPointRecord& record,
        const Gs3dHeader& header
    );

    static void update_error_stats(
        Gs3dVerifyResult& result,
        const Gs3dPoint& expected,
        const Gs3dPoint& actual
    );

    [[nodiscard]]
    bool is_mismatch(
        const Gs3dPoint& expected,
        const Gs3dPoint& actual
    ) const;
};

} // namespace gs3d::data