#include "app/ViewerAppStateInitialization.hpp"
#include "app/ViewerDatasetDescriptor.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

TEST_CASE("Viewer dataset descriptor uses portable metadata and named attributes")
{
    auto header = gs3d::data::Gs3dFormat::create_empty_header();
    header.point_count = 2;
    header.bbox_min_x = 1.0f;
    header.bbox_min_y = 2.0f;
    header.bbox_min_z = 3.0f;
    header.bbox_max_x = 4.0f;
    header.bbox_max_y = 5.0f;
    header.bbox_max_z = 6.0f;
    const gs3d::data::Gs3dDataset dataset(
        header,
        {{1.0f, 2.0f, 3.0f, 4.0f}, {4.0f, 5.0f, 6.0f, 7.0f}},
        {},
        false
    );
    const std::vector<gs3d::app::AttrDescriptor> attributes{
        {"fold", gs3d::app::AttrPhysicalSource::Value, 4.0f, 7.0f},
        {"elevation", gs3d::app::AttrPhysicalSource::Z, 3.0f, 6.0f}
    };

    const auto descriptor = gs3d::app::make_viewer_dataset_descriptor(
        dataset,
        "/tmp/project/survey.gs3d",
        attributes
    );

    CHECK(descriptor.display_name == "survey.gs3d");
    CHECK(descriptor.path == "/tmp/project/survey.gs3d");
    CHECK(descriptor.point_count == 2);
    CHECK(descriptor.bounds.min_z == 3.0f);
    CHECK(descriptor.bounds.max_x == 4.0f);
    CHECK(descriptor.attributes[0].name == "fold");
    CHECK(descriptor.attributes[1].name == "elevation");
}

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

TEST_CASE("Initial viewer app state resolves the configured UI layout")
{
    auto header = gs3d::data::Gs3dFormat::create_empty_header();
    header.point_count = 1;
    const gs3d::data::Gs3dDataset dataset(
        header,
        {{0.0f, 0.0f, 0.0f, 0.0f}},
        {},
        false
    );
    const auto descriptor = gs3d::app::make_viewer_dataset_descriptor(
        dataset,
        "layout.gs3d",
        {}
    );
    const std::vector<gs3d::app::AttrDescriptor> attributes;
    const gs3d::data::Gs3dLodDataset lod_dataset;
    const std::optional<gs3d::data::Gs3dTileReader> tile_reader;
    const gs3d::app::ViewerAppStateInitializationInput input{
        descriptor,
        attributes,
        lod_dataset,
        tile_reader,
        false,
        1,
        1,
        false
    };

    SECTION("floating-dock 配置进入悬浮 Dock 布局") {
        const auto state =
            gs3d::app::make_initial_viewer_app_state(input, "floating-dock");
        CHECK(state.ui_layout_mode ==
              gs3d::app::UiLayoutMode::kFloatingDock);
    }
    SECTION("缺省与未知取值回退到工作台布局") {
        const auto default_state =
            gs3d::app::make_initial_viewer_app_state(input);
        CHECK(default_state.ui_layout_mode ==
              gs3d::app::UiLayoutMode::kWorkbench);
        const auto unknown_state =
            gs3d::app::make_initial_viewer_app_state(input, "holo-deck");
        CHECK(unknown_state.ui_layout_mode ==
              gs3d::app::UiLayoutMode::kWorkbench);
    }
}
