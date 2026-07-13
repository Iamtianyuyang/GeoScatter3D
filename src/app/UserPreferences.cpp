#include "app/UserPreferences.hpp"

#include <toml++/toml.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace gs3d::app {

namespace {

[[nodiscard]]
std::filesystem::path user_config_directory()
{
    if (const char* override_path =
            std::getenv("GS3D_USER_CONFIG_DIR")) {
        if (override_path[0] != '\0') {
            return override_path;
        }
    }

    std::filesystem::path config_root;
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA")) {
        if (appdata[0] != '\0') {
            config_root = appdata;
        }
    }
#else
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
#endif

    if (config_root.empty()) {
        std::error_code ec;
        config_root = std::filesystem::temp_directory_path(ec);
        if (ec) {
            config_root = ".";
        }
    }

    return config_root / "geoscatter3d";
}

[[nodiscard]]
std::string escape_toml_string(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

} // namespace

std::filesystem::path user_preferences_path()
{
    return user_config_directory() / "preferences.toml";
}

std::optional<std::string> load_preferred_gpu_preference()
{
    const auto path = user_preferences_path();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return std::nullopt;
    }

    try {
        const auto root = toml::parse_file(path.string());
        const auto* graphics = root["graphics"].as_table();
        if (graphics == nullptr) {
            return std::nullopt;
        }
        return (*graphics)["preferred_gpu"].value<std::string>();
    } catch (const std::exception& error) {
        std::cerr << "[WARN] Ignoring invalid user preferences at "
                  << path << ": " << error.what() << '\n';
        return std::nullopt;
    }
}

bool save_preferred_gpu_preference(std::string_view preferred_gpu)
{
    const auto path = user_preferences_path();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        std::cerr << "[WARN] Failed to create user preferences directory "
                  << path.parent_path() << ": " << ec.message() << '\n';
        return false;
    }

    const auto temporary_path =
        std::filesystem::path(path.string() + ".tmp");
    {
        std::ofstream output(temporary_path, std::ios::trunc);
        if (!output) {
            std::cerr << "[WARN] Failed to write user preferences at "
                      << temporary_path << '\n';
            return false;
        }
        output << "[graphics]\n"
               << "preferred_gpu = \""
               << escape_toml_string(preferred_gpu)
               << "\"\n";
        if (!output.good()) {
            std::cerr << "[WARN] Failed while writing user preferences at "
                      << temporary_path << '\n';
            return false;
        }
    }

    std::filesystem::rename(temporary_path, path, ec);
    if (!ec) {
        return true;
    }

    // Windows cannot replace an existing file with rename(). Retry after
    // removing the old file, matching the existing recent-projects storage.
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temporary_path, path, ec);
    if (!ec) {
        return true;
    }

    std::cerr << "[WARN] Failed to finalize user preferences at "
              << path << ": " << ec.message() << '\n';
    return false;
}

} // namespace gs3d::app
