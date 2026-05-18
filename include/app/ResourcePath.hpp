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

    [[nodiscard]]
    static std::filesystem::path current_working_directory();

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