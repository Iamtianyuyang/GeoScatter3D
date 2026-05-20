#pragma once

#include "data/Gs3dFormat.hpp"
#include "data/Gs3dTileFormat.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace gs3d::data {

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

    [[nodiscard]]
    std::vector<Gs3dTileRecord> query_records_by_bbox(
        const Gs3dTileQueryBox& box
    ) const;

    [[nodiscard]]
    std::vector<std::uint64_t> query_tile_ids_by_bbox(
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

private:
    static void validate_query_box(
        const Gs3dTileQueryBox& box
    );

    [[nodiscard]]
    static bool intersects(
        const Gs3dTileRecord& record,
        const Gs3dTileQueryBox& box
    ) noexcept;
};

} // namespace gs3d::data