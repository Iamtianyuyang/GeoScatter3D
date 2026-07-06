#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace gs3d::app {

struct RecentProjectEntry {
    std::filesystem::path path;
    std::int64_t last_opened_unix = 0;
};

[[nodiscard]]
std::filesystem::path recent_projects_storage_path();

[[nodiscard]]
std::vector<RecentProjectEntry> load_recent_projects(
    std::size_t max_entries = 8
);

void remember_recent_project(
    const std::filesystem::path& project_path,
    std::size_t max_entries = 8
);

void clear_recent_projects();

} // namespace gs3d::app
