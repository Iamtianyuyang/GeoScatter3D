#include "ui/UiRoot.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace gs3d::ui {

namespace {

constexpr const char* kHostWindowName =
    "GeoScatter3D 工作台###GeoScatter3DWorkspace";
constexpr const char* kDatasetWindowName =
    "项目###DatasetPanel";
constexpr const char* kRenderSettingsWindowName =
    "属性###RenderSettings";
constexpr const char* kDebugLogWindowName =
    "日志###DebugLog";
constexpr const char* kTileInspectorWindowName =
    "瓦片###TileInspector";
constexpr const char* kLodViewWindowName =
    "细节层级###LodView";
constexpr const char* kPerformanceWindowName =
    "性能###Performance";

std::string render_view_window_name(int index)
{
    return "视图 " + std::to_string(index + 1) +
        "###RenderView" + std::to_string(index);
}

float bytes_to_mb(std::uint64_t bytes)
{
    return static_cast<float>(bytes) / (1024.0f * 1024.0f);
}

std::uint32_t visible_view_signature(
    const gs3d::app::AppState& state
) {
    std::uint32_t signature = 0;
    for (const auto& view : state.render_views) {
        if (view.visible &&
            view.viewport_index >= 0 &&
            view.viewport_index < 24) {
            signature |=
                1u << static_cast<std::uint32_t>(view.viewport_index);
        }
    }
    return signature;
}

bool show_first_hidden_view(gs3d::app::AppState& state)
{
    const auto hidden = std::find_if(
        state.render_views.begin(),
        state.render_views.end(),
        [](const auto& view) {
            return !view.visible;
        }
    );
    if (hidden == state.render_views.end()) {
        return false;
    }

    hidden->visible = true;
    hidden->render_requested = false;
    return true;
}

void draw_mock_viewport(const ImVec2& min, const ImVec2& max)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        min,
        max,
        IM_COL32(20, 22, 25, 255)
    );

    const float width = max.x - min.x;
    const float height = max.y - min.y;
    for (int i = 0; i < 120; ++i) {
        const float x =
            static_cast<float>((i * 37) % 100) / 100.0f;
        const float y =
            static_cast<float>((i * 53) % 100) / 100.0f;
        const float depth =
            static_cast<float>((i * 17) % 100) / 100.0f;
        draw_list->AddCircleFilled(
            {
                min.x + width * (0.18f + x * 0.64f),
                min.y + height * (0.22f + y * 0.52f) -
                    depth * 28.0f
            },
            1.2f + depth * 1.4f,
            IM_COL32(
                72 + static_cast<int>(depth * 100.0f),
                130 + static_cast<int>(depth * 70.0f),
                215,
                220
            )
        );
    }
}

void draw_orientation_gizmo(const ImVec2& min, const ImVec2& max)
{
    (void)min;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 origin{max.x - 42.0f, max.y - 42.0f};
    draw_list->AddCircleFilled(origin, 17.0f, IM_COL32(18, 20, 23, 210));
    draw_list->AddLine(
        origin,
        {origin.x + 14.0f, origin.y},
        IM_COL32(225, 92, 92, 255),
        2.0f
    );
    draw_list->AddLine(
        origin,
        {origin.x, origin.y - 14.0f},
        IM_COL32(91, 204, 122, 255),
        2.0f
    );
    draw_list->AddLine(
        origin,
        {origin.x - 10.0f, origin.y + 10.0f},
        IM_COL32(81, 141, 230, 255),
        2.0f
    );
}

void draw_scale_bar(
    const ImVec2& min,
    const ImVec2& max,
    const char* label
) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float y = max.y - 20.0f;
    const float x0 = min.x + 14.0f;
    const float x1 = x0 + 88.0f;
    draw_list->AddLine(
        {x0, y},
        {x1, y},
        IM_COL32(225, 225, 225, 210),
        2.0f
    );
    draw_list->AddLine(
        {x0, y - 5.0f},
        {x0, y + 1.0f},
        IM_COL32(225, 225, 225, 210),
        2.0f
    );
    draw_list->AddLine(
        {x1, y - 5.0f},
        {x1, y + 1.0f},
        IM_COL32(225, 225, 225, 210),
        2.0f
    );
    draw_list->AddText(
        {x0, y - 20.0f},
        IM_COL32(225, 225, 225, 220),
        label
    );
}

void draw_viewport_overlay(
    const gs3d::app::RenderViewState& view,
    const ImVec2& min,
    const ImVec2& max
) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    char text[160];
    std::snprintf(
        text,
        sizeof(text),
        "%llu 点  |  %.2f ms",
        static_cast<unsigned long long>(view.points_visible),
        static_cast<double>(view.frame_time_ms)
    );

    const ImVec2 text_size = ImGui::CalcTextSize(text);
    const ImVec2 box_min{min.x + 10.0f, min.y + 10.0f};
    const ImVec2 box_max{
        box_min.x + text_size.x + 16.0f,
        box_min.y + text_size.y + 10.0f
    };
    draw_list->AddRectFilled(
        box_min,
        box_max,
        IM_COL32(15, 16, 18, 190),
        3.0f
    );
    draw_list->AddText(
        {box_min.x + 8.0f, box_min.y + 5.0f},
        IM_COL32(215, 217, 220, 255),
        text
    );

    draw_scale_bar(min, max, view.scale.c_str());
    draw_orientation_gizmo(min, max);
}

void draw_viewport_window(
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions
) {
    view.render_requested = false;
    const auto window_name =
        render_view_window_name(view.viewport_index);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::SetNextWindowSize(
        ImVec2(760.0f, 520.0f),
        ImGuiCond_FirstUseEver
    );
    const bool content_visible =
        ImGui::Begin(window_name.c_str(), &view.visible, flags);
    if (!content_visible) {
        ImGui::End();
        return;
    }

    const auto* window_viewport = ImGui::GetWindowViewport();
    view.detached =
        !ImGui::IsWindowDocked() ||
        (window_viewport != nullptr &&
         window_viewport->ID != ImGui::GetMainViewport()->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
    ImGui::TextDisabled(
        view.detached ? "独立窗口" : "工作区"
    );
    ImGui::SameLine();
    if (ImGui::SmallButton("适配")) {
        actions.reset_camera_index = view.viewport_index;
    }
    ImGui::SameLine();
    ImGui::Checkbox("联动相机", &view.camera_linked);
    ImGui::SameLine();
    ImGui::TextDisabled(
        "左键旋转  右键/中键平移  滚轮缩放"
    );
    ImGui::PopStyleVar();
    ImGui::Separator();

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(1.0f, available.x);
    available.y = std::max(1.0f, available.y);

    ImGui::InvisibleButton(
        "##ViewportCanvas",
        available,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle
    );
    const ImVec2 canvas_min = ImGui::GetItemRectMin();
    const ImVec2 canvas_max = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    if (view.show_live_image &&
        view.descriptor != VK_NULL_HANDLE) {
        ImGui::GetWindowDrawList()->AddImage(
            static_cast<ImTextureID>(
                reinterpret_cast<ImU64>(view.descriptor)
            ),
            canvas_min,
            canvas_max
        );
    } else {
        draw_mock_viewport(canvas_min, canvas_max);
    }

    draw_viewport_overlay(view, canvas_min, canvas_max);
    if (hovered || active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    const ImGuiIO& io = ImGui::GetIO();
    gs3d::app::ViewportFrameCmd frame;
    frame.index = view.viewport_index;
    frame.hovered = hovered;
    frame.active = active;
    frame.width = static_cast<std::uint32_t>(available.x);
    frame.height = static_cast<std::uint32_t>(available.y);
    frame.mouse_delta_x = active ? io.MouseDelta.x : 0.0f;
    frame.mouse_delta_y = active ? io.MouseDelta.y : 0.0f;
    frame.mouse_wheel = hovered ? io.MouseWheel : 0.0f;
    frame.rotate = active && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    frame.pan =
        active &&
        (ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
         ImGui::IsMouseDown(ImGuiMouseButton_Middle));
    actions.viewport_frames.push_back(frame);
    view.render_requested = view.visible;

    ImGui::End();
}

void draw_dataset_panel(gs3d::app::AppState& state)
{
    if (!state.panels.dataset) {
        return;
    }

    if (ImGui::Begin(kDatasetWindowName, &state.panels.dataset)) {
        ImGui::TextUnformatted(state.dataset.active_dataset.c_str());
        ImGui::TextDisabled(
            "%llu 点  |  %s",
            static_cast<unsigned long long>(state.dataset.point_count),
            state.dataset.file_size.c_str()
        );
        ImGui::Separator();

        ImGui::InputTextWithHint(
            "##DatasetSearch",
            "筛选项目",
            state.dataset.search_text.data(),
            state.dataset.search_text.size()
        );

        if (ImGui::CollapsingHeader(
                "场景",
                ImGuiTreeNodeFlags_DefaultOpen
            )) {
            for (const auto& item : state.dataset.dataset_tree) {
                ImGui::Selectable(item.c_str(), false);
            }
        }

        if (ImGui::CollapsingHeader(
                "属性",
                ImGuiTreeNodeFlags_DefaultOpen
            )) {
            for (const auto& attribute : state.dataset.attributes) {
                ImGui::BulletText("%s", attribute.c_str());
            }
        }

        if (ImGui::CollapsingHeader("文件信息")) {
            ImGui::TextWrapped("路径：%s", state.dataset.path.c_str());
            ImGui::Text("格式：%s", state.dataset.format.c_str());
            ImGui::TextWrapped(
                "包围盒：%s",
                state.dataset.bounding_box.c_str()
            );
        }
    }
    ImGui::End();
}

void draw_render_settings(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions
) {
    if (!state.panels.render_settings) {
        return;
    }

    if (ImGui::Begin(
            kRenderSettingsWindowName,
            &state.panels.render_settings
        )) {
        ImGui::TextDisabled("点云外观");
        ImGui::Separator();

        float point_size = state.render_settings.point_size;
        if (ImGui::SliderFloat(
                "点大小",
                &point_size,
                1.0f,
                10.0f,
                "%.1f"
            )) {
            state.render_settings.point_size = point_size;
            actions.point_size_changed = true;
            actions.point_size = point_size;
        }

        const auto& color_options =
            state.render_settings.color_by_options;
        const char* preview = "无";
        if (!color_options.empty()) {
            const int preview_index = std::clamp(
                state.render_settings.color_by_index,
                0,
                static_cast<int>(color_options.size()) - 1
            );
            preview =
                color_options[
                    static_cast<std::size_t>(preview_index)
                ].c_str();
        }
        if (ImGui::BeginCombo("着色", preview)) {
            for (std::size_t i = 0; i < color_options.size(); ++i) {
                const bool selected =
                    static_cast<int>(i) ==
                    state.render_settings.color_by_index;
                if (ImGui::Selectable(
                        color_options[i].c_str(),
                        selected
                    )) {
                    state.render_settings.color_by_index =
                        static_cast<int>(i);
                    actions.color_by_changed = true;
                    actions.color_by_index = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TextDisabled("颜色映射");
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton(
            "##ColorMapPreview",
            ImVec2(width, 18.0f)
        );
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
            start,
            {start.x + width, start.y + 18.0f},
            IM_COL32(60, 105, 215, 255),
            IM_COL32(55, 190, 175, 255),
            IM_COL32(235, 190, 75, 255),
            IM_COL32(218, 82, 76, 255)
        );

        ImGui::Spacing();
        ImGui::TextDisabled("流式加载");
        ImGui::Separator();
        ImGui::Text(
            "GPU 瓦片  %s",
            state.render_settings.cache_usage.c_str()
        );
        ImGui::Text(
            "CPU 缓存  %s",
            state.render_settings.cpu_cache_usage.c_str()
        );
        ImGui::Text(
            "缓存命中  %.1f%%",
            state.render_settings.cache_hit_rate
        );
        if (ImGui::Button("清空缓存")) {
            actions.clear_cache_requested = true;
        }
    }
    ImGui::End();
}

void draw_auxiliary_panels(gs3d::app::AppState& state)
{
    if (state.panels.performance) {
        if (ImGui::Begin(
                kPerformanceWindowName,
                &state.panels.performance
            )) {
            ImGui::Text("帧率        %.1f", state.performance.fps);
            ImGui::Text(
                "帧耗时      %.2f ms",
                state.performance.frame_time_ms
            );
            ImGui::Text(
                "可见点数    %llu",
                static_cast<unsigned long long>(
                    state.performance.visible_points
                )
            );
            ImGui::Text(
                "GPU 显存    %.1f MB",
                bytes_to_mb(state.performance.gpu_memory_bytes)
            );
            ImGui::Text(
                "瓦片        %u 已加载 / %u 等待",
                state.performance.loaded_tiles,
                state.performance.pending_tiles
            );
        }
        ImGui::End();
    }

    if (state.panels.debug_log) {
        if (ImGui::Begin(kDebugLogWindowName, &state.panels.debug_log)) {
            for (const auto& line : state.debug_log.lines) {
                ImGui::TextUnformatted(line.c_str());
            }
        }
        ImGui::End();
    }

    if (state.panels.tile_inspector) {
        if (ImGui::Begin(
                kTileInspectorWindowName,
                &state.panels.tile_inspector
            )) {
            ImGui::Text(
                "已加载瓦片  %u",
                state.render_settings.loaded_tiles
            );
            ImGui::Text(
                "等待瓦片    %u",
                state.render_settings.pending_tiles
            );
            ImGui::Text(
                "GPU 驻留    %s",
                state.render_settings.cache_usage.c_str()
            );
            ImGui::Text(
                "CPU 缓存    %s",
                state.render_settings.cpu_cache_usage.c_str()
            );
        }
        ImGui::End();
    }

    if (state.panels.lod_view) {
        if (ImGui::Begin(kLodViewWindowName, &state.panels.lod_view)) {
            ImGui::Text(
                "模式        %s",
                state.performance.lod_mode.c_str()
            );
            ImGui::Text(
                "目标帧率    %.0f",
                state.render_settings.target_fps
            );
        }
        ImGui::End();
    }
}

} // namespace

void UiRoot::build_default_layout(const gs3d::app::AppState& state)
{
    const std::uint32_t signature = visible_view_signature(state);
    if (dock_layout_initialized_ &&
        dock_layout_signature_ == signature) {
        return;
    }

    const ImGuiID dockspace_id =
        ImGui::GetID("GeoScatter3D.DockSpace");
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(
        dockspace_id,
        ImGuiDockNodeFlags_DockSpace
    );
    ImGui::DockBuilderSetNodeSize(
        dockspace_id,
        ImGui::GetMainViewport()->WorkSize
    );

    ImGuiID center_id = dockspace_id;
    const ImGuiID left_id = ImGui::DockBuilderSplitNode(
        center_id,
        ImGuiDir_Left,
        0.19f,
        nullptr,
        &center_id
    );
    const ImGuiID right_id = ImGui::DockBuilderSplitNode(
        center_id,
        ImGuiDir_Right,
        0.24f,
        nullptr,
        &center_id
    );

    ImGui::DockBuilderDockWindow(kDatasetWindowName, left_id);
    ImGui::DockBuilderDockWindow(kTileInspectorWindowName, left_id);
    ImGui::DockBuilderDockWindow(kLodViewWindowName, left_id);
    ImGui::DockBuilderDockWindow(kRenderSettingsWindowName, right_id);
    ImGui::DockBuilderDockWindow(kPerformanceWindowName, right_id);
    ImGui::DockBuilderDockWindow(kDebugLogWindowName, right_id);

    for (const auto& view : state.render_views) {
        if (view.visible) {
            ImGui::DockBuilderDockWindow(
                render_view_window_name(view.viewport_index).c_str(),
                center_id
            );
        }
    }

    ImGui::DockBuilderFinish(dockspace_id);
    dock_layout_initialized_ = true;
    dock_layout_signature_ = signature;
}

gs3d::app::UiActions UiRoot::draw(gs3d::app::AppState& state)
{
    gs3d::app::UiActions actions;
    if (ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        show_first_hidden_view(state);
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(0.0f, 0.0f)
    );

    if (ImGui::Begin(kHostWindowName, nullptr, host_flags)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("文件")) {
                if (ImGui::MenuItem("打开数据")) {
                    actions.open_requested = true;
                }
                if (ImGui::MenuItem("截图")) {
                    actions.screenshot_requested = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("视图")) {
                const bool has_hidden = std::any_of(
                    state.render_views.begin(),
                    state.render_views.end(),
                    [](const auto& view) {
                        return !view.visible;
                    }
                );
                if (ImGui::MenuItem(
                        "新建视图",
                        "Ctrl+N",
                        false,
                        has_hidden
                    )) {
                    show_first_hidden_view(state);
                }
                ImGui::Separator();
                for (auto& view : state.render_views) {
                    const auto label =
                        "视图 " +
                        std::to_string(view.viewport_index + 1);
                    ImGui::MenuItem(
                        label.c_str(),
                        nullptr,
                        &view.visible
                    );
                }
                ImGui::Separator();
                if (ImGui::MenuItem("恢复默认工作区")) {
                    dock_layout_initialized_ = false;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("窗口")) {
                ImGui::MenuItem(
                    "项目",
                    nullptr,
                    &state.panels.dataset
                );
                ImGui::MenuItem(
                    "属性",
                    nullptr,
                    &state.panels.render_settings
                );
                ImGui::MenuItem(
                    "性能",
                    nullptr,
                    &state.panels.performance
                );
                ImGui::MenuItem(
                    "瓦片",
                    nullptr,
                    &state.panels.tile_inspector
                );
                ImGui::MenuItem(
                    "细节层级",
                    nullptr,
                    &state.panels.lod_view
                );
                ImGui::MenuItem(
                    "日志",
                    nullptr,
                    &state.panels.debug_log
                );
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("帮助")) {
                ImGui::TextUnformatted(
                    "视图可作为标签页使用，也可拖到其他显示器。"
                );
                ImGui::TextUnformatted(
                    "默认相机相互独立，可在视图工具条启用联动。"
                );
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        ImGui::PushStyleVar(
            ImGuiStyleVar_FramePadding,
            ImVec2(7.0f, 4.0f)
        );
        ImGui::BeginChild(
            "##TopToolbar",
            ImVec2(0.0f, 34.0f),
            true,
            ImGuiWindowFlags_NoScrollbar
        );
        if (ImGui::SmallButton("打开")) {
            actions.open_requested = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+ 视图")) {
            show_first_hidden_view(state);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("截图")) {
            actions.screenshot_requested = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            "  %s",
            state.dataset.active_dataset.c_str()
        );
        ImGui::EndChild();
        ImGui::PopStyleVar();

        build_default_layout(state);
        constexpr float status_bar_height = 23.0f;
        ImGui::DockSpace(
            ImGui::GetID("GeoScatter3D.DockSpace"),
            ImVec2(
                0.0f,
                std::max(
                    0.0f,
                    ImGui::GetContentRegionAvail().y -
                        status_bar_height
                )
            ),
            ImGuiDockNodeFlags_None
        );

        ImGui::BeginChild(
            "##StatusBar",
            ImVec2(0.0f, status_bar_height),
            false,
            ImGuiWindowFlags_NoScrollbar
        );
        ImGui::TextDisabled(
            "%.1f FPS   %.2f ms   %llu 点   GPU %.1f MB   %s",
            state.status_bar.fps,
            state.performance.frame_time_ms,
            static_cast<unsigned long long>(
                state.status_bar.visible_points
            ),
            bytes_to_mb(state.status_bar.gpu_memory_bytes),
            state.status_bar.ready_state.c_str()
        );
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

    draw_dataset_panel(state);
    draw_render_settings(state, actions);

    for (auto& view : state.render_views) {
        if (view.visible) {
            draw_viewport_window(view, actions);
        } else {
            view.detached = false;
            view.render_requested = false;
        }
    }

    draw_auxiliary_panels(state);
    return actions;
}

} // namespace gs3d::ui
