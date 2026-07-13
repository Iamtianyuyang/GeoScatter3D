#include "app/ViewerApp.hpp"
#include "app/ViewerAppTileStreaming.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>

TEST_CASE("TileStreamingSystem requires a populated reader before preloading")
{
    gs3d::app::ViewerTileConfig config;
    config.enabled = true;
    config.preload_all = true;

    gs3d::app::TileStreamingSystem system(
        config,
        false,
        std::nullopt
    );
    CHECK_FALSE(system.state().preload_enabled);
}
