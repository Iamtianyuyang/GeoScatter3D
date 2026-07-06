#pragma once

#include "imgui.h"

namespace gs3d::gui {

struct UiFonts {
    ImFont* regular = nullptr;
    ImFont* small = nullptr;
    ImFont* panel_title = nullptr;
    ImFont* axis = nullptr;
    ImFont* status = nullptr;
    // UI-only scale derived from the monitor's resolution (NOT from
    // glfwGetWindowContentScale). 1080p => 1.0, clamped to [1.0, kMaxUiScale].
    // Used solely for ImGui appearance (font sizes + style metrics). It must
    // never feed the Vulkan render-size chain (swapchain extent, offscreen
    // framebuffer, viewport/scissor, mouse-pick mapping, io.DisplayFramebufferScale).
    float ui_scale = 1.0f;
};

[[nodiscard]] const UiFonts& ui_fonts() noexcept;

} // namespace gs3d::gui
