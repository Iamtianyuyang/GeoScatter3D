#include "data/CsvSniffer.hpp"
#include "data/CsvStreamReader.hpp"
#include "data/Gs3dFormat.hpp"
#include "data/Gs3dReader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ── CLI options ───────────────────────────────────────────────────────────

struct CsvFieldOverrides {
    std::string x_field;
    std::string y_field;
    std::string z_field;
    std::string value_field;
};

struct Opts {
    std::string_view cmd;
    std::filesystem::path data_path;
    std::filesystem::path gs3d_ref_path;     // --gs3d: read origin from here
    CsvFieldOverrides fields;

    // nearest
    float nearest_cx = 0.0f;
    float nearest_cy = 0.0f;
    float nearest_radius = 0.0f;

    // region
    float region_x_min = 0.0f;
    float region_x_max = 0.0f;
    float region_y_min = 0.0f;
    float region_y_max = 0.0f;
    bool region_list = false;
};

// ── helpers ───────────────────────────────────────────────────────────────

bool has_gs3d_extension(const std::filesystem::path& path) {
    const auto ext = path.extension().string();
    return ext == ".gs3d" || ext == ".GS3D";
}

gs3d::data::Gs3dHeader read_origin_from_gs3d(const std::filesystem::path& path) {
    return gs3d::data::Gs3dReader::read_header(path);
}

gs3d::data::CsvReadConfig make_csv_config(const CsvFieldOverrides& overrides) {
    gs3d::data::CsvReadConfig cfg;
    cfg.has_header = true;
    cfg.skip_empty_lines = true;
    cfg.allow_comment_lines = true;
    cfg.comment_char = '#';
    cfg.delimiter_mode = gs3d::data::DelimiterMode::Auto;
    if (!overrides.x_field.empty()) cfg.schema.x_field = overrides.x_field;
    if (!overrides.y_field.empty()) cfg.schema.y_field = overrides.y_field;
    if (!overrides.z_field.empty()) cfg.schema.z_field = overrides.z_field;
    if (!overrides.value_field.empty()) cfg.schema.primary_value_field = overrides.value_field;
    return cfg;
}

void print_schema_info(const gs3d::data::CsvReadConfig& cfg, const std::filesystem::path& path) {
    std::cout << "CSV schema: x=" << cfg.schema.x_field
              << " y=" << cfg.schema.y_field
              << " z=" << cfg.schema.z_field
              << " value=" << cfg.schema.primary_value_field << "\n";
    std::cout << "source: " << path.filename().string() << " (text)\n\n";
}

// ── PointStats (shared between GS3D and CSV paths) ─────────────────────────

struct PointStats {
    double x_min = std::numeric_limits<double>::max();
    double x_max = std::numeric_limits<double>::lowest();
    double y_min = std::numeric_limits<double>::max();
    double y_max = std::numeric_limits<double>::lowest();
    double z_min = std::numeric_limits<double>::max();
    double z_max = std::numeric_limits<double>::lowest();
    double value_min = std::numeric_limits<double>::max();
    double value_max = std::numeric_limits<double>::lowest();

    double x_sum = 0.0;
    double y_sum = 0.0;
    double z_sum = 0.0;
    double value_sum = 0.0;

    std::uint64_t count = 0;

    void accumulate(double x, double y, double z, double v) {
        x_min = std::min(x_min, x);
        x_max = std::max(x_max, x);
        y_min = std::min(y_min, y);
        y_max = std::max(y_max, y);
        z_min = std::min(z_min, z);
        z_max = std::max(z_max, z);
        value_min = std::min(value_min, v);
        value_max = std::max(value_max, v);

        x_sum += x;
        y_sum += y;
        z_sum += z;
        value_sum += v;

        ++count;
    }

    void print(const char* label, double offset_x = 0.0,
               double offset_y = 0.0, double offset_z = 0.0) const {
        if (count == 0) {
            std::cout << "  (no points)\n";
            return;
        }
        const double n = static_cast<double>(count);
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "--- " << label << " ---\n";
        std::cout << "  point_count: " << count << "\n";
        std::cout << "  x:     min=" << (x_min + offset_x)
                  << "  max=" << (x_max + offset_x)
                  << "  mean=" << (x_sum / n + offset_x) << "\n";
        std::cout << "  y:     min=" << (y_min + offset_y)
                  << "  max=" << (y_max + offset_y)
                  << "  mean=" << (y_sum / n + offset_y) << "\n";
        std::cout << "  z:     min=" << (z_min + offset_z)
                  << "  max=" << (z_max + offset_z)
                  << "  mean=" << (z_sum / n + offset_z) << "\n";
        // value has no offset — it's passthrough in both GS3D and CSV
        std::cout << "  value: min=" << value_min
                  << "  max=" << value_max
                  << "  mean=" << (value_sum / n) << "\n";
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  INFO
// ═══════════════════════════════════════════════════════════════════════════

void cmd_info_gs3d(const Opts& opts) {
    auto result = gs3d::data::Gs3dReader::read_all(opts.data_path);
    const auto& header = result.header;
    const auto& points = result.points;

    std::cout << "=== GS3D Ground Truth: " << opts.data_path.filename().string() << " ===\n\n";

    const bool have_origin = !opts.gs3d_ref_path.empty();
    double origin_x = header.origin_x;
    double origin_y = header.origin_y;
    double origin_z = header.origin_z;

    if (have_origin) {
        // Read origin from the reference GS3D for cross-check
        const auto ref = read_origin_from_gs3d(opts.gs3d_ref_path);
        origin_x = ref.origin_x;
        origin_y = ref.origin_y;
        origin_z = ref.origin_z;
        std::cout << "Origin (from --gs3d): ("
                  << origin_x << ", " << origin_y << ", " << origin_z << ")\n\n";
    } else {
        std::cout << "Origin (from header): ("
                  << origin_x << ", " << origin_y << ", " << origin_z << ")\n\n";
    }

    // Scan: GS3D points are origin-relative
    PointStats rel_stats;
    PointStats abs_stats;  // reconstructed absolute
    for (const auto& p : points) {
        const double rx = static_cast<double>(p.x);
        const double ry = static_cast<double>(p.y);
        const double rz = static_cast<double>(p.z);
        const double rv = static_cast<double>(p.value);
        rel_stats.accumulate(rx, ry, rz, rv);
        abs_stats.accumulate(rx + origin_x, ry + origin_y, rz + origin_z, rv);
    }

    std::cout << "Header bbox (relative): ["
              << header.bbox_min_x << ", " << header.bbox_max_x << "] x ["
              << header.bbox_min_y << ", " << header.bbox_max_y << "] x ["
              << header.bbox_min_z << ", " << header.bbox_max_z << "]\n";
    std::cout << "Header origin: (" << header.origin_x << ", "
              << header.origin_y << ", " << header.origin_z << ")\n";
    std::cout << "Header value_range: ["
              << header.value_min << ", " << header.value_max << "]\n\n";

    rel_stats.print("Relative coords (GS3D stored, = absolute - origin)");
    std::cout << "\n";
    abs_stats.print("Absolute coords (reconstructed, = GS3D point + origin)");
}

void cmd_info_csv(const Opts& opts) {
    const auto cfg = make_csv_config(opts.fields);
    print_schema_info(cfg, opts.data_path);

    std::cout << "=== CSV/DAT Ground Truth: " << opts.data_path.filename().string() << " ===\n\n";

    double origin_x = 0.0, origin_y = 0.0, origin_z = 0.0;
    const bool have_origin = !opts.gs3d_ref_path.empty();
    if (have_origin) {
        const auto ref = read_origin_from_gs3d(opts.gs3d_ref_path);
        origin_x = ref.origin_x;
        origin_y = ref.origin_y;
        origin_z = ref.origin_z;
        std::cout << "Origin (from --gs3d): ("
                  << origin_x << ", " << origin_y << ", " << origin_z << ")\n\n";
    }

    gs3d::data::CsvStreamReader reader(cfg);
    PointStats abs_stats;

    const auto stats = reader.read(
        opts.data_path,
        [&](const gs3d::data::CsvPointRecord& rec, std::uint64_t) {
            abs_stats.accumulate(rec.x, rec.y, rec.z,
                                 static_cast<double>(rec.primary_value));
        }
    );

    std::cout << "CSV read: " << stats.valid_records << " valid records"
              << " (skipped " << stats.invalid_records << ")\n\n";

    abs_stats.print("Absolute coords (direct from CSV/DAT)");

    if (have_origin) {
        std::cout << "\n";
        // offset = -origin converts absolute → relative
        abs_stats.print("Relative coords (absolute - origin, for GS3D comparison)",
                        -origin_x, -origin_y, -origin_z);
    }
}

void cmd_info(const Opts& opts) {
    if (has_gs3d_extension(opts.data_path)) {
        cmd_info_gs3d(opts);
    } else {
        cmd_info_csv(opts);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  NEAREST
// ═══════════════════════════════════════════════════════════════════════════

void cmd_nearest_gs3d(const Opts& opts) {
    auto result = gs3d::data::Gs3dReader::read_all(opts.data_path);
    const auto& header = result.header;
    const auto& points = result.points;

    double origin_x = header.origin_x;
    double origin_y = header.origin_y;
    double origin_z = header.origin_z;
    if (!opts.gs3d_ref_path.empty()) {
        const auto ref = read_origin_from_gs3d(opts.gs3d_ref_path);
        origin_x = ref.origin_x;
        origin_y = ref.origin_y;
        origin_z = ref.origin_z;
    }

    // Query coords are absolute; GS3D points are relative.
    // Convert query to relative for comparison: rel_q = abs_q - origin
    const double qx = static_cast<double>(opts.nearest_cx) - origin_x;
    const double qy = static_cast<double>(opts.nearest_cy) - origin_y;
    const double r2 = static_cast<double>(opts.nearest_radius) * static_cast<double>(opts.nearest_radius);

    double best_dist2 = r2;
    const gs3d::data::Gs3dPoint* best = nullptr;

    for (const auto& p : points) {
        const double dx = static_cast<double>(p.x) - qx;
        const double dy = static_cast<double>(p.y) - qy;
        const double d2 = dx * dx + dy * dy;
        if (d2 < best_dist2) {
            best_dist2 = d2;
            best = &p;
        }
    }

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Query: (" << opts.nearest_cx << ", " << opts.nearest_cy
              << ") radius=" << opts.nearest_radius << "\n";
    if (!opts.gs3d_ref_path.empty()) {
        std::cout << "Origin (from --gs3d): ("
                  << origin_x << ", " << origin_y << ", " << origin_z << ")\n";
    }

    if (best == nullptr) {
        std::cout << "Result: no point within radius\n";
        return;
    }

    const double abs_x = static_cast<double>(best->x) + origin_x;
    const double abs_y = static_cast<double>(best->y) + origin_y;
    const double abs_z = static_cast<double>(best->z) + origin_z;

    std::cout << "Result:\n";
    std::cout << "  ABSOLUTE  x=" << abs_x << "  y=" << abs_y
              << "  z=" << abs_z << "  value=" << best->value << "\n";
    std::cout << "  RELATIVE  x=" << best->x << "  y=" << best->y
              << "  z=" << best->z << "  value=" << best->value << "\n";
    std::cout << "  distance=" << std::sqrt(best_dist2) << "\n";
}

void cmd_nearest_csv(const Opts& opts) {
    const auto cfg = make_csv_config(opts.fields);
    print_schema_info(cfg, opts.data_path);

    double origin_x = 0.0, origin_y = 0.0, origin_z = 0.0;
    const bool have_origin = !opts.gs3d_ref_path.empty();
    if (have_origin) {
        const auto ref = read_origin_from_gs3d(opts.gs3d_ref_path);
        origin_x = ref.origin_x;
        origin_y = ref.origin_y;
        origin_z = ref.origin_z;
        std::cout << "Origin (from --gs3d): ("
                  << origin_x << ", " << origin_y << ", " << origin_z << ")\n";
    }

    // ponytail: double throughout — float has ULP ~4 at 4.5e7 coords
    const double cx = static_cast<double>(opts.nearest_cx);
    const double cy = static_cast<double>(opts.nearest_cy);
    const double r2 = static_cast<double>(opts.nearest_radius) * static_cast<double>(opts.nearest_radius);

    gs3d::data::CsvStreamReader reader(cfg);
    double best_dist2 = r2;
    bool found = false;
    double best_x = 0, best_y = 0, best_z = 0;
    float best_value = 0;

    std::ignore = reader.read(
        opts.data_path,
        [&](const gs3d::data::CsvPointRecord& rec, std::uint64_t) {
            const double dx = rec.x - cx;
            const double dy = rec.y - cy;
            const double d2 = dx * dx + dy * dy;
            if (d2 < best_dist2) {
                best_dist2 = d2;
                best_x = rec.x;
                best_y = rec.y;
                best_z = rec.z;
                best_value = rec.primary_value;
                found = true;
            }
        }
    );

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Query: (" << cx << ", " << cy
              << ") radius=" << opts.nearest_radius << " (absolute coords)\n";

    if (!found) {
        std::cout << "Result: no point within radius\n";
        return;
    }

    std::cout << "Result:\n";
    std::cout << "  ABSOLUTE  x=" << best_x << "  y=" << best_y
              << "  z=" << best_z << "  value=" << best_value << "\n";
    if (have_origin) {
        std::cout << "  RELATIVE  x=" << (best_x - origin_x)
                  << "  y=" << (best_y - origin_y)
                  << "  z=" << (best_z - origin_z)
                  << "  value=" << best_value << "\n";
    }
    std::cout << "  distance=" << std::sqrt(best_dist2) << "\n";
}

void cmd_nearest(const Opts& opts) {
    if (has_gs3d_extension(opts.data_path)) {
        cmd_nearest_gs3d(opts);
    } else {
        cmd_nearest_csv(opts);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  REGION
// ═══════════════════════════════════════════════════════════════════════════

void cmd_region_gs3d(const Opts& opts) {
    auto result = gs3d::data::Gs3dReader::read_all(opts.data_path);
    const auto& header = result.header;
    const auto& points = result.points;

    double origin_x = header.origin_x;
    double origin_y = header.origin_y;
    double origin_z = header.origin_z;
    if (!opts.gs3d_ref_path.empty()) {
        const auto ref = read_origin_from_gs3d(opts.gs3d_ref_path);
        origin_x = ref.origin_x;
        origin_y = ref.origin_y;
        origin_z = ref.origin_z;
    }

    // Query bbox is absolute; convert to relative for comparison
    const double rx_min = static_cast<double>(opts.region_x_min) - origin_x;
    const double rx_max = static_cast<double>(opts.region_x_max) - origin_x;
    const double ry_min = static_cast<double>(opts.region_y_min) - origin_y;
    const double ry_max = static_cast<double>(opts.region_y_max) - origin_y;

    std::uint64_t count = 0;

    if (opts.region_list) {
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "# abs_x abs_y abs_z value  rel_x rel_y rel_z\n";
    }

    for (const auto& p : points) {
        if (static_cast<double>(p.x) >= rx_min &&
            static_cast<double>(p.x) <= rx_max &&
            static_cast<double>(p.y) >= ry_min &&
            static_cast<double>(p.y) <= ry_max) {
            ++count;
            if (opts.region_list) {
                const double ax = static_cast<double>(p.x) + origin_x;
                const double ay = static_cast<double>(p.y) + origin_y;
                const double az = static_cast<double>(p.z) + origin_z;
                std::cout << ax << " " << ay << " " << az << " " << p.value
                          << "  " << p.x << " " << p.y << " " << p.z << "\n";
            }
        }
    }

    std::cout << "region [" << opts.region_x_min << ", " << opts.region_x_max
              << "] x [" << opts.region_y_min << ", " << opts.region_y_max
              << "] (absolute): " << count << " points\n";
}

void cmd_region_csv(const Opts& opts) {
    const auto cfg = make_csv_config(opts.fields);
    print_schema_info(cfg, opts.data_path);

    double origin_x = 0.0, origin_y = 0.0, origin_z = 0.0;
    const bool have_origin = !opts.gs3d_ref_path.empty();
    if (have_origin) {
        const auto ref = read_origin_from_gs3d(opts.gs3d_ref_path);
        origin_x = ref.origin_x;
        origin_y = ref.origin_y;
        origin_z = ref.origin_z;
        std::cout << "Origin (from --gs3d): ("
                  << origin_x << ", " << origin_y << ", " << origin_z << ")\n";
    }

    // ponytail: double — float can't distinguish 2m at 4.5e7 coords
    const double rx_min = static_cast<double>(opts.region_x_min);
    const double rx_max = static_cast<double>(opts.region_x_max);
    const double ry_min = static_cast<double>(opts.region_y_min);
    const double ry_max = static_cast<double>(opts.region_y_max);

    gs3d::data::CsvStreamReader reader(cfg);
    std::uint64_t count = 0;

    if (opts.region_list) {
        std::cout << std::fixed << std::setprecision(6);
        if (have_origin) {
            std::cout << "# abs_x abs_y abs_z value  rel_x rel_y rel_z\n";
        } else {
            std::cout << "# x y z value\n";
        }
    }

    std::ignore = reader.read(
        opts.data_path,
        [&](const gs3d::data::CsvPointRecord& rec, std::uint64_t) {
            if (rec.x >= rx_min && rec.x <= rx_max &&
                rec.y >= ry_min && rec.y <= ry_max) {
                ++count;
                if (opts.region_list) {
                    if (have_origin) {
                        std::cout << rec.x << " " << rec.y << " " << rec.z
                                  << " " << rec.primary_value
                                  << "  " << (rec.x - origin_x)
                                  << " " << (rec.y - origin_y)
                                  << " " << (rec.z - origin_z) << "\n";
                    } else {
                        std::cout << rec.x << " " << rec.y << " "
                                  << rec.z << " " << rec.primary_value << "\n";
                    }
                }
            }
        }
    );

    std::cout << "region [" << rx_min << ", " << rx_max
              << "] x [" << ry_min << ", " << ry_max
              << "] (absolute coords): " << count << " points\n";
}

void cmd_region(const Opts& opts) {
    if (has_gs3d_extension(opts.data_path)) {
        cmd_region_gs3d(opts);
    } else {
        cmd_region_csv(opts);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  CLI parsing
// ═══════════════════════════════════════════════════════════════════════════

void print_usage(const char* prog) {
    std::cout << "Usage:\n"
              << "  " << prog << " info    <file> [--gs3d <ref.gs3d>]\n"
              << "          [--x-field <name>] [--y-field <name>]\n"
              << "          [--z-field <name>] [--value-field <name>]\n"
              << "\n"
              << "  " << prog << " nearest <file> <x> <y> <radius> [--gs3d <ref.gs3d>]\n"
              << "          [--x-field <name>] [--y-field <name>]\n"
              << "          [--z-field <name>] [--value-field <name>]\n"
              << "\n"
              << "  " << prog << " region  <file> <x_min> <x_max> <y_min> <y_max> [--list]\n"
              << "          [--gs3d <ref.gs3d>]\n"
              << "          [--x-field <name>] [--y-field <name>]\n"
              << "          [--z-field <name>] [--value-field <name>]\n"
              << "\n"
              << "File type: .gs3d extension → binary GS3D; otherwise → text CSV/DAT.\n"
              << "Default CSV schema: x/y/elevation/fold.\n"
              << "--gs3d: read origin from a reference .gs3d header for absolute↔relative alignment.\n";
}

bool parse_field_override(const std::string_view& arg, const std::string_view& flag,
                          std::string& out, int& i, int argc, char* argv[]) {
    if (arg == flag) {
        if (i + 1 >= argc) return false;
        out = argv[++i];
        return true;
    }
    return false;
}

// ponytail: manual arg parse, no getopt dependency for 4 optional flags
Opts parse_opts(int argc, char* argv[]) {
    Opts opts;

    if (argc < 3) {
        print_usage(argv[0]);
        std::exit(1);
    }

    opts.cmd = argv[1];
    opts.data_path = argv[2];

    int i = 3;

    // Parse positional args for nearest/region before flags
    if (opts.cmd == "nearest" && argc >= 6) {
        opts.nearest_cx = std::strtof(argv[3], nullptr);
        opts.nearest_cy = std::strtof(argv[4], nullptr);
        opts.nearest_radius = std::strtof(argv[5], nullptr);
        i = 6;
    } else if (opts.cmd == "region" && argc >= 7) {
        opts.region_x_min = std::strtof(argv[3], nullptr);
        opts.region_x_max = std::strtof(argv[4], nullptr);
        opts.region_y_min = std::strtof(argv[5], nullptr);
        opts.region_y_max = std::strtof(argv[6], nullptr);
        i = 7;
    }

    for (; i < argc; ++i) {
        const std::string_view arg(argv[i]);

        if (arg == "--list") {
            opts.region_list = true;
        } else if (parse_field_override(arg, "--x-field", opts.fields.x_field, i, argc, argv)) {
            // parsed in helper
        } else if (parse_field_override(arg, "--y-field", opts.fields.y_field, i, argc, argv)) {
            // parsed in helper
        } else if (parse_field_override(arg, "--z-field", opts.fields.z_field, i, argc, argv)) {
            // parsed in helper
        } else if (parse_field_override(arg, "--value-field", opts.fields.value_field, i, argc, argv)) {
            // parsed in helper
        } else if (arg == "--gs3d") {
            if (i + 1 >= argc) {
                std::cerr << "error: --gs3d requires a path argument\n";
                std::exit(1);
            }
            opts.gs3d_ref_path = argv[++i];
        } else {
            std::cerr << "error: unknown flag: " << arg << "\n";
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    return opts;
}

} // namespace

int main(int argc, char* argv[]) {
    const auto opts = parse_opts(argc, argv);

    try {
        if (opts.cmd == "info") {
            cmd_info(opts);
        } else if (opts.cmd == "nearest") {
            cmd_nearest(opts);
        } else if (opts.cmd == "region") {
            cmd_region(opts);
        } else {
            print_usage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
