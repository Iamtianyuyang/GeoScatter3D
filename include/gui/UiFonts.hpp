#pragma once

#include "imgui.h"

namespace gs3d::gui {

struct UiFonts {
    ImFont* regular = nullptr;
    ImFont* small = nullptr;
    ImFont* panel_title = nullptr;
    ImFont* axis = nullptr;
    ImFont* status = nullptr;
};

[[nodiscard]] const UiFonts& ui_fonts() noexcept;

} // namespace gs3d::gui
