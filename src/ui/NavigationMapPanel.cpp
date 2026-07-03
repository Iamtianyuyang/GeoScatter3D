#include "ui/NavigationMapPanel.hpp"

#include "imgui.h"

namespace gs3d::ui {

namespace {
constexpr const char* kNavigationMapWindowName = "导航图###NavigationMap";
} // namespace

void draw_navigation_map(gs3d::app::AppState& state)
{
    if (!state.panels.navigation_map) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(240.0f, 260.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(
            kNavigationMapWindowName,
            &state.panels.navigation_map
        )) {
        ImGui::End();
        return;
    }

    auto& nm = state.navigation_map;

    // 缩略图区域：尽量撑满内容区，保持正方形
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float avail_h = ImGui::GetContentRegionAvail().y;
    const float size = std::min(avail_w, avail_h);
    if (size < 16.0f) {
        ImGui::End();
        return;
    }

    const ImVec2 img_min = ImGui::GetCursorScreenPos();
    const ImVec2 img_max = {img_min.x + size, img_min.y + size};

    // 背景填充（缩略图未就绪时显示暗色占位）
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (nm.valid && nm.texture_descriptor != VK_NULL_HANDLE) {
        // 缩略图纹理内部按 bbox 宽高比渲染，显示时用 ImGui Image 的
        // UV 范围保持宽高比，letterbox 两侧自动为 clear color。
        const float tex_aspect = nm.tex_w / nm.tex_h;
        const float img_aspect = size / size;  // 1.0 (square display)
        ImVec2 uv0{0.0f, 0.0f};
        ImVec2 uv1{1.0f, 1.0f};
        dl->AddImage(
            static_cast<ImTextureID>(
                reinterpret_cast<ImU64>(nm.texture_descriptor)),
            img_min,
            img_max,
            uv0,
            uv1
        );
    } else {
        dl->AddRectFilled(
            img_min,
            img_max,
            IM_COL32(32, 34, 40, 255)
        );
    }

    // 视野框叠加
    if (nm.view_rect_valid) {
        // view_rect 在纹理像素空间 [0, tex_w]×[0, tex_h]，
        // 纹理北在上(tex_y=0)、南在下(tex_y=tex_h)，和 ImGui
        // AddImage 的映射一致，直接缩放，不需要额外 Y 翻转。
        const float sx = size / nm.tex_w;
        const float sy = size / nm.tex_h;
        const ImVec2 rect_min{
            img_min.x + nm.view_rect_min_x * sx,
            img_min.y + nm.view_rect_min_y * sy
        };
        const ImVec2 rect_max{
            img_min.x + nm.view_rect_max_x * sx,
            img_min.y + nm.view_rect_max_y * sy
        };
        dl->AddRect(
            rect_min,
            rect_max,
            IM_COL32(255, 80, 80, 220),
            0.0f,
            0,
            2.0f
        );
    }

    ImGui::End();
}

} // namespace gs3d::ui
