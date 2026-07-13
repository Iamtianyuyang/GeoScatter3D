#include "app/ViewerKeyboardShortcutSystem.hpp"

#include "app/AppState.hpp"
#include "app/ViewerApp.hpp"
#include "app/ViewerAttributeMapping.hpp"
#include "app/ViewportCameraSystem.hpp"
#include "camera/CameraHub.hpp"
#include "platform/Window.hpp"
#include "render/ViewportManager.hpp"
#include "util/Log.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>

namespace gs3d::app {

namespace {

std::size_t active_render_index(
    const ViewerKeyboardShortcutContext& context
) {
    return static_cast<std::size_t>(
        std::clamp(
            context.streaming_viewport_index,
            0,
            static_cast<int>(context.viewport_pushes.size()) - 1
        )
    );
}

void synchronize_camera_link_groups(
    const AppState& app_state,
    gs3d::camera::CameraHub& camera_hub
) {
    for (const auto& view : app_state.render_views) {
        camera_hub.set_group(
            view.viewport_index,
            view.camera_linked
                ? 0
                : gs3d::camera::CameraHub::kIndependent
        );
    }
}

} // namespace

void ViewerKeyboardShortcutSystem::process(
    ViewerKeyboardShortcutContext& context
) {
    if (!context.imgui_wants_keyboard &&
        context.window.key_pressed(GLFW_KEY_ESCAPE)) {
        context.window.request_close();
    }

    if (!context.imgui_wants_keyboard) {
        auto& push = context.viewport_pushes[active_render_index(context)];
        if (context.window.key_pressed(GLFW_KEY_EQUAL) ||
            context.window.key_pressed(GLFW_KEY_KP_ADD)) {
            push.point_size = std::min(push.point_size + 0.05f, 10.0f);
        }
        if (context.window.key_pressed(GLFW_KEY_MINUS) ||
            context.window.key_pressed(GLFW_KEY_KP_SUBTRACT)) {
            push.point_size = std::max(push.point_size - 0.05f, 1.0f);
        }
    }

    const bool r_pressed =
        context.keyboard_shortcuts_allowed &&
        (context.window.key_pressed(GLFW_KEY_R) ||
         ImGui::IsKeyPressed(ImGuiKey_R, false));
    if (r_pressed && !r_was_pressed_) {
        context.viewport_cameras
            .controller(context.streaming_viewport_index)
            .clear_orbit_pivot();
        initialize_camera_from_config(
            context.viewport_manager.camera(context.streaming_viewport_index),
            context.config,
            context.bounds
        );
        context.camera_hub.propagate(context.streaming_viewport_index);
        context.tile_selection_dirty = true;
    }
    r_was_pressed_ = r_pressed;

    const bool f_pressed =
        context.keyboard_shortcuts_allowed &&
        (context.window.key_pressed(GLFW_KEY_F) ||
         ImGui::IsKeyPressed(ImGuiKey_F, false));
    if (f_pressed && !f_was_pressed_) {
        const auto focus_index =
            static_cast<std::size_t>(context.streaming_viewport_index);
        if (focus_index < context.selected_focus_points.size() &&
            context.selected_focus_points[focus_index].has_value()) {
            context.viewport_cameras
                .controller(context.streaming_viewport_index)
                .focus_on(
                    context.viewport_manager.camera(
                        context.streaming_viewport_index
                    ),
                    *context.selected_focus_points[focus_index]
                );
            context.camera_hub.propagate(context.streaming_viewport_index);
            context.tile_selection_dirty = true;
            gs3d::util::log::info()
                << "[CAMERA] focused selected point in viewport "
                << context.streaming_viewport_index << '\n';
        } else {
            gs3d::util::log::info()
                << "[CAMERA] focus skipped: no selected point in viewport "
                << context.streaming_viewport_index << '\n';
        }
    }
    f_was_pressed_ = f_pressed;

    const bool tab_held =
        !context.imgui_wants_keyboard &&
        context.window.key_pressed(GLFW_KEY_TAB);
    const bool shift_held =
        context.window.key_pressed(GLFW_KEY_LEFT_SHIFT) ||
        context.window.key_pressed(GLFW_KEY_RIGHT_SHIFT);
    if (!tab_held) {
        tab_was_pressed_ = false;
        shift_tab_was_pressed_ = false;
    } else if (shift_held && !shift_tab_was_pressed_) {
        shift_tab_was_pressed_ = true;
        tab_was_pressed_ = true;
        const auto index = active_render_index(context);
        auto& scene = context.viewport_scene_states[index];
        auto& push = context.viewport_pushes[index];
        auto& height_exaggeration = context.viewport_height_exaggerations[index];
        const auto attribute_count =
            static_cast<std::uint32_t>(context.attributes.size());
        const auto next_index =
            (static_cast<std::uint32_t>(scene.active_height_index) + 1u) %
            attribute_count;
        scene.active_height_index = static_cast<int>(next_index);
        context.attribute_mapping.apply_height_to(
            push,
            context.attributes[next_index],
            height_exaggeration
        );
        gs3d::util::log::info()
            << "[HEIGHT] switched to: "
            << context.attributes[next_index].name << '\n';
    } else if (!tab_was_pressed_) {
        tab_was_pressed_ = true;
        const auto index = active_render_index(context);
        auto& scene = context.viewport_scene_states[index];
        auto& push = context.viewport_pushes[index];
        const auto attribute_count =
            static_cast<std::uint32_t>(context.attributes.size());
        const auto next_index =
            (static_cast<std::uint32_t>(scene.active_attribute_index) + 1u) %
            attribute_count;
        scene.active_attribute_index = static_cast<int>(next_index);
        const auto& attribute = context.attributes[next_index];
        push.color_source = static_cast<std::uint32_t>(attribute.source);
        push.color_min = attribute.min_val;
        push.color_range = attribute.range();
        if (push.color_range <= 0.0f) {
            push.color_range = 1.0f;
        }
        navigation_map_for_view(
            context.app_state,
            static_cast<int>(index)
        ).dirty = true;
        gs3d::util::log::info()
            << "[COLOR] switched to: " << attribute.name << '\n';
    }

    synchronize_camera_link_groups(context.app_state, context.camera_hub);
}

} // namespace gs3d::app
