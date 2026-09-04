#include "ui/layouts/LayoutRegistry.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace gs3d::ui;

TEST_CASE("LayoutRegistry has 3 builtin layouts", "[layout_registry]") {
    auto& reg = LayoutRegistry::instance();
    CHECK(reg.layout_count() >= 3);

    const auto* wb = reg.find_layout("workbench");
    REQUIRE(wb != nullptr);
    CHECK(wb->id == "workbench");
    CHECK(wb->name == "标准工作台");
    CHECK(wb->enabled);

    const auto* fd = reg.find_layout("floating-dock");
    REQUIRE(fd != nullptr);
    CHECK(fd->id == "floating-dock");
    CHECK(fd->name == "悬浮胶囊 Dock");

    const auto* ar = reg.find_layout("analysis-rail");
    REQUIRE(ar != nullptr);
    CHECK(ar->id == "analysis-rail");
    CHECK(ar->name == "暗色分析舱");
}

TEST_CASE("LayoutRegistry can register custom layout dynamically", "[layout_registry]") {
    auto& reg = LayoutRegistry::instance();
    const std::size_t initial_count = reg.layout_count();

    LayoutDescriptor quad{
        .id = "quad_split_test",
        .name = "四分屏视口",
        .description = "2x2 视口对比矩阵",
        .enabled = true
    };
    CHECK(reg.register_layout(quad));
    CHECK(reg.layout_count() == initial_count + 1);

    const auto* found = reg.find_layout("quad_split_test");
    REQUIRE(found != nullptr);
    CHECK(found->name == "四分屏视口");
    CHECK(found->description == "2x2 视口对比矩阵");

    // Switching active layout
    reg.set_active_layout_id("quad_split_test");
    CHECK(reg.active_layout_id() == "quad_split_test");
    CHECK(reg.active_layout_name() == "四分屏视口");

    // Reset back to workbench
    reg.set_active_layout_id("workbench");
    CHECK(reg.active_layout_id() == "workbench");
}

TEST_CASE("LayoutRegistry can switch between 3 builtin layouts", "[layout_registry]") {
    auto& reg = LayoutRegistry::instance();

    reg.set_active_layout_id("floating-dock");
    CHECK(reg.active_layout_id() == "floating-dock");
    CHECK(reg.active_layout_name() == "悬浮胶囊 Dock");

    reg.set_active_layout_id("analysis-rail");
    CHECK(reg.active_layout_id() == "analysis-rail");
    CHECK(reg.active_layout_name() == "暗色分析舱");

    reg.set_active_layout_id("workbench");
    CHECK(reg.active_layout_id() == "workbench");
    CHECK(reg.active_layout_name() == "标准工作台");
}
