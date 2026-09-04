#include "ui/AppChrome.hpp"

#include "app/UserPreferences.hpp"
#include "ui/UiFonts.hpp"
#include "ui/IconFont.hpp"
#include "ui/LayoutRegistry.hpp"
#include "ui/PanelRegistry.hpp"
#include "ui/ThemeRegistry.hpp"
#include "ui/UiPalette.hpp"
#include "ui/UiRoot.hpp"
#include "ui/Widgets.hpp"
#include "ui/WorkspaceManager.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

namespace gs3d::ui {

namespace {
constexpr float kStatusInsetX = 9.0f;
ImFont* small_font() { return gs3d::gui::ui_fonts().small; }
ImFont* status_font() { return gs3d::gui::ui_fonts().status; }
ImFont* mono_font() { return gs3d::gui::ui_fonts().mono; }
ImFont* medium_font() { return gs3d::gui::ui_fonts().medium; }

void draw_segment_separator(float ui_scale) {
    ImGui::SameLine(0.0f, 14.0f * ui_scale);
    auto p = ImGui::GetCursorScreenPos();
    float y = p.y + ImGui::GetFontSize() * 0.5f;
    ImGui::GetWindowDrawList()->AddLine(
        {p.x, y - 5.0f * ui_scale}, {p.x, y + 5.0f * ui_scale},
        to_u32(palette::kBorder, 150), 1.0f);
    ImGui::SameLine(0.0f, 14.0f * ui_scale);
}

void push_menu_style(float ui_scale) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10.0f*ui_scale, 8.0f*ui_scale});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10.0f*ui_scale, 9.0f*ui_scale});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8.0f*ui_scale, 5.0f*ui_scale});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f*ui_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, {7.0f*ui_scale, 5.0f*ui_scale});
    ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kText, 255));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, to_u32(palette::kTextDim, 255));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, to_u32(palette::kSurface, 255));
    ImGui::PushStyleColor(ImGuiCol_Header, to_u32(palette::kAccent, 18));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, to_u32(palette::kAccent, 32));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, to_u32(palette::kAccent, 52));
    ImGui::PushStyleColor(ImGuiCol_Border, to_u32(palette::kBorder, 180));
    ImGui::PushStyleColor(ImGuiCol_Separator, to_u32(palette::kBorder, 110));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, to_u32(palette::kAccent, 255));
}
void pop_menu_style() { ImGui::PopStyleColor(9); ImGui::PopStyleVar(6); }

void menu_section(const char* label) {
    if (small_font()) ImGui::PushFont(small_font());
    ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextFaint, 210));
    ImGui::SeparatorText(label);
    ImGui::PopStyleColor();
    if (small_font()) ImGui::PopFont();
}

void menu_hint(const char* text) {
    if (small_font()) ImGui::PushFont(small_font());
    ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextDim, 190));
    ImGui::BulletText("%s", text);
    ImGui::PopStyleColor();
    if (small_font()) ImGui::PopFont();
}

bool icon_button(const char* id, const icons::Glyph& g, const char* tip, bool active=false, bool enabled=true) {
    char buf[5]; icons::utf8(g.codepoint, buf);
    ImGui::BeginDisabled(!enabled);
    bool c = widgets::IconButton(id, buf, tip, active);
    ImGui::EndDisabled();
    return c;
}

bool any_camera_linked(const gs3d::app::AppState& s) {
    for (auto& v : s.render_views) if (v.camera_linked) return true;
    return false;
}
void toggle_all_camera_link(gs3d::app::AppState& s) {
    bool t = !any_camera_linked(s);
    for (auto& v : s.render_views) v.camera_linked = t;
}

struct ShortcutEntry { const char* keys; const char* desc; };
constexpr ShortcutEntry kShortcuts[] = {
    {"Ctrl+O", "打开数据文件"}, {"Ctrl+N", "新建视图"}, {"Ctrl+P", "面板命令面板"},
    {"R", "复位相机"}, {"F", "聚焦选中点"}, {"M", "切换测量模式"},
    {"C", "复制坐标"}, {"Shift+拖拽", "框选统计"}, {"Ctrl+拖拽", "框选拾取"},
    {"?", "快捷键总览"}, {"Esc", "关闭弹层"}, {"导航球点击", "沿轴锁定视角"},
};
constexpr int kShortcutCount = sizeof(kShortcuts)/sizeof(kShortcuts[0]);

void draw_layout_menu(gs3d::app::AppState&, AppChromeResult& r) {
    if (!ImGui::BeginMenu("布局")) return;
    const auto& reg = LayoutRegistry::instance();
    const std::string_view active_id = reg.active_layout_id();
    for (std::size_t i = 0; i < reg.layout_count(); ++i) {
        const auto* l = reg.layout_at(i);
        if (l == nullptr || !l->enabled) continue;
        const bool sel = (l->id == active_id);
        if (ImGui::MenuItem(l->name.c_str(), nullptr, sel) && !sel) {
            r.layout_change_requested = true;
            r.requested_layout_id = l->id;
        }
        if (ImGui::IsItemHovered() && !l->description.empty()) {
            ImGui::SetTooltip("%s", l->description.c_str());
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("重置为默认工作台布局")) {
        r.restore_default_workspace_requested = true;
    }
    ImGui::EndMenu();
}

void draw_theme_menu(gs3d::app::AppState&, AppChromeResult& r) {
    if (!ImGui::BeginMenu("主题颜色")) return;
    const auto& reg = ThemeRegistry::instance();
    const ThemeId active_id = active_theme();
    const std::string_view active_name = theme_tokens(active_id).id;
    for (std::size_t i = 0; i < reg.theme_count(); ++i) {
        const auto* t = reg.theme_at(i);
        if (t == nullptr) continue;
        const bool sel = (t->id != nullptr && t->id == active_name);
        if (ImGui::MenuItem(t->name != nullptr ? t->name : t->id, nullptr, sel) && !sel) {
            r.requested_theme = (i < static_cast<std::size_t>(kThemeCount))
                ? static_cast<ThemeId>(i)
                : ThemeId::kCustom;
            r.theme_change_requested = true;
        }
    }
    ImGui::EndMenu();
}

void draw_panel_menu(gs3d::app::PanelVisibilityState& p) {
    menu_section("面板显示 · 业务");
    for (int i = 0; i < kPanelCount; ++i) {
        auto id = (PanelId)i;
        if (is_debug_panel(id)) continue;
        if (auto* v = panel_visibility(p, id))
            ImGui::MenuItem(panel_name(id).data(), nullptr, v);
    }
    menu_section("面板显示 · 调试");
    for (int i = 0; i < kPanelCount; ++i) {
        auto id = (PanelId)i;
        if (!is_debug_panel(id)) continue;
        if (auto* v = panel_visibility(p, id))
            ImGui::MenuItem(panel_name(id).data(), nullptr, v);
    }
}
} // namespace

void handle_chrome_shortcuts(gs3d::app::AppState& state, gs3d::app::UiActions&) {
    bool typing = ImGui::GetIO().WantTextInput;
    if (!typing && !ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Slash, false))
        state.ui_chrome.shortcut_overlay_open = !state.ui_chrome.shortcut_overlay_open;
    if (!typing && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) {
        state.ui_chrome.panel_palette_open = !state.ui_chrome.panel_palette_open;
        if (state.ui_chrome.panel_palette_open) state.ui_chrome.panel_palette_query[0] = '\0';
    }
}

void draw_top_bar(gs3d::app::AppState& state, gs3d::app::UiActions& actions, float ui_scale, AppChromeResult& result) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8.0f*ui_scale, 13.0f*ui_scale});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {6.0f*ui_scale, 4.0f*ui_scale});
    if (medium_font()) ImGui::PushFont(medium_font());
    ImGui::TextUnformatted("GeoScatter");
    ImGui::SameLine(0.0f, 2.0f*ui_scale);
    ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kAccent, 255));
    ImGui::TextUnformatted("3D");
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 12.0f*ui_scale);
    push_menu_style(ui_scale);

    if (ImGui::BeginMenu("文件")) {
        menu_section("文件操作");
        if (ImGui::MenuItem("打开数据文件…", "Ctrl+O")) actions.open_requested = true;
        if (ImGui::MenuItem("打开 GS3D Bundle 项目…")) actions.open_bundle_requested = true;
        if (ImGui::MenuItem("截图")) actions.screenshot_requested = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("视图")) {
        menu_section("工作区");
        bool has = has_hidden_view(state);
        if (ImGui::MenuItem("新建视图", "Ctrl+N", false, has)) show_first_hidden_view(state);
        if (ImGui::MenuItem("重置为默认工作台布局")) result.restore_default_workspace_requested = true;
        menu_section("界面布局");
        draw_layout_menu(state, result);
        menu_section("色彩主题");
        draw_theme_menu(state, result);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("窗口")) {
        menu_section("工作窗口");
        bool can = has_hidden_view(state);
        if (ImGui::MenuItem("新建工作窗口", nullptr, false, can)) create_workspace_window(state);
        draw_panel_menu(state.panels);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("帮助")) {
        menu_section("帮助与引导");
        if (ImGui::MenuItem("欢迎页")) actions.show_welcome_requested = true;
        if (ImGui::MenuItem("快捷键总览", "?")) state.ui_chrome.shortcut_overlay_open = true;
        menu_section("使用提示");
        menu_hint("视图可作为标签页使用，也可拖到其他显示器。");
        menu_hint("默认相机相互独立，可在顶栏启用联动。");
        ImGui::EndMenu();
    }

    ImGui::SameLine(0.0f, 18.0f*ui_scale);
    bool can_add = has_hidden_view(state);
    if (icon_button("##TO", icons::kFolderOpen, "打开 (Ctrl+O)")) actions.open_requested = true;
    ImGui::SameLine();
    if (icon_button("##TS", icons::kPhotoCamera, "截图")) actions.screenshot_requested = true;
    ImGui::SameLine();
    if (icon_button("##TA", icons::kAdd, "新建视图 (Ctrl+N)", false, can_add)) show_first_hidden_view(state);
    ImGui::SameLine();
    auto& meas = gs3d::app::measurement_for_view(state, state.active_viewport_index);
    bool measuring = meas.measure_mode_active();
    if (icon_button("##TM", icons::kStraighten, "测量 (M)", measuring)) {
        meas.toggle_measure_mode();
        if (!measuring) meas.clear_pending();
    }
    ImGui::SameLine();
    if (icon_button("##TL", icons::kLink, "联动相机", any_camera_linked(state)))
        toggle_all_camera_link(state);
    ImGui::SameLine();
    if (icon_button("##TP", icons::kApps, "面板 (Ctrl+P)")) {
        state.ui_chrome.panel_palette_open = true;
        state.ui_chrome.panel_palette_query[0] = '\0';
    }
    ImGui::SameLine();
    // TIA-111 方向 B：可折叠侧边栏切换按钮
    if (icon_button("##SB", icons::kMenu, "切换侧边栏", state.ui_chrome.sidebar_visible)) {
        state.ui_chrome.sidebar_visible = !state.ui_chrome.sidebar_visible;
    }
    pop_menu_style();
    if (medium_font()) ImGui::PopFont();

    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > 200.0f*ui_scale) {
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 300.0f*ui_scale));
        ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextDim, 190));
        ImGui::TextUnformatted(state.dataset.active_dataset.empty() ? "未加载数据" : state.dataset.active_dataset.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar(2);
}

void draw_status_bar(const gs3d::app::AppState& state, float ui_scale) {
    ImVec2 mn = ImGui::GetWindowPos(), mx(mn.x+ImGui::GetWindowSize().x, mn.y+ImGui::GetWindowSize().y);
    ImGui::GetWindowDrawList()->AddLine(mn, mx, to_u32(palette::kBorder, 56), 1.0f);
    ImGui::SetCursorPosX(kStatusInsetX);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY()+4.0f*ui_scale);
    ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextDim, 200));
    if (status_font()) ImGui::PushFont(status_font());
    char ib[5], vb[96];

    icons::utf8(icons::kSpeed.codepoint, ib);
    std::snprintf(vb, sizeof(vb), "%.1f FPS  %.2f ms", state.status_bar.fps, state.performance.frame_time_ms);
    widgets::IconLabel(ib, vb);
    draw_segment_separator(ui_scale);

    icons::utf8(icons::kPanoramaFishEye.codepoint, ib);
    std::snprintf(vb, sizeof(vb), "%llu 可见点", (unsigned long long)state.status_bar.visible_points);
    widgets::IconLabel(ib, vb);
    draw_segment_separator(ui_scale);

    icons::utf8(icons::kMemory.codepoint, ib);
    std::snprintf(vb, sizeof(vb), "GPU %.1f MB  (%u/%u)", bytes_to_mb(state.status_bar.gpu_memory_bytes), state.status_bar.loaded_tiles, state.status_bar.pending_tiles);
    widgets::IconLabel(ib, vb);
    draw_segment_separator(ui_scale);

    icons::utf8(icons::kCloudDone.codepoint, ib);
    widgets::IconLabel(ib, state.status_bar.ready_state.c_str());

    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > 220.0f*ui_scale) {
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 240.0f*ui_scale));
        if (mono_font()) ImGui::PushFont(mono_font());
        ImGui::TextUnformatted(state.status_bar.camera_position.c_str());
        if (mono_font()) ImGui::PopFont();
        ImGui::SameLine(0.0f, 12.0f*ui_scale);
        ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextFaint, 200));
        ImGui::TextUnformatted(state.status_bar.crs.c_str());
        ImGui::PopStyleColor();
    }
    if (status_font()) ImGui::PopFont();
    ImGui::PopStyleColor();
}

void draw_shortcut_overlay(gs3d::app::AppState& state, float ui_scale) {
    if (!state.ui_chrome.shortcut_overlay_open) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f,0.5f});
    ImGui::SetNextWindowSize({520.0f*ui_scale, 0.0f}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f*ui_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {18.0f, 14.0f});
    bool open = true;
    if (ImGui::Begin("快捷键总览###ShortcutOverlay", &open, ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove)) {
        draw_panel_section_label("键盘快捷键");
        if (ImGui::BeginTable("##ST", 2, ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 130.0f*ui_scale);
            ImGui::TableSetupColumn("d", ImGuiTableColumnFlags_WidthStretch);
            for (int i = 0; i < kShortcutCount; ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (mono_font()) ImGui::PushFont(mono_font());
                ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kAccent, 230));
                ImGui::TextUnformatted(kShortcuts[i].keys);
                ImGui::PopStyleColor();
                if (mono_font()) ImGui::PopFont();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(kShortcuts[i].desc);
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextFaint, 210));
        if (small_font()) ImGui::PushFont(small_font());
        ImGui::TextUnformatted("按 ? 或 Esc 关闭");
        if (small_font()) ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    if (!open || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput))
        state.ui_chrome.shortcut_overlay_open = false;
}

void draw_panel_command_palette(gs3d::app::AppState& state, float ui_scale) {
    if (!state.ui_chrome.panel_palette_open) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f,0.35f});
    ImGui::SetNextWindowSize({420.0f*ui_scale, 0.0f}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f*ui_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.0f, 12.0f});
    if (ImGui::Begin("###PanelPalette", nullptr, ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoTitleBar)) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SetKeyboardFocusHere(0);
        bool enter = ImGui::InputTextWithHint("##PPQ", "搜索面板…", state.ui_chrome.panel_palette_query.data(),
            state.ui_chrome.panel_palette_query.size(), ImGuiInputTextFlags_AutoSelectAll|ImGuiInputTextFlags_EnterReturnsTrue);
        std::string_view q(state.ui_chrome.panel_palette_query.data());
        if (ImGui::BeginTable("##PPL", 2, ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_NoPadOuterX|ImGuiTableFlags_NoPadInnerX)) {
            ImGui::TableSetupColumn("n", ImGuiTableColumnFlags_WidthFixed, 90.0f*ui_scale);
            ImGui::TableSetupColumn("d", ImGuiTableColumnFlags_WidthStretch);
            int first = -1;
            for (int i = 0; i < kPanelCount; ++i) {
                auto id = (PanelId)i;
                if (!panel_matches_query(id, q)) continue;
                if (first < 0) first = i;
                bool* vis = panel_visibility(state.panels, id);
                if (!vis) continue;
                auto& d = kPanelRegistry[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(d.name, *vis)) toggle_panel(state.panels, id);
                ImGui::TableSetColumnIndex(1);
                ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextDim, 200));
                if (small_font()) ImGui::PushFont(small_font());
                ImGui::TextUnformatted(d.description);
                if (small_font()) ImGui::PopFont();
                ImGui::PopStyleColor();
                if (ImGui::IsItemClicked()) toggle_panel(state.panels, id);
            }
            if (first >= 0 && enter) toggle_panel(state.panels, (PanelId)first);
            ImGui::EndTable();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    if ((ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput) ||
        (ImGui::IsKeyPressed(ImGuiKey_P, false) && ImGui::GetIO().KeyCtrl)) {
        state.ui_chrome.panel_palette_open = false;
        state.ui_chrome.panel_palette_query[0] = '\0';
    }
}

void persist_ui_preferences(const gs3d::app::AppState&, ThemeId theme) {
    gs3d::app::UiPreferences p;
    p.theme = theme_tokens(theme).id;
    p.layout = std::string(LayoutRegistry::instance().active_layout_id());
    gs3d::app::save_ui_preferences(p);
}

} // namespace gs3d::ui
