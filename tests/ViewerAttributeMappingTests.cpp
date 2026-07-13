#include "app/ViewerAttributeMapping.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {

gs3d::data::Gs3dDataset make_dataset()
{
    auto header = gs3d::data::Gs3dFormat::create_empty_header();
    header.point_count = 2;
    header.bbox_min_z = 10.0f;
    header.bbox_max_z = 30.0f;
    header.value_min = 2.0f;
    header.value_max = 6.0f;
    return {
        header,
        {{0.0f, 0.0f, 10.0f, 2.0f}, {0.0f, 0.0f, 30.0f, 6.0f}},
        {},
        false
    };
}

} // namespace

TEST_CASE("ViewerAttributeMapping initializes push constants from named channels")
{
    const auto dataset = make_dataset();
    const gs3d::app::ViewerAttributeMapping mapping(
        dataset,
        "fold",
        "elevation"
    );

    const auto push = mapping.make_initial_push(2.5f, 1.5f);

    REQUIRE(mapping.descriptors().size() == 2);
    CHECK(mapping.primary_value_name() == "fold");
    CHECK(mapping.z_field_name() == "elevation");
    CHECK(push.point_size == Catch::Approx(2.5f));
    CHECK(push.color_min == Catch::Approx(2.0f));
    CHECK(push.color_range == Catch::Approx(4.0f));
    CHECK(push.height_mult == Catch::Approx(1.5f));
    CHECK(push.height_offset == Catch::Approx(0.0f));
}

TEST_CASE("ViewerAttributeMapping maps value height into the dataset elevation range")
{
    const auto dataset = make_dataset();
    const gs3d::app::ViewerAttributeMapping mapping(dataset, {}, {});
    gs3d::render::PointPushConstants push{};

    mapping.apply_height_to(push, mapping.descriptors().front(), 2.0f);

    CHECK(mapping.primary_value_name() == "value");
    CHECK(mapping.z_field_name() == "z");
    CHECK(push.height_mult == Catch::Approx(10.0f));
    CHECK(push.height_offset == Catch::Approx(-10.0f));
}
