#include "app/UserPreferences.hpp"

#include "app/AppState.hpp"
#include "util/Log.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace gs3d::app {

namespace {

[[nodiscard]]
std::filesystem::path user_config_directory()
{
    if (const char* o = std::getenv("GS3D_USER_CONFIG_DIR")) {
        if (o[0] != '\0') return o;
    }
    std::filesystem::path root;
#if defined(_WIN32)
    if (const char* a = std::getenv("APPDATA")) {
        if (a[0] != '\0') root = a;
    }
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME")) {
        if (x[0] != '\0') root = x;
    }
    if (root.empty()) {
        if (const char* h = std::getenv("HOME")) {
            if (h[0] != '\0') root = std::filesystem::path(h) / ".config";
        }
    }
#endif
    if (root.empty()) {
        std::error_code ec;
        root = std::filesystem::temp_directory_path(ec);
        if (ec) root = ".";
    }
    return root / "geoscatter3d";
}

// 读取既有偏好文件（解析失败按空表处理，不阻断写入）。
[[nodiscard]]
toml::table load_existing(const std::filesystem::path& p)
{
    try { return toml::parse_file(p.string()); }
    catch (const std::exception& e) {
        gs3d::util::log::warning()
            << "[WARN] Ignoring invalid preferences at "
            << p << ": " << e.what() << '\n';
        return {};
    }
}

// 原子写整份偏好文件：先写临时文件再重命名，中断不会留下截断文件。
// Windows 无法直接 rename 覆盖已存在文件，先移除旧文件重试。
bool write_preferences(
    const std::filesystem::path& p,
    const toml::table& root
) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    if (ec) {
        gs3d::util::log::warning()
            << "[WARN] Failed to create user preferences directory "
            << p.parent_path() << ": " << ec.message() << '\n';
        return false;
    }

    const auto tmp = std::filesystem::path(p.string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            gs3d::util::log::warning()
                << "[WARN] Failed to write user preferences at "
                << tmp << '\n';
            return false;
        }
        out << root;
        if (!out.good()) {
            gs3d::util::log::warning()
                << "[WARN] Failed while writing user preferences at "
                << tmp << '\n';
            return false;
        }
    }

    std::filesystem::rename(tmp, p, ec);
    if (!ec) return true;

    // Windows cannot replace an existing file with rename(). Retry after
    // removing the old file, matching the existing recent-projects storage.
    std::filesystem::remove(p, ec);
    ec.clear();
    std::filesystem::rename(tmp, p, ec);
    if (!ec) return true;

    gs3d::util::log::warning()
        << "[WARN] Failed to finalize user preferences at "
        << p << ": " << ec.message() << '\n';
    return false;
}

toml::table* ensure_table(toml::table& root, std::string_view section)
{
    auto* t = root[section].as_table();
    if (!t) {
        root.insert(section, toml::table{});
        t = root[section].as_table();
    }
    return t;
}

// 转义 TOML 字符串中的特殊字符（双引号、反斜杠、换行）。
[[nodiscard]]
std::string escape_toml_string(std::string_view s)
{
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '"':  result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n";  break;
        default:   result += c;      break;
        }
    }
    return result;
}

} // namespace

std::filesystem::path user_preferences_path()
{
    return user_config_directory() / "preferences.toml";
}

std::optional<std::string> load_preferred_gpu_preference()
{
    const auto root = load_existing(user_preferences_path());
    const auto* graphics = root["graphics"].as_table();
    if (graphics == nullptr) {
        return std::nullopt;
    }
    return (*graphics)["preferred_gpu"].value<std::string>();
}

bool save_preferred_gpu_preference(std::string_view preferred_gpu)
{
    const auto path = user_preferences_path();
    auto root = load_existing(path);
    auto* graphics = ensure_table(root, "graphics");
    graphics->insert_or_assign(
        "preferred_gpu", escape_toml_string(preferred_gpu)
    );
    return write_preferences(path, root);
}

// --- Render settings persistence (TIA-90) ---

std::optional<RenderSettingsPreferences>
load_render_settings_preferences()
{
    const auto root = load_existing(user_preferences_path());
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
    auto root = load_existing(path);
    auto* section = ensure_table(root, "render_settings");
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
    return write_preferences(path, root);
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

// --- UI preferences (TIA-92 碳蓝工作台 2.0) ---

std::optional<UiPreferences> load_ui_preferences()
{
    auto path = user_preferences_path();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec)
        return std::nullopt;
    auto root = load_existing(path);
    auto* ui = root["ui"].as_table();
    if (!ui) return std::nullopt;
    UiPreferences p;
    p.theme = (*ui)["theme"].value<std::string>().value_or("");
    p.layout = (*ui)["layout"].value<std::string>().value_or("");
    if (p.theme.empty() && p.layout.empty()) return std::nullopt;
    return p;
}

bool save_ui_preferences(const UiPreferences& p)
{
    auto path = user_preferences_path();
    auto root = load_existing(path);
    auto* ui = ensure_table(root, "ui");
    ui->insert_or_assign("theme", p.theme);
    ui->insert_or_assign("layout", p.layout);
    return write_preferences(path, root);
}

} // namespace gs3d::app
