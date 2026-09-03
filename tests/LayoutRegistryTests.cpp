#include "ui/LayoutRegistry.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace gs3d::ui;

TEST_CASE("LayoutRegistry has default workbench layout", "[layout_registry]") {
    auto& reg = LayoutRegistry::instance();
    CHECK(reg.layout_count() >= 1);

    const auto* wb = reg.find_layout("workbench");
    REQUIRE(wb != nullptr);
    CHECK(wb->id == "workbench");
    CHECK(wb->name == "标准工作台");
    CHECK(wb->enabled);
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
