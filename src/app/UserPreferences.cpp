#include "app/UserPreferences.hpp"
#include "util/Log.hpp"

#include <toml++/toml.hpp>

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

[[nodiscard]]
toml::table load_existing(const std::filesystem::path& p)
{
    try { return toml::parse_file(p.string()); }
    catch (const std::exception& e) {
        gs3d::util::log::warning() << "[WARN] Ignoring invalid preferences at " << p << ": " << e.what() << '\n';
        return {};
    }
}

bool write_preferences(const std::filesystem::path& p, const toml::table& root)
{
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    if (ec) {
        gs3d::util::log::warning() << "[WARN] Failed to create prefs dir " << p.parent_path() << ": " << ec.message() << '\n';
        return false;
    }
    auto tmp = std::filesystem::path(p.string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) { gs3d::util::log::warning() << "[WARN] Failed to write prefs at " << tmp << '\n'; return false; }
        out << root;
        if (!out.good()) { gs3d::util::log::warning() << "[WARN] Failed while writing prefs at " << tmp << '\n'; return false; }
    }
    std::filesystem::rename(tmp, p, ec);
    if (!ec) return true;
    std::filesystem::remove(p, ec);
    ec.clear();
    std::filesystem::rename(tmp, p, ec);
    if (!ec) return true;
    gs3d::util::log::warning() << "[WARN] Failed to finalize prefs at " << p << ": " << ec.message() << '\n';
    return false;
}

toml::table* ensure_table(toml::table& root, std::string_view section)
{
    auto* t = root[section].as_table();
    if (!t) { root.insert(section, toml::table{}); t = root[section].as_table(); }
    return t;
}

} // namespace

std::filesystem::path user_preferences_path()
{
    return user_config_directory() / "preferences.toml";
}

std::optional<std::string> load_preferred_gpu_preference()
{
    auto root = load_existing(user_preferences_path());
    auto* g = root["graphics"].as_table();
    if (!g) return std::nullopt;
    return (*g)["preferred_gpu"].value<std::string>();
}

bool save_preferred_gpu_preference(std::string_view gpu)
{
    auto path = user_preferences_path();
    auto root = load_existing(path);
    auto* g = ensure_table(root, "graphics");
    g->insert_or_assign("preferred_gpu", std::string(gpu));
    return write_preferences(path, root);
}

std::optional<UiPreferences> load_ui_preferences()
{
    auto path = user_preferences_path();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return std::nullopt;
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
