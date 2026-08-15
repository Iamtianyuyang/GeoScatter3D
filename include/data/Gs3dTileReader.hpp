#pragma once

#include "data/Gs3dFormat.hpp"
#include "data/Gs3dTileFormat.hpp"

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace gs3d::data {

/*
 * Returned by read_tile_points_with_ids().
 * In v1 format point_ids is empty; in v2 it is populated from the
 * embedded per-point uint32_t in the tile data file.
 */
struct Gs3dTilePointBlock {
    std::vector<Gs3dPoint> points;
    std::vector<std::uint32_t> point_ids;
};

struct Gs3dTileQueryBox {
    float min_x = 0.0f;
    float min_y = 0.0f;
    float min_z = 0.0f;

    float max_x = 0.0f;
    float max_y = 0.0f;
    float max_z = 0.0f;
};

struct Gs3dTileReaderStats {
    std::filesystem::path index_path;
    std::filesystem::path data_path;

    std::uint64_t tile_count = 0;
    std::uint64_t total_point_count = 0;
    std::uint64_t total_point_bytes = 0;

    std::uint64_t index_file_bytes = 0;
    std::uint64_t data_file_bytes = 0;

    bool success = false;
};

class Gs3dTileReader {
public:
    Gs3dTileReader() = default;

    [[nodiscard]]
    static Gs3dTileReader open(
        const std::filesystem::path& index_path,
        const std::filesystem::path& data_path,
        const Gs3dHeader& source_header
    );

    [[nodiscard]]
    static Gs3dTileReader open_without_source_validation(
        const std::filesystem::path& index_path,
        const std::filesystem::path& data_path
    );

    [[nodiscard]]
    bool valid() const noexcept;

    [[nodiscard]]
    const std::filesystem::path& index_path() const noexcept;

    [[nodiscard]]
    const std::filesystem::path& data_path() const noexcept;

    [[nodiscard]]
    const Gs3dTileIndexFileHeader& index_header() const noexcept;

    [[nodiscard]]
    const Gs3dTileDataFileHeader& data_header() const noexcept;

    [[nodiscard]]
    const std::vector<Gs3dTileRecord>& records() const noexcept;

    [[nodiscard]]
    std::uint64_t tile_count() const noexcept;

    [[nodiscard]]
    std::uint64_t total_point_count() const noexcept;

    [[nodiscard]]
    std::vector<Gs3dPoint> read_tile_points(
        std::uint64_t tile_id
    ) const;

    /*
     * Returns true when the tile data file has embedded per-point
     * global IDs (format version >= 2).
     */
    [[nodiscard]]
    bool has_embedded_point_ids() const noexcept;

    /*
     * Reads tile points and their global point_ids in one operation.
     *
     * v2 format (point_stride 20):  both points and point_ids are
     *     populated from the interleaved on-disk data.
     *
     * v1 format (point_stride 16):  points are populated, point_ids
     *     is empty.  The caller must supply point_ids externally via
     *     the slow startup scan.
     */
    [[nodiscard]]
    Gs3dTilePointBlock read_tile_points_with_ids(
        std::uint64_t tile_id
    ) const;

    [[nodiscard]]
    std::vector<Gs3dTileRecord> query_records_by_bbox(
        const Gs3dTileQueryBox& box
    ) const;

    [[nodiscard]]
    std::vector<std::uint64_t> query_tile_ids_by_bbox(
        const Gs3dTileQueryBox& box
    ) const;

    /*
     * EPT-style grid-address query.
     * Selects tiles by logical grid cell range derived from box and grid
     * parameters, guaranteeing that every tile owning at least one point
     * inside [box.min_x, box.max_x] × [box.min_y, box.max_y] is returned.
     */
    [[nodiscard]]
    std::vector<std::uint64_t> query_tile_ids_by_grid(
        const Gs3dTileQueryBox& box
    ) const;

    [[nodiscard]]
    const Gs3dTileRecord& record(
        std::uint64_t tile_id
    ) const;

    [[nodiscard]]
    const Gs3dTileRecord& max_point_count_record() const;

    [[nodiscard]]
    Gs3dTileReaderStats stats() const;

private:
    std::filesystem::path index_path_;
    std::filesystem::path data_path_;

    Gs3dTileIndexFileHeader index_header_{};
    Gs3dTileDataFileHeader data_header_{};

    std::vector<Gs3dTileRecord> records_;

    /*
     * Maps logical grid key (tile_y * grid_count_x + tile_x) → tile_id.
     * Built at open() time for O(1) EPT-style grid-address lookup.
     */
    std::unordered_map<std::uint64_t, std::uint64_t> grid_to_tile_id_;

private:
    static void validate_query_box(
        const Gs3dTileQueryBox& box
    );

    [[nodiscard]]
    static bool intersects(
        const Gs3dTileRecord& record,
        const Gs3dTileQueryBox& box
    ) noexcept;

    /*
     * 读取前逐记录校验：point_data_bytes 必须严格等于
     * point_count × data header stride，否则显式报错，禁止按错误
     * stride 解码导致尾部点静默置零。
     */
    void validate_record_stride(
        const Gs3dTileRecord& record
    ) const;

    void build_grid_map() noexcept;
};

} // namespace gs3d::data