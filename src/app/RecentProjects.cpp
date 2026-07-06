#include "app/RecentProjects.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <string>
#include <unordered_set>

namespace gs3d::app {

namespace {

std::filesystem::path normalized_path(
    const std::filesystem::path& path
) {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal();
}

std::string path_key(const std::filesystem::path& path)
{
    return normalized_path(path).generic_string();
}

} // namespace

std::filesystem::path recent_projects_storage_path()
{
    if (const char* override_path =
            std::getenv("GS3D_RECENT_PROJECTS_PATH")) {
        if (override_path[0] != '\0') {
            return override_path;
        }
    }

    std::filesystem::path config_root;
    if (const char* xdg_config = std::getenv("XDG_CONFIG_HOME")) {
        if (xdg_config[0] != '\0') {
            config_root = xdg_config;
        }
    }
    if (config_root.empty()) {
        if (const char* home = std::getenv("HOME")) {
            if (home[0] != '\0') {
                config_root =
                    std::filesystem::path(home) / ".config";
            }
        }
    }
    if (config_root.empty()) {
        std::error_code ec;
        config_root = std::filesystem::temp_directory_path(ec);
        if (ec) {
            config_root = ".";
        }
    }
    return config_root /
        "geoscatter3d" /
        "recent-projects.txt";
}

std::vector<RecentProjectEntry> load_recent_projects(
    std::size_t max_entries
) {
    std::vector<RecentProjectEntry> entries;
    std::ifstream input(recent_projects_storage_path());
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('\t');
        if (separator == std::string::npos) {
            continue;
        }
        try {
            RecentProjectEntry entry;
            entry.last_opened_unix =
                std::stoll(line.substr(0, separator));
            entry.path = line.substr(separator + 1);
            if (!entry.path.empty()) {
                entries.push_back(std::move(entry));
            }
        } catch (const std::exception&) {
            continue;
        }
    }

    std::stable_sort(
        entries.begin(),
        entries.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.last_opened_unix > rhs.last_opened_unix;
        }
    );
    std::unordered_set<std::string> seen;
    std::vector<RecentProjectEntry> unique_entries;
    unique_entries.reserve(std::min(entries.size(), max_entries));
    for (auto& entry : entries) {
        if (!seen.insert(path_key(entry.path)).second) {
            continue;
        }
        unique_entries.push_back(std::move(entry));
        if (unique_entries.size() >= max_entries) {
            break;
        }
    }
    return unique_entries;
}

void remember_recent_project(
    const std::filesystem::path& project_path,
    std::size_t max_entries
) {
    if (project_path.empty()) {
        return;
    }

    auto entries = load_recent_projects(max_entries);
    const auto normalized = normalized_path(project_path);
    const auto key = path_key(normalized);
    entries.erase(
        std::remove_if(
            entries.begin(),
            entries.end(),
            [&key](const auto& entry) {
                return path_key(entry.path) == key;
            }
        ),
        entries.end()
    );
    entries.insert(
        entries.begin(),
        RecentProjectEntry{
            .path = normalized,
            .last_opened_unix =
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now()
                        .time_since_epoch()
                ).count()
        }
    );
    if (entries.size() > max_entries) {
        entries.resize(max_entries);
    }

    const auto storage_path = recent_projects_storage_path();
    std::error_code ec;
    std::filesystem::create_directories(
        storage_path.parent_path(),
        ec
    );
    if (ec) {
        return;
    }

    const auto temporary_path =
        storage_path.string() + ".tmp";
    {
        std::ofstream output(
            temporary_path,
            std::ios::trunc
        );
        if (!output) {
            return;
        }
        for (const auto& entry : entries) {
            output
                << entry.last_opened_unix
                << '\t'
                << entry.path.generic_string()
                << '\n';
        }
    }

    std::filesystem::rename(
        temporary_path,
        storage_path,
        ec
    );
    if (ec) {
        std::filesystem::remove(storage_path, ec);
        ec.clear();
        std::filesystem::rename(
            temporary_path,
            storage_path,
            ec
        );
    }
}

} // namespace gs3d::app
