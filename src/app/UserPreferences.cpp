#include "app/UserPreferences.hpp"

#include "app/AppState.hpp"
#include "util/Log.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
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

// 读取既有偏好文件（解析失败按空表处理，不阻断写入）。
[[nodiscard]]
toml::table load_existing_preferences(
    const std::filesystem::path& path
) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return {};
    }
    try {
        return toml::parse_file(path.string());
    } catch (const std::exception& error) {
        gs3d::util::log::warning()
            << "[WARN] Ignoring invalid user preferences at "
            << path << ": " << error.what() << '\n';
        return {};
    }
}

// 原子写整份偏好文件：先写临时文件再重命名，中断不会留下截断文件。
// Windows 无法直接 rename 覆盖已存在文件，先移除旧文件重试。
bool write_preferences_file(
    const std::filesystem::path& path,
    const toml::table& root
) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        gs3d::util::log::warning()
            << "[WARN] Failed to create user preferences directory "
            << path.parent_path() << ": " << ec.message() << '\n';
        return false;
    }

    const auto temporary_path =
        std::filesystem::path(path.string() + ".tmp");
    {
        std::ofstream output(temporary_path, std::ios::trunc);
        if (!output) {
            gs3d::util::log::warning()
                << "[WARN] Failed to write user preferences at "
                << temporary_path << '\n';
            return false;
        }
        output << root;
        if (!output.good()) {
            gs3d::util::log::warning()
                << "[WARN] Failed while writing user preferences at "
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

    gs3d::util::log::warning()
        << "[WARN] Failed to finalize user preferences at "
        << path << ": " << ec.message() << '\n';
    return false;
}

} // namespace

std::filesystem::path user_preferences_path()
{
    return user_config_directory() / "preferences.toml";
}

std::optional<std::string> load_preferred_gpu_preference()
{
    const auto root = load_existing_preferences(user_preferences_path());
    const auto* graphics = root["graphics"].as_table();
    if (graphics == nullptr) {
        return std::nullopt;
    }
    return (*graphics)["preferred_gpu"].value<std::string>();
}

bool save_preferred_gpu_preference(std::string_view preferred_gpu)
{
    const auto path = user_preferences_path();
    auto root = load_existing_preferences(path);
    toml::table* graphics = root["graphics"].as_table();
    if (graphics == nullptr) {
        root.insert("graphics", toml::table{});
        graphics = root["graphics"].as_table();
    }
    graphics->insert_or_assign(
        "preferred_gpu", escape_toml_string(preferred_gpu)
    );
    return write_preferences_file(path, root);
}

std::optional<RenderSettingsPreferences>
load_render_settings_preferences()
{
    const auto root = load_existing_preferences(user_preferences_path());
    const auto* section = root["render_settings"].as_table();
    if (section == nullptr) {
        return std::nullopt;
    }

    RenderSettingsPreferences preferences;
    if (const auto value = (*section)["point_size"].value<float>()) {
        preferences.point_size = *value;
    }
    if (const auto value = (*section)["point_shape"].value<int>()) {
        preferences.point_shape = *value;
    }
    if (const auto value = (*section)["height_attr_index"].value<int>()) {
        preferences.height_attr_index = *value;
    }
    if (const auto value = (*section)["color_attr_index"].value<int>()) {
        preferences.color_attr_index = *value;
    }
    if (const auto value =
            (*section)["height_exaggeration"].value<float>()) {
        preferences.height_exaggeration = *value;
    }
    if (const auto value = (*section)["colormap_index"].value<int>()) {
        preferences.colormap_index = *value;
    }
    if (const auto value =
            (*section)["value_clip_enabled"].value<bool>()) {
        preferences.value_clip_enabled = *value;
    }
    if (const auto value = (*section)["value_clip_min"].value<float>()) {
        preferences.value_clip_min = *value;
    }
    if (const auto value = (*section)["value_clip_max"].value<float>()) {
        preferences.value_clip_max = *value;
    }
    return preferences;
}

bool save_render_settings_preferences(
    const RenderSettingsPreferences& preferences
) {
    const auto path = user_preferences_path();
    auto root = load_existing_preferences(path);
    toml::table* section = root["render_settings"].as_table();
    if (section == nullptr) {
        root.insert("render_settings", toml::table{});
        section = root["render_settings"].as_table();
    }
    section->insert_or_assign("point_size", preferences.point_size);
    section->insert_or_assign("point_shape", preferences.point_shape);
    section->insert_or_assign(
        "height_attr_index", preferences.height_attr_index
    );
    section->insert_or_assign(
        "color_attr_index", preferences.color_attr_index
    );
    section->insert_or_assign(
        "height_exaggeration", preferences.height_exaggeration
    );
    section->insert_or_assign(
        "colormap_index", preferences.colormap_index
    );
    section->insert_or_assign(
        "value_clip_enabled", preferences.value_clip_enabled
    );
    section->insert_or_assign(
        "value_clip_min", preferences.value_clip_min
    );
    section->insert_or_assign(
        "value_clip_max", preferences.value_clip_max
    );
    return write_preferences_file(path, root);
}

void apply_render_settings_preferences(
    AppState& state,
    const RenderSettingsPreferences& preferences
) {
    state.render_settings.point_size = preferences.point_size;
    state.render_settings.point_shape = std::clamp(
        preferences.point_shape, 0, 3
    );
    state.render_settings.height_exaggeration =
        preferences.height_exaggeration;
    state.render_settings.colormap_index = std::max(
        0, preferences.colormap_index
    );
    state.render_settings.value_clip_enabled =
        preferences.value_clip_enabled;
    state.render_settings.value_clip_min = preferences.value_clip_min;
    state.render_settings.value_clip_max = preferences.value_clip_max;

    // 属性索引按当前数据集的属性表钳制（不同数据集属性数可能不同）。
    if (!state.render_settings.height_by_options.empty()) {
        state.render_settings.height_attr_index = std::clamp(
            preferences.height_attr_index,
            0,
            static_cast<int>(
                state.render_settings.height_by_options.size()
            ) - 1
        );
    }
    if (!state.render_settings.color_by_options.empty()) {
        state.render_settings.color_attr_index = std::clamp(
            preferences.color_attr_index,
            0,
            static_cast<int>(
                state.render_settings.color_by_options.size()
            ) - 1
        );
    }
}

} // namespace gs3d::app
