#include "data/Gs3dExporter.hpp"

#include "data/Gs3dReader.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <limits>
#include <stdexcept>
#include <string>

namespace gs3d::data {

namespace {

[[nodiscard]] std::string normalized_format(std::string format) {
    std::transform(
        format.begin(), format.end(), format.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return format;
}

void write_points(
    std::ostream& output,
    const std::vector<Gs3dPoint>& points,
    const std::string& format
) {
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    if (format == "ply") {
        output << "ply\n"
               << "format ascii 1.0\n"
               << "comment Exported by GeoScatter3D\n"
               << "element vertex " << points.size() << "\n"
               << "property float x\n"
               << "property float y\n"
               << "property float z\n"
               << "property float value\n"
               << "end_header\n";
        for (const auto& point : points) {
            output << point.x << ' ' << point.y << ' ' << point.z << ' '
                   << point.value << '\n';
        }
        return;
    }

    output << "x,y,z,value\n";
    for (const auto& point : points) {
        output << point.x << ',' << point.y << ',' << point.z << ','
               << point.value << '\n';
    }
}

} // namespace

Gs3dExportResult export_gs3d_points(
    const std::filesystem::path& source_path,
    const std::filesystem::path& target_path,
    const std::string& requested_format
) {
    Gs3dExportResult result;
    const std::string format = normalized_format(requested_format);
    if (format != "ply" && format != "csv") {
        result.error = "unsupported export format: " + requested_format;
        return result;
    }
    if (source_path.empty() || target_path.empty()) {
        result.error = "source and target paths are required";
        return result;
    }

    try {
        const auto source = Gs3dReader::read_all(source_path);
        if (source.points.size() != source.header.point_count) {
            result.error = "source point count does not match GS3D header";
            return result;
        }

        const auto parent = target_path.parent_path();
        if (!parent.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error) {
                result.error = "cannot create output directory: " +
                    error.message();
                return result;
            }
        }

        std::ofstream output(target_path, std::ios::out | std::ios::trunc);
        if (!output.is_open()) {
            result.error = "cannot open export target";
            return result;
        }
        write_points(output, source.points, format);
        output.flush();
        if (!output) {
            result.error = "failed while writing export target";
            return result;
        }

        result.success = true;
        result.point_count = source.header.point_count;
        return result;
    } catch (const std::exception& error) {
        result.error = error.what();
        return result;
    }
}

} // namespace gs3d::data
