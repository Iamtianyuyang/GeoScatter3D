#include "app/ViewerAppStateInitialization.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

TEST_CASE("Viewer app state initialization reflects dataset startup state")
{
    gs3d::core::DatasetDescriptor dataset;
    dataset.display_name = "survey.gs3d";
    dataset.path = "/portable/survey.gs3d";
    dataset.point_count = 42;
    dataset.file_size = "1.25 MB";
    dataset.bounds = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    dataset.dataset_tree = {"survey.gs3d", "瓦片", "细节层级", "属性"};

    const std::vector<gs3d::app::AttrDescriptor> attributes{
        {"fold", gs3d::app::AttrPhysicalSource::Value, 1.0f, 9.0f},
        {"elevation", gs3d::app::AttrPhysicalSource::Z, 2.0f, 8.0f}
    };
    const gs3d::data::Gs3dLodDataset lod_dataset;
    const std::optional<gs3d::data::Gs3dTileReader> tile_reader;
    const gs3d::app::ViewerAppStateInitializationInput input{
        dataset,
        attributes,
        lod_dataset,
        tile_reader,
        false,
        3,
        2,
        true
    };

    const auto state = gs3d::app::make_initial_viewer_app_state(input);

    CHECK(state.dataset.active_dataset == "survey.gs3d");
    CHECK(state.dataset.loaded_points == 42);
    CHECK(state.dataset.bounding_box ==
          "[1.000000, 2.000000, 3.000000] -> [4.000000, 5.000000, 6.000000]");
    CHECK(state.dataset.attributes ==
          std::vector<std::string>{"fold", "elevation"});
    CHECK(state.dataset.tile_details == std::vector<std::string>{"状态：未启用"});
    CHECK(state.dataset.lod_details == std::vector<std::string>{"状态：未启用"});
    REQUIRE(state.render_views.size() == 3);
    CHECK(state.render_views[0].visible);
    CHECK(state.render_views[1].visible);
    CHECK(!state.render_views[2].visible);
    CHECK(!state.panels.dataset);
    CHECK(!state.panels.render_settings);
    CHECK(!state.panels.performance);
}
