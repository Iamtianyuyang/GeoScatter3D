#include "app/ViewportLodController.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr float kNearPixelsPerWorld = 4.5f;
constexpr float kFarPixelsPerWorld = 100.0f;

} // namespace

TEST_CASE(
    "ViewportLodController freezes the spatial target during interaction",
    "[viewport_lod]"
)
{
    gs3d::render::LodSelector selector;
    selector.update(true, 0.0);
    gs3d::app::ViewportLodController controller;
    const std::vector<float> voxel_sizes{1.0f, 4.0f, 16.0f};

    const auto initial = controller.select(
        selector,
        voxel_sizes,
        kNearPixelsPerWorld,
        true,
        true,
        0.8
    );
    REQUIRE(initial.has_value());
    CHECK(initial->spatial_level == 1);

    const auto frozen = controller.select(
        selector,
        voxel_sizes,
        kFarPixelsPerWorld,
        true,
        true,
        0.8
    );
    REQUIRE(frozen.has_value());
    CHECK(frozen->spatial_level == 1);
}

TEST_CASE(
    "ViewportLodController protects stable quality while interacting",
    "[viewport_lod]"
)
{
    const std::vector<float> voxel_sizes{1.0f, 4.0f, 16.0f};

    gs3d::render::LodSelector coarse_selector;
    coarse_selector.update(true, 0.0);
    gs3d::app::ViewportLodController coarse_controller;
    const auto coarse = coarse_controller.select(
        coarse_selector,
        voxel_sizes,
        kFarPixelsPerWorld,
        true,
        true,
        0.8
    );
    REQUIRE(coarse.has_value());
    CHECK(coarse->level == 2);

    gs3d::render::LodSelector stable_selector;
    stable_selector.update(true, 0.0);
    gs3d::app::ViewportLodController stable_controller;
    const auto stable = stable_controller.select(
        stable_selector,
        voxel_sizes,
        kFarPixelsPerWorld,
        true,
        false,
        0.8
    );
    REQUIRE(stable.has_value());
    CHECK(stable->level == 0);

    stable_selector.update(false, 1.0);
    const auto idle = stable_controller.select(
        stable_selector,
        voxel_sizes,
        kFarPixelsPerWorld,
        false,
        false,
        0.8
    );
    REQUIRE(idle.has_value());
    CHECK(idle->level == 2);
}

TEST_CASE(
    "ViewportLodController reports transitions and rejects an empty ladder",
    "[viewport_lod]"
)
{
    gs3d::render::LodSelector selector;
    selector.update(false, 1.0);
    gs3d::app::ViewportLodController controller;
    const std::vector<float> voxel_sizes{1.0f, 4.0f, 16.0f};

    const auto first = controller.select(
        selector,
        voxel_sizes,
        kFarPixelsPerWorld,
        false,
        true,
        0.8
    );
    REQUIRE(first.has_value());
    CHECK(first->changed);

    const auto second = controller.select(
        selector,
        voxel_sizes,
        kFarPixelsPerWorld,
        false,
        true,
        0.8
    );
    REQUIRE(second.has_value());
    CHECK_FALSE(second->changed);

    CHECK_FALSE(controller.select(
        selector,
        {},
        kFarPixelsPerWorld,
        false,
        true,
        0.8
    ).has_value());
}
