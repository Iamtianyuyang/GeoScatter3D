#include "app/ViewerPickSystem.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("ViewerPickSystem derives a bounded point-size-aware hover radius")
{
    CHECK(gs3d::app::compute_hover_pick_radius_px(0.1f) == 3);
    CHECK(gs3d::app::compute_hover_pick_radius_px(4.0f) == 3);
    CHECK(gs3d::app::compute_hover_pick_radius_px(8.0f) == 5);
    CHECK(gs3d::app::compute_hover_pick_radius_px(100.0f) == 5);
}

TEST_CASE("ViewerPickSystem resolves only valid runtime point identifiers")
{
    const std::vector<gs3d::data::Gs3dPoint> points{
        {1.0f, 2.0f, 3.0f, 4.0f},
        {5.0f, 6.0f, 7.0f, 8.0f}
    };
    const std::vector<std::uint8_t> valid{0, 1};

    gs3d::app::GpuPickResult result;
    result.has_hit = true;
    result.point_id = 1;
    const auto resolved = gs3d::app::resolve_pick_point(
        result,
        points,
        valid
    );
    REQUIRE(resolved.point.has_value());
    CHECK(resolved.via_runtime_lookup);
    CHECK(resolved.point->x == 5.0f);

    result.point_id = 0;
    const auto invalid = gs3d::app::resolve_pick_point(result, points, valid);
    CHECK_FALSE(invalid.point.has_value());
    CHECK_FALSE(invalid.via_runtime_lookup);
}
