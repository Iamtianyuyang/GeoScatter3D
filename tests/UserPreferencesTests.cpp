#include "app/UserPreferences.hpp"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {
class ScopedConfig {
public:
    explicit ScopedConfig(const std::filesystem::path& d) : d_(d) {
#if defined(_WIN32)
        _putenv_s("GS3D_USER_CONFIG_DIR", d.string().c_str());
#else
        setenv("GS3D_USER_CONFIG_DIR", d.string().c_str(), 1);
#endif
    }
    ~ScopedConfig() {
#if defined(_WIN32)
        _putenv_s("GS3D_USER_CONFIG_DIR", "");
#else
        unsetenv("GS3D_USER_CONFIG_DIR");
#endif
        std::error_code ec; std::filesystem::remove_all(d_, ec);
    }
private:
    std::filesystem::path d_;
};
std::filesystem::path test_root() {
    auto n = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("gs3d-ui-pref-" + std::to_string(n));
}
}

TEST_CASE("UI preferences round-trip", "[preferences]") {
    ScopedConfig c(test_root());
    CHECK_FALSE(gs3d::app::load_ui_preferences().has_value());
    CHECK(gs3d::app::save_ui_preferences({.theme="deep-graphite", .layout="workbench"}));
    auto p = gs3d::app::load_ui_preferences();
    REQUIRE(p.has_value());
    CHECK(p->theme == "deep-graphite");
    CHECK(p->layout == "workbench");
}

TEST_CASE("UI prefs preserve GPU section", "[preferences]") {
    ScopedConfig c(test_root());
    REQUIRE(gs3d::app::save_preferred_gpu_preference("uuid:abcd"));
    REQUIRE(gs3d::app::save_ui_preferences({.theme="high-contrast", .layout="workbench"}));
    CHECK(gs3d::app::load_preferred_gpu_preference().value_or("") == "uuid:abcd");
    auto p = gs3d::app::load_ui_preferences();
    REQUIRE(p.has_value()); CHECK(p->theme == "high-contrast");
    REQUIRE(gs3d::app::save_preferred_gpu_preference("auto"));
    auto p2 = gs3d::app::load_ui_preferences();
    REQUIRE(p2.has_value()); CHECK(p2->theme == "high-contrast");
}
