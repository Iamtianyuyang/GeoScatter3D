#pragma once

#include <cstdint>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace gs3d::data {

inline constexpr char GS3D_MAGIC[4] = {'G', 'S', '3', 'D'};
inline constexpr std::uint32_t GS3D_LEGACY_VERSION = 1;
inline constexpr std::uint32_t GS3D_VERSION = 2;
inline constexpr std::uint32_t GS3D_HEADER_V2_SIZE = 108;
inline constexpr std::uint32_t GS3D_POINT_SIZE = 16;

struct Gs3dHeader {
    char magic[4];

    std::uint32_t version;
    std::uint32_t header_size;

    std::uint64_t point_count;
    std::uint64_t point_data_offset;

    double origin_x;
    double origin_y;
    double origin_z;

    float bbox_min_x;
    float bbox_min_y;
    float bbox_min_z;

    float bbox_max_x;
    float bbox_max_y;
    float bbox_max_z;

    float value_min;
    float value_max;

    std::uint32_t flags;
    std::uint32_t reserved0;

    std::uint64_t reserved1;
    std::uint64_t reserved2;
};

struct Gs3dPoint {
    float x;
    float y;
    float z;
    float value;
};

static_assert(sizeof(Gs3dPoint) == 16, "Gs3dPoint must be 16 bytes");

/*
 * Gs3dPointWithId — v2 tile data format.
 *
 * Extends Gs3dPoint with the original global 1-based point_id from the
 * source GS3D file.  point_id == 0 is reserved (invalid / no hit).
 *
 * Stored interleaved in tile data files (20 bytes per point) so that a
 * single sequential read delivers both geometry and identity.
 */
struct Gs3dPointWithId {
    float x;
    float y;
    float z;
    float value;
    std::uint32_t point_id;
};

static_assert(sizeof(Gs3dPointWithId) == 20, "Gs3dPointWithId must be 20 bytes");

class Gs3dFormat {
public:
    static Gs3dHeader create_empty_header();

    static bool has_valid_magic(const Gs3dHeader& header);

    static bool has_supported_version(const Gs3dHeader& header);

    static bool is_valid_header(const Gs3dHeader& header);

    static std::string describe_header_error(const Gs3dHeader& header);

    static std::uint64_t expected_file_size(const Gs3dHeader& header);

    // Version 2 is the portable on-disk representation: every integer and
    // IEEE-754 value is emitted as an explicit little-endian field. Version 1
    // files remain readable only as a legacy native-layout compatibility path.
    static bool write_header(std::ostream& out, const Gs3dHeader& header);

    static bool read_header(std::istream& in, Gs3dHeader& header);

    static bool write_points(
        std::ostream& out,
        std::span<const Gs3dPoint> points
    );

    static bool read_points(
        std::istream& in,
        const Gs3dHeader& header,
        std::vector<Gs3dPoint>& points
    );
};

} // namespace gs3d::data
