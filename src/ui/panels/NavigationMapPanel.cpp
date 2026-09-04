#include "ui/NavigationMapPanel.hpp"
#include "ui/UiPalette.hpp"

#include "imgui.h"

namespace gs3d::ui {

namespace {
constexpr const char* kNavigationMapWindowName = "导航图###NavigationMap";
} // namespace

void draw_navigation_map_content(
    gs3d::app::AppState& state,
    gs3d::app::NavigationMapState* navigation_map
)
{
    auto& nm = navigation_map != nullptr
        ? *navigation_map
        : gs3d::app::navigation_map_for_view(
            state,
            state.active_viewport_index
        );

    // 缩略图区域：在内容区中水平与垂直居中，自适应正方形
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float avail_h = ImGui::GetContentRegionAvail().y;
    const float pad = 6.0f;
    const float size = std::max(16.0f, std::min(avail_w - pad * 2.0f, avail_h - pad * 2.0f));
    if (size < 16.0f) {
        return;
    }

    const float offset_x = (avail_w - size) * 0.5f;
    const float offset_y = (avail_h - size) * 0.5f;
    const ImVec2 start_pos = ImGui::GetCursorScreenPos();
    const ImVec2 img_min = {start_pos.x + offset_x, start_pos.y + offset_y};
    const ImVec2 img_max = {img_min.x + size, img_min.y + size};

    // 背景填充（缩略图未就绪时显示暗色占位）
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        img_min,
        img_max,
        to_u32(palette::kFrame, 240),
        4.0f
    );

    if (nm.valid && nm.texture_descriptor != kNullTextureHandle) {
        // 缩略图纹理保持宽高比
        ImVec2 uv0{0.0f, 0.0f};
        ImVec2 uv1{1.0f, 1.0f};
        dl->AddImage(
            static_cast<ImTextureID>(nm.texture_descriptor),
            img_min,
            img_max,
            uv0,
            uv1
        );
    }

    // 视野框叠加（带有微光透光与高亮边框）
    if (nm.view_rect_valid) {
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
        dl->AddRectFilled(
            rect_min,
            rect_max,
            to_u32(palette::kAccent, 45),
            1.0f
        );
        dl->AddRect(
            rect_min,
            rect_max,
            to_u32(palette::kAccent, 240),
            1.0f,
            0,
            1.5f
        );
    }

    // 卡片精致外边框
    dl->AddRect(
        img_min,
        img_max,
        to_u32(palette::kBorder, 130),
        4.0f,
        0,
        1.0f
    );

    ImGui::Dummy(ImVec2(avail_w, avail_h));

}

void draw_navigation_map(
    gs3d::app::AppState& state,
    const char* window_name,
    bool* open,
    gs3d::app::NavigationMapState* navigation_map
)
{
    const bool use_default_window = window_name == nullptr;
    if (use_default_window && !state.panels.navigation_map) {
        return;
    }
    if (window_name == nullptr) {
        window_name = kNavigationMapWindowName;
    }
    if (open == nullptr && use_default_window) {
        open = &state.panels.navigation_map;
    }

    ImGui::SetNextWindowSize(ImVec2(240.0f, 260.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(window_name, open)) {
        draw_navigation_map_content(state, navigation_map);
    }
    ImGui::End();
}

} // namespace gs3d::ui
