#include "ui/ThemeRegistry.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace gs3d::ui;

TEST_CASE("ThemeRegistry contains 5 builtin themes", "[theme_registry]") {
    auto& reg = ThemeRegistry::instance();
    CHECK(reg.theme_count() >= 5);

    const auto* carbon = reg.find_theme("carbon-blue");
    REQUIRE(carbon != nullptr);
    CHECK(std::string_view(carbon->id) == "carbon-blue");

    const auto* dark = reg.find_theme("carbon-blue-dark");
    REQUIRE(dark != nullptr);
    CHECK(dark->dark == true);

    const auto* graphite = reg.find_theme("deep-graphite");
    REQUIRE(graphite != nullptr);

    const auto* amber = reg.find_theme("instrument-amber");
    REQUIRE(amber != nullptr);

    const auto* hc = reg.find_theme("high-contrast");
    REQUIRE(hc != nullptr);
}

TEST_CASE("ThemeRegistry can register new color theme dynamically", "[theme_registry]") {
    auto& reg = ThemeRegistry::instance();
    const std::size_t initial_count = reg.theme_count();

    ThemeTokens custom_nord{
        .id = "nord-aurora",
        .name = "极光 · Nord Aurora",
        .dark = true,
        .bg = ImVec4(0.18f, 0.20f, 0.25f, 1.0f),
        .text = ImVec4(0.93f, 0.94f, 0.96f, 1.0f),
        .accent = ImVec4(0.53f, 0.75f, 0.82f, 1.0f)
    };

    CHECK(reg.register_theme(custom_nord));
    CHECK(reg.theme_count() == initial_count + 1);

    const auto* found = reg.find_theme("nord-aurora");
    REQUIRE(found != nullptr);
    CHECK(std::string_view(found->name) == "极光 · Nord Aurora");
    CHECK(found->dark == true);

    // Active theme switching
    reg.set_active_theme("nord-aurora");
    CHECK(reg.active_theme_name() == "极光 · Nord Aurora");
    CHECK(reg.active_theme_tokens().accent.x == custom_nord.accent.x);

    // Reset back
    reg.set_active_theme(ThemeId::kCarbonBlueDark);
    CHECK(reg.active_theme_id() == ThemeId::kCarbonBlueDark);
}
