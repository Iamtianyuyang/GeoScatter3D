#pragma once

#include "app/AppState.hpp"
#include "camera/Camera.hpp"

namespace gs3d::app {

constexpr int kMaxViewportCount = 4;

void compute_gizmo_axes(
    gs3d::app::RenderViewState& view,
    const gs3d::camera::Camera& camera
);

void compute_axis_overlay(
    gs3d::app::RenderViewState& view,
    const gs3d::camera::CameraBounds& bounds,
    const gs3d::camera::Camera& camera,
    double origin_x,
    double origin_y,
    double origin_z,
    float  z_label_mult        = 1.0f,
    float  z_label_offset      = 0.0f,
    bool   z_axis_add_origin_z = false
);

void compute_map_axis_overlay(
    gs3d::app::RenderViewState& view,
    const gs3d::camera::Camera& camera,
    double origin_x,
    double origin_y
);

} // namespace gs3d::app
