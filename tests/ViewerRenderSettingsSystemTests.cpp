#include "app/ViewerAttributeMapping.hpp"
#include "app/ViewerRenderSettingsSystem.hpp"

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

TEST_CASE(
    "ViewerRenderSettingsSystem applies active and scoped commands",
    "[render_settings]"
)
{
    const auto dataset = make_dataset();
    const gs3d::app::ViewerAttributeMapping mapping(dataset, "fold", "z");
    gs3d::app::AppState app_state;
    app_state.active_viewport_index = 1;
    app_state.navigation_maps.resize(2);
    std::vector<gs3d::render::PointPushConstants> pushes(2);
    std::vector<gs3d::scene::SceneState> scenes(2);
    std::vector<float> height_exaggerations{1.0f, 1.0f};
    gs3d::app::ViewerRenderSettingsSystem system;

    gs3d::app::RenderSettingsCommand active_command;
    active_command.color_by_changed = true;
    active_command.color_by_index = 1;
    active_command.value_clip_changed = true;
    active_command.value_clip_enabled = true;
    active_command.value_clip_min = 12.0f;
    active_command.value_clip_max = 22.0f;
    system.apply_commands(
        {active_command},
        app_state,
        pushes,
        scenes,
        mapping.descriptors(),
        dataset,
        height_exaggerations
    );

    CHECK(scenes[1].active_attribute_index == 1);
    CHECK(pushes[1].color_min == Catch::Approx(10.0f));
    CHECK(pushes[1].color_range == Catch::Approx(20.0f));
    CHECK(pushes[1].clip_min[3] == Catch::Approx(0.1f));
    CHECK(pushes[1].clip_max[3] == Catch::Approx(0.6f));
    CHECK(app_state.navigation_maps[1].dirty);
    CHECK(scenes[0].active_attribute_index == 0);

    gs3d::app::RenderSettingsCommand scoped_command;
    scoped_command.has_viewport_scope = true;
    scoped_command.viewport_indices = {0, 9};
    scoped_command.point_size_changed = true;
    scoped_command.point_size = 99.0f;
    scoped_command.height_by_changed = true;
    scoped_command.height_by_index = 0;
    scoped_command.height_exag_changed = true;
    scoped_command.height_exag = 2.0f;
    system.apply_commands(
        {scoped_command},
        app_state,
        pushes,
        scenes,
        mapping.descriptors(),
        dataset,
        height_exaggerations
    );

    CHECK(pushes[0].point_size == Catch::Approx(10.0f));
    CHECK(scenes[0].active_height_index == 0);
    CHECK(height_exaggerations[0] == Catch::Approx(2.0f));
    CHECK(pushes[0].height_mult == Catch::Approx(10.0f));
    CHECK(pushes[0].height_offset == Catch::Approx(-10.0f));
}
