#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace gs3d::app {

struct ResourcePathContext {
    std::filesystem::path config_path;
    std::filesystem::path executable_path;
};

class ResourcePath {
public:
    [[nodiscard]]
    static std::filesystem::path resolve_existing_file(
        const std::filesystem::path& path,
        const ResourcePathContext& context
    );

    [[nodiscard]]
    static std::filesystem::path resolve_existing_file(
        const std::filesystem::path& path,
        const std::filesystem::path& config_path,
        const std::filesystem::path& executable_path
    );

    [[nodiscard]]
    static std::filesystem::path resolve_optional_file(
        const std::filesystem::path& path,
        const ResourcePathContext& context
    );

    // 解析一个"将要写入"的路径（如 ImGui 布局 ini）。与字体/着色器一致的
    // 搜索根回退策略：若某个搜索根下已存在同名文件（说明是上次会话的持久化
    // 文件），优先命中该位置，避免因启动 CWD 不同而读写到不同位置；文件尚
    // 不存在时落到第一个搜索根（配置所在目录的父目录，即发布根）下创建。
    [[nodiscard]]
    static std::filesystem::path resolve_writable_file(
        const std::filesystem::path& path,
        const ResourcePathContext& context
    );

    [[nodiscard]]
    static std::filesystem::path current_working_directory();

    // Best-effort absolute path to the running executable. Used for packaged
    // runtime assets so lookup does not depend on the process working dir.
    [[nodiscard]]
    static std::filesystem::path current_executable_path();

    [[nodiscard]]
    static std::filesystem::path config_directory(
        const std::filesystem::path& config_path
    );

    [[nodiscard]]
    static std::filesystem::path executable_directory(
        const std::filesystem::path& executable_path
    );

    [[nodiscard]]
    static std::vector<std::filesystem::path> search_roots(
        const ResourcePathContext& context
    );

private:
    [[nodiscard]]
    static std::filesystem::path normalize(
        const std::filesystem::path& path
    );

    [[nodiscard]]
    static bool is_existing_regular_file(
        const std::filesystem::path& path
    );

    [[nodiscard]]
    static std::string make_not_found_message(
        const std::filesystem::path& path,
        const std::vector<std::filesystem::path>& roots
    );
};

} // namespace gs3d::app
