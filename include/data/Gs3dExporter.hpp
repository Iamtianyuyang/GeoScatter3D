#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace gs3d::data {

struct Gs3dExportResult {
    bool success = false;
    std::uint64_t point_count = 0;
    std::string error;
};

// 从原始 GS3D v2 数据文件全量导出点数据。该函数不依赖渲染缓存、LOD 或
// 当前可见瓦片，因此输出点数始终以源文件的 header.point_count 为准。
[[nodiscard]] Gs3dExportResult export_gs3d_points(
    const std::filesystem::path& source_path,
    const std::filesystem::path& target_path,
    const std::string& format
);

} // namespace gs3d::data
