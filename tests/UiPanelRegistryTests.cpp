#include "ui/PanelRegistry.hpp"
#include <catch2/catch_test_macros.hpp>

using gs3d::app::PanelVisibilityState;
using gs3d::ui::PanelId;
using gs3d::ui::kPanelCount;
using gs3d::ui::kPanelRegistry;
using gs3d::ui::is_debug_panel;
using gs3d::ui::panel_matches_query;
using gs3d::ui::panel_name;
using gs3d::ui::panel_visibility;
using gs3d::ui::toggle_panel;

TEST_CASE("Panel registry covers every id", "[panel_registry]") {
    REQUIRE(static_cast<int>(kPanelRegistry.size()) == kPanelCount);
    for (int i = 0; i < kPanelCount; ++i) {
        CHECK(panel_name(static_cast<PanelId>(i)) == kPanelRegistry[i].name);
        CHECK(!panel_name(static_cast<PanelId>(i)).empty());
    }
    CHECK(panel_name(static_cast<PanelId>(kPanelCount)).empty());
}

TEST_CASE("Debug panels default closed, business panels default open", "[panel_registry]") {
    CHECK(is_debug_panel(PanelId::kTileInspector));
    CHECK(is_debug_panel(PanelId::kLodView));
    CHECK_FALSE(is_debug_panel(PanelId::kDataset));
    const PanelVisibilityState d;
    CHECK(d.dataset); CHECK(d.render_settings); CHECK(d.navigation_map);
    CHECK(d.measurement); CHECK(d.region_stats);
    CHECK_FALSE(d.performance); CHECK_FALSE(d.tile_inspector); CHECK_FALSE(d.lod_view);
}

TEST_CASE("Toggle panel flips visibility", "[panel_registry]") {
    PanelVisibilityState p;
    toggle_panel(p, PanelId::kPerformance); CHECK(p.performance);
    toggle_panel(p, PanelId::kPerformance); CHECK_FALSE(p.performance);
    toggle_panel(p, PanelId::kTileInspector); CHECK(p.tile_inspector);
    CHECK(panel_visibility(p, PanelId::kCount) == nullptr);
}

TEST_CASE("Command palette filter", "[panel_registry]") {
    for (int i = 0; i < kPanelCount; ++i) {
        CHECK(panel_matches_query(static_cast<PanelId>(i), ""));
        CHECK(panel_matches_query(static_cast<PanelId>(i), panel_name(static_cast<PanelId>(i))));
    }
    CHECK(panel_matches_query(PanelId::kDataset, "项目"));
    CHECK_FALSE(panel_matches_query(PanelId::kMeasurement, "瓦片"));
}
