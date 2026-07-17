#include "app/ViewportInteractionState.hpp"
#include "ui/ViewportCanvas.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

gs3d::app::ViewportFrameCmd rotate_frame(
    const int index,
    const bool rotate,
    const float mouse_x,
    const float mouse_y
)
{
    gs3d::app::ViewportFrameCmd frame;
    frame.index = index;
    frame.rotate = rotate;
    frame.mouse_local_x = mouse_x;
    frame.mouse_local_y = mouse_y;
    return frame;
}

gs3d::camera::CameraInput rotation_input(
    const float delta_x,
    const float delta_y
)
{
    gs3d::camera::CameraInput input;
    input.rotate = true;
    input.delta_x = delta_x;
    input.delta_y = delta_y;
    return input;
}

} // namespace

TEST_CASE(
    "Viewport rotation ignores clicks and sub-threshold drags",
    "[viewport_interaction][rotation]"
) {
    gs3d::app::ViewportInteractionState state(1);

    auto press = rotation_input(4.0f, -2.0f);
    state.apply_rotation_gate(rotate_frame(0, true, 100.0f, 50.0f), press);
    CHECK_FALSE(press.rotate_begin);
    CHECK(press.delta_x == 0.0f);
    CHECK(press.delta_y == 0.0f);

    auto small_drag = rotation_input(3.0f, 1.0f);
    state.apply_rotation_gate(
        rotate_frame(0, true, 103.0f, 53.0f),
        small_drag
    );
    CHECK_FALSE(small_drag.rotate_begin);
    CHECK(small_drag.delta_x == 0.0f);
    CHECK(small_drag.delta_y == 0.0f);
}

TEST_CASE(
    "Viewport rotation activates once per drag and resets after release",
    "[viewport_interaction][rotation]"
) {
    gs3d::app::ViewportInteractionState state(1);

    auto press = rotation_input(1.0f, 1.0f);
    state.apply_rotation_gate(rotate_frame(0, true, 10.0f, 10.0f), press);

    auto activation = rotation_input(5.0f, 0.0f);
    state.apply_rotation_gate(
        rotate_frame(0, true, 15.0f, 10.0f),
        activation
    );
    CHECK(activation.rotate_begin);
    CHECK(activation.delta_x == 5.0f);
    CHECK(activation.delta_y == 0.0f);

    auto continued_drag = rotation_input(2.0f, -1.0f);
    state.apply_rotation_gate(
        rotate_frame(0, true, 17.0f, 9.0f),
        continued_drag
    );
    CHECK_FALSE(continued_drag.rotate_begin);
    CHECK(continued_drag.delta_x == 2.0f);
    CHECK(continued_drag.delta_y == -1.0f);

    auto release = rotation_input(0.0f, 0.0f);
    release.rotate = false;
    state.apply_rotation_gate(rotate_frame(0, false, 17.0f, 9.0f), release);

    auto second_press = rotation_input(7.0f, 0.0f);
    state.apply_rotation_gate(
        rotate_frame(0, true, 17.0f, 9.0f),
        second_press
    );
    CHECK_FALSE(second_press.rotate_begin);
    CHECK(second_press.delta_x == 0.0f);
    CHECK(second_press.delta_y == 0.0f);
}

TEST_CASE(
    "Viewport rotation histories are independent and reject invalid indices",
    "[viewport_interaction][rotation]"
) {
    gs3d::app::ViewportInteractionState state(2);
    CHECK(state.viewport_count() == 2);

    auto first_press = rotation_input(1.0f, 1.0f);
    state.apply_rotation_gate(rotate_frame(0, true, 0.0f, 0.0f), first_press);

    auto second_view = rotation_input(6.0f, 0.0f);
    state.apply_rotation_gate(rotate_frame(1, true, 6.0f, 0.0f), second_view);
    CHECK_FALSE(second_view.rotate_begin);
    CHECK(second_view.delta_x == 0.0f);

    auto invalid = rotation_input(4.0f, -3.0f);
    state.apply_rotation_gate(rotate_frame(2, true, 20.0f, 20.0f), invalid);
    CHECK_FALSE(invalid.rotate_begin);
    CHECK(invalid.delta_x == 4.0f);
    CHECK(invalid.delta_y == -3.0f);
}

TEST_CASE(
    "Viewport input is routed only through the focused platform window",
    "[viewport_interaction][window_focus]"
) {
    const auto foreground = gs3d::ui::resolve_viewport_input_routing(
        true,
        true,
        true
    );
    CHECK(foreground.hovered);
    CHECK(foreground.active);

    const auto background = gs3d::ui::resolve_viewport_input_routing(
        true,
        true,
        false
    );
    CHECK_FALSE(background.hovered);
    CHECK_FALSE(background.active);
}
