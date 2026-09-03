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

    // 验证全新别名与显示名称
    const auto* arctic = reg.find_theme("arctic-light");
    REQUIRE(arctic != nullptr);
    CHECK(std::string_view(arctic->name) == "极地冷白 (Arctic Light)");

    const auto* midnight = reg.find_theme("midnight-dark");
    REQUIRE(midnight != nullptr);
    CHECK(std::string_view(midnight->name) == "深空暗夜 (Midnight Dark)");

    const auto* slate = reg.find_theme("slate-graphite");
    REQUIRE(slate != nullptr);
    CHECK(std::string_view(slate->name) == "蓝灰石墨 (Slate Graphite)");

    const auto* radar = reg.find_theme("radar-amber");
    REQUIRE(radar != nullptr);
    CHECK(std::string_view(radar->name) == "雷达琥珀 (Radar Amber)");

    const auto* contrast = reg.find_theme("contrast-light");
    REQUIRE(contrast != nullptr);
    CHECK(std::string_view(contrast->name) == "强光对比 (High Contrast)");
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
