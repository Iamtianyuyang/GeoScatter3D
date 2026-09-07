#include "ui/UiFonts.hpp"

namespace gs3d::ui {

namespace {
// ImGui 前端已移除：字体指针不再加载（全部保持 nullptr），
// ui_scale 固定为 1.0（布局缩放后续可由窗口 DPI 重新接入）。
UiFonts g_ui_fonts{};
} // namespace

const UiFonts& ui_fonts() noexcept
{
    return g_ui_fonts;
}

} // namespace gs3d::ui
