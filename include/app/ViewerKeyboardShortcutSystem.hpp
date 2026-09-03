#pragma once

#include "app/ViewerAppInternal.hpp"
#include "render/PointPipeline.hpp"
#include "core/SceneState.hpp"

#include <optional>
#include <vector>

namespace gs3d::app {
class ViewerAttributeMapping;
struct ViewerCameraConfig;
class ViewportCameraSystem;
}
namespace gs3d::platform { class Window; }
namespace gs3d::render { class ViewportManager; }
namespace gs3d::camera { class CameraHub; }

namespace gs3d::app {

struct ViewerKeyboardShortcutContext {
    gs3d::platform::Window& window;
    bool imgui_wants_keyboard = false;
    bool keyboard_shortcuts_allowed = false;
    const ViewerCameraConfig& camera_config;
    AppState& app_state;
    gs3d::render::ViewportManager& viewport_manager;
    std::vector<gs3d::render::PointPushConstants>& viewport_pushes;
    std::vector<gs3d::scene::SceneState>& viewport_scene_states;
    std::vector<float>& viewport_height_exaggerations;
    const std::vector<AttrDescriptor>& attributes;
    ViewerAttributeMapping& attribute_mapping;
    ViewportCameraSystem& viewport_cameras;
    gs3d::camera::CameraHub& camera_hub;
    const gs3d::camera::CameraBounds& bounds;
    const std::vector<std::optional<gs3d::camera::Vec3>>& selected_focus_points;
    int streaming_viewport_index = 0;
    bool& tile_selection_dirty;
};

// Owns keyboard edge detection so held keys cause one semantic action rather
// than repeating every rendered frame.
class ViewerKeyboardShortcutSystem {
public:
    void process(ViewerKeyboardShortcutContext& context);

private:
    bool r_was_pressed_ = false;
    bool f_was_pressed_ = false;
    bool tab_was_pressed_ = false;
    bool shift_tab_was_pressed_ = false;
};

} // namespace gs3d::app
