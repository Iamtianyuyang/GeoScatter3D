#pragma once

#include "data/Gs3dFormat.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>
#include <functional>
#include <span>

namespace gs3d::data {

struct Gs3dReadResult {
    Gs3dHeader header{};
    std::vector<Gs3dPoint> points;
};

class Gs3dReader {
public:
    [[nodiscard]]
    static Gs3dHeader read_header(const std::filesystem::path& path);

    [[nodiscard]]
    static Gs3dReadResult read_all(const std::filesystem::path& path);

    // 固定内存块遍历完整源数据；回调返回 false 时立即取消。
    static bool visit_points(const std::filesystem::path& path,
        const std::function<bool(std::span<const Gs3dPoint>)>& visitor);

    static Gs3dReadResult read_preview(const std::filesystem::path& path, std::size_t limit = 65536);

    [[nodiscard]]
    static bool validate_file_size(const std::filesystem::path& path);
};

} // namespace gs3d::data
