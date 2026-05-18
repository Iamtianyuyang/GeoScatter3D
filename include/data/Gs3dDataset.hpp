#pragma once

#include "data/Gs3dFormat.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gs3d::data {

class Gs3dDataset {
public:
    Gs3dDataset() = default;

    Gs3dDataset(
        Gs3dHeader header,
        std::vector<Gs3dPoint> points,
        std::filesystem::path source_path
    );

    [[nodiscard]]
    const Gs3dHeader& header() const noexcept;

    [[nodiscard]]
    const std::vector<Gs3dPoint>& points() const noexcept;

    [[nodiscard]]
    std::vector<Gs3dPoint>& points() noexcept;

    [[nodiscard]]
    const Gs3dPoint* point_data() const noexcept;

    [[nodiscard]]
    std::uint64_t point_count() const noexcept;

    [[nodiscard]]
    std::uint64_t point_bytes() const noexcept;

    [[nodiscard]]
    bool empty() const noexcept;

    [[nodiscard]]
    const std::filesystem::path& source_path() const noexcept;

    [[nodiscard]]
    double origin_x() const noexcept;

    [[nodiscard]]
    double origin_y() const noexcept;

    [[nodiscard]]
    double origin_z() const noexcept;

    [[nodiscard]]
    float bbox_min_x() const noexcept;

    [[nodiscard]]
    float bbox_min_y() const noexcept;

    [[nodiscard]]
    float bbox_min_z() const noexcept;

    [[nodiscard]]
    float bbox_max_x() const noexcept;

    [[nodiscard]]
    float bbox_max_y() const noexcept;

    [[nodiscard]]
    float bbox_max_z() const noexcept;

    [[nodiscard]]
    float value_min() const noexcept;

    [[nodiscard]]
    float value_max() const noexcept;

    [[nodiscard]]
    std::string summary() const;

    [[nodiscard]]
    bool is_consistent() const noexcept;

private:
    Gs3dHeader header_{};
    std::vector<Gs3dPoint> points_;
    std::filesystem::path source_path_;
};

class Gs3dDatasetLoader {
public:
    [[nodiscard]]
    static Gs3dDataset load(const std::filesystem::path& path);

    [[nodiscard]]
    static Gs3dDataset load_header_only(const std::filesystem::path& path);
};

} // namespace gs3d::data