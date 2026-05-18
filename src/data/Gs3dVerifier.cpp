#include "data/Gs3dVerifier.hpp"

#include "data/Gs3dReader.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace gs3d::data {

Gs3dVerifier::Gs3dVerifier(
    CsvReadConfig csv_config,
    Gs3dVerifyConfig verify_config
)
    : csv_config_(std::move(csv_config))
    , verify_config_(verify_config)
{
}

Gs3dVerifyResult Gs3dVerifier::verify(
    const std::filesystem::path& csv_path,
    const std::filesystem::path& gs3d_path
) const {
    Gs3dVerifyResult result;

    const Gs3dHeader header = Gs3dReader::read_header(gs3d_path);

    if (!Gs3dReader::validate_file_size(gs3d_path)) {
        throw std::runtime_error(
            "Gs3dVerifier: GS3D file size does not match header"
        );
    }

    std::ifstream in(gs3d_path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error(
            "Gs3dVerifier: failed to open GS3D file: " + gs3d_path.string()
        );
    }

    in.seekg(
        static_cast<std::streamoff>(header.point_data_offset),
        std::ios::beg
    );

    if (!in.good()) {
        throw std::runtime_error("Gs3dVerifier: failed to seek point data");
    }

    CsvStreamReader csv_reader(csv_config_);

    const auto csv_stats = csv_reader.read(
        csv_path,
        [&](const CsvPointRecord& record, std::uint64_t) {
            if (result.checked_points >= header.point_count) {
                ++result.mismatch_points;

                if (!result.has_mismatch) {
                    result.has_mismatch = true;
                    result.first_mismatch_index = result.checked_points;
                }

                if (verify_config_.stop_on_first_mismatch) {
                    throw std::runtime_error(
                        "Gs3dVerifier: CSV has more valid points than GS3D"
                    );
                }

                return;
            }

            Gs3dPoint actual{};

            in.read(
                reinterpret_cast<char*>(&actual),
                static_cast<std::streamsize>(sizeof(Gs3dPoint))
            );

            if (!in.good()) {
                throw std::runtime_error(
                    "Gs3dVerifier: failed to read GS3D point data"
                );
            }

            const Gs3dPoint expected = make_expected_point(record, header);

            update_error_stats(result, expected, actual);

            if (is_mismatch(expected, actual)) {
                ++result.mismatch_points;

                if (!result.has_mismatch) {
                    result.has_mismatch = true;
                    result.first_mismatch_index = result.checked_points;
                }

                if (verify_config_.stop_on_first_mismatch) {
                    throw std::runtime_error(
                        "Gs3dVerifier: point data mismatch"
                    );
                }
            }

            ++result.checked_points;
        }
    );

    if (csv_stats.valid_records != header.point_count) {
        ++result.mismatch_points;

        if (!result.has_mismatch) {
            result.has_mismatch = true;
            result.first_mismatch_index = result.checked_points;
        }
    }

    if (result.checked_points != header.point_count) {
        ++result.mismatch_points;

        if (!result.has_mismatch) {
            result.has_mismatch = true;
            result.first_mismatch_index = result.checked_points;
        }
    }

    result.passed =
        result.mismatch_points == 0 &&
        result.checked_points == header.point_count &&
        csv_stats.valid_records == header.point_count;

    return result;
}

Gs3dPoint Gs3dVerifier::make_expected_point(
    const CsvPointRecord& record,
    const Gs3dHeader& header
) {
    Gs3dPoint point{};

    point.x = static_cast<float>(
        static_cast<double>(record.x) - header.origin_x
    );

    point.y = static_cast<float>(
        static_cast<double>(record.y) - header.origin_y
    );

    point.z = static_cast<float>(
        static_cast<double>(record.z) - header.origin_z
    );

    point.value = record.primary_value;

    return point;
}

void Gs3dVerifier::update_error_stats(
    Gs3dVerifyResult& result,
    const Gs3dPoint& expected,
    const Gs3dPoint& actual
) {
    const double dx = std::abs(
        static_cast<double>(expected.x) - static_cast<double>(actual.x)
    );

    const double dy = std::abs(
        static_cast<double>(expected.y) - static_cast<double>(actual.y)
    );

    const double dz = std::abs(
        static_cast<double>(expected.z) - static_cast<double>(actual.z)
    );

    const double dv = std::abs(
        static_cast<double>(expected.value) - static_cast<double>(actual.value)
    );

    result.max_x_error = std::max(result.max_x_error, dx);
    result.max_y_error = std::max(result.max_y_error, dy);
    result.max_z_error = std::max(result.max_z_error, dz);
    result.max_value_error = std::max(result.max_value_error, dv);

    const double position_error = std::max({dx, dy, dz});
    result.max_position_error = std::max(
        result.max_position_error,
        position_error
    );
}

bool Gs3dVerifier::is_mismatch(
    const Gs3dPoint& expected,
    const Gs3dPoint& actual
) const {
    const double dx = std::abs(
        static_cast<double>(expected.x) - static_cast<double>(actual.x)
    );

    const double dy = std::abs(
        static_cast<double>(expected.y) - static_cast<double>(actual.y)
    );

    const double dz = std::abs(
        static_cast<double>(expected.z) - static_cast<double>(actual.z)
    );

    const double dv = std::abs(
        static_cast<double>(expected.value) - static_cast<double>(actual.value)
    );

    return dx > verify_config_.position_epsilon ||
           dy > verify_config_.position_epsilon ||
           dz > verify_config_.position_epsilon ||
           dv > verify_config_.value_epsilon;
}

} // namespace gs3d::data