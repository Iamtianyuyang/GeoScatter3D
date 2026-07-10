#pragma once

#include "imgui.h"

#include <cstddef>

namespace gs3d::ui::widgets {

/*
 * 主题化自绘控件库（对应三套主题设计稿的组件板）。
 *
 * 所有控件从 palette:: 取线性空间颜色、从 ImGuiStyle 取圆角，主题切换
 * 自动跟随，无需调用方干预。交互语义与同名 ImGui 控件一致（返回值 =
 * 被点击 / 值已修改），可直接替换调用点。
 *
 * 变体语义：
 *   Primary   实心强调色 + 底部投影 —— 每个面板至多一个主行动
 *   Secondary 描边中性底 —— 常规动作
 *   Ghost     无底强调色文字 —— 低优先级动作
 *   Danger    红描边透明底 —— 破坏性动作（删除/清空）
 */

enum class ButtonVariant {
    kPrimary,
    kSecondary,
    kGhost,
    kDanger,
};

// size.x/y 为 0 时按文字自适应。small=true 用于工具条内的紧凑按钮。
bool Button(
    const char* label,
    ButtonVariant variant = ButtonVariant::kSecondary,
    ImVec2 size = ImVec2(0.0f, 0.0f),
    bool small = false
);

// 工具栏胶囊按钮（“+ 视图 / 截图 / 测量”）。active 画选中态。
bool Chip(const char* label, bool active = false);

// 自绘复选框：圆角方块 + accent 填充对勾。
bool Checkbox(const char* label, bool* v);

// 细轨道滑块：4px 轨道 + accent 填充 + 主题化拇指，右侧显示数值。
// 宽度取 CalcItemWidth()。拖动期间每帧返回 true（值有变化时）。
bool SliderFloat(
    const char* label,
    float* v,
    float v_min,
    float v_max,
    const char* format = "%.1f"
);

// 输入框（包装 ImGui::DragFloat，保留拖动 + Ctrl+点击键入），
// 外观改为字段样式：主题底色、hover 强调描边、激活焦点环。
bool DragFloat(
    const char* label,
    float* v,
    float v_speed,
    float v_min,
    float v_max,
    const char* format = "%.3f",
    ImGuiSliderFlags flags = 0
);

// 文本输入框：与数值字段共享 hover 描边和键盘焦点环。
bool InputText(
    const char* label,
    char* buffer,
    std::size_t buffer_size,
    ImGuiInputTextFlags flags = 0
);
bool InputTextWithHint(
    const char* label,
    const char* hint,
    char* buffer,
    std::size_t buffer_size,
    ImGuiInputTextFlags flags = 0
);

// 下拉框（包装 BeginCombo，去掉原生箭头，画 accent 三角 + 状态描边）。
// 与 ImGui::BeginCombo/EndCombo 相同用法：返回 true 时须调 EndCombo()。
bool BeginCombo(const char* label, const char* preview);
void EndCombo();

// 分段控件（替代 RadioButton 组）。返回 true 时 *current 已更新。
bool Segmented(
    const char* id,
    const char* const items[],
    int count,
    int* current
);

// 6px 进度条。fraction 取 0-1，超界自动截断。宽度取 CalcItemWidth()。
void Meter(float fraction);

} // namespace gs3d::ui::widgets
