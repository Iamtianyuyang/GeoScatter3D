#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "app/ViewerAppInternal.hpp"
#include "data/Gs3dDataset.hpp"
#include "render/PointPipeline.hpp"
#include "core/SceneState.hpp"

#include <vector>

namespace gs3d::app {

struct ViewerRenderSettingsContext {
    gs3d::render::PointPushConstants& push;
    gs3d::scene::SceneState& scene_state;
    NavigationMapState& navigation_map;
    const std::vector<AttrDescriptor>& attributes;
    const gs3d::data::Gs3dDataset& dataset;
    float& height_exaggeration;
};

// Applies UI render settings to their intended viewport state. The system is
// deliberately independent of ViewerApp so command routing and push-constant
// updates can be exercised without a window or Vulkan runtime.
class ViewerRenderSettingsSystem {
public:
    void apply_commands(
        const std::vector<RenderSettingsCommand>& commands,
        AppState& app_state,
        std::vector<gs3d::render::PointPushConstants>& viewport_pushes,
        std::vector<gs3d::scene::SceneState>& viewport_scenes,
        const std::vector<AttrDescriptor>& attributes,
        const gs3d::data::Gs3dDataset& dataset,
        std::vector<float>& viewport_height_exaggerations
    ) const;

    void apply(
        const RenderSettingsCommand& command,
        ViewerRenderSettingsContext& context
    ) const;
};

} // namespace gs3d::app
