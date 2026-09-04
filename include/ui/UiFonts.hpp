#pragma once

namespace gs3d::ui {

struct UiFonts {
    void* regular = nullptr;
    void* medium = nullptr;
    void* bold = nullptr;
    void* small = nullptr;
    void* panel_title = nullptr;
    void* axis = nullptr;
    void* status = nullptr;
    void* icons = nullptr;
    void* mono = nullptr;
    // 基于屏幕 PPI 计算的 UI 缩放因子。1080p => 1.0。
    float ui_scale = 1.0f;
};

[[nodiscard]] const UiFonts& ui_fonts() noexcept;

} // namespace gs3d::ui

// 向后兼容命名空间别名
namespace gs3d::gui {
    using UiFonts = gs3d::ui::UiFonts;
    [[nodiscard]] inline const UiFonts& ui_fonts() noexcept {
        return gs3d::ui::ui_fonts();
    }
} // namespace gs3d::gui
