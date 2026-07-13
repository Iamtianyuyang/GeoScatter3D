#include "app/ViewerApp.hpp"
#include "util/Log.hpp"
#include "app/ViewerAppRunState.hpp"

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "data/Gs3dDataset.hpp"
#include "platform/NativeFileDialog.hpp"
#include "platform/Window.hpp"
#include "render/PointPipeline.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace gs3d::app {

void ViewerApp::apply_render_setting_commands(
    const RenderSettingsCommand& command,
    ViewerAppRenderSettingsContext& ctx
) {
    const float elev_min   = static_cast<float>(ctx.dataset.bbox_min_z());
    const float elev_range = static_cast<float>(
        ctx.dataset.bbox_max_z() - ctx.dataset.bbox_min_z());

    // 根据属性描述 + 夸张系数计算 height_offset / height_mult。
    auto apply_height_attr = [&](const AttrDescriptor& a, float exag) {
        ctx.push.height_source = static_cast<std::uint32_t>(a.source);
        if (a.source == AttrPhysicalSource::Z) {
            // 高程值已在空间尺度，stretch around origin
            ctx.push.height_mult   = exag;
            ctx.push.height_offset = 0.0f;
        } else {
            // 非空间属性 → 线性映射到 [elev_min, elev_min + elev_range * exag]
            const float r = a.range();
            const float m = (r > 0.0f) ? (elev_range / r * exag) : exag;
            ctx.push.height_mult   = m;
            ctx.push.height_offset = elev_min - a.min_val * m;
        }
    };

    if (command.point_size_changed) {
        ctx.push.point_size = std::clamp(command.point_size, 1.0f, 10.0f);
    }
    if (command.color_by_changed) {
        const int new_idx = std::clamp(
            command.color_by_index, 0,
            static_cast<int>(ctx.attr_list.size()) - 1
        );
        ctx.scene_state.active_attribute_index = new_idx;
        const auto& a = ctx.attr_list[static_cast<std::size_t>(new_idx)];
        ctx.push.color_source = static_cast<std::uint32_t>(a.source);
        ctx.push.color_min    = a.min_val;
        ctx.push.color_range  = a.range();
        if (ctx.push.color_range <= 0.0f) ctx.push.color_range = 1.0f;
        gs3d::util::log::info() << "[COLOR] switched to: " << a.name << '\n';
        ctx.navigation_map.dirty = true;
    }
    if (command.height_by_changed) {
        const int new_idx = std::clamp(
            command.height_by_index, 0,
            static_cast<int>(ctx.attr_list.size()) - 1
        );
        ctx.scene_state.active_height_index = new_idx;
        apply_height_attr(ctx.attr_list[static_cast<std::size_t>(new_idx)], ctx.height_exag);
        gs3d::util::log::info() << "[HEIGHT] switched to: "
                  << ctx.attr_list[static_cast<std::size_t>(new_idx)].name << '\n';
    }
    if (command.height_exag_changed) {
        ctx.height_exag = command.height_exag;
        apply_height_attr(
            ctx.attr_list[static_cast<std::size_t>(ctx.scene_state.active_height_index)],
            ctx.height_exag
        );
    }
    if (command.colormap_changed) {
        // 清零 colormap bits 再写入新索引
        ctx.push.flags &= ~gs3d::render::PointFlags::kColormapMask;
        ctx.push.flags |= (static_cast<std::uint32_t>(command.colormap_index) << 1)
            & gs3d::render::PointFlags::kColormapMask;
        ctx.navigation_map.dirty = true;
    }
    if (command.value_clip_changed) {
        if (command.value_clip_enabled) {
            ctx.push.flags |= gs3d::render::PointFlags::kValueClip;
            // 将原始数据值转换为归一化 [0,1] 传给 shader。
            // UI 输入的是绝对属性值（与 data_value 同体系），
            // 对着色器需要转回 push.color_min 所在的空间。
            const float cr = ctx.push.color_range > 0.0f
                ? ctx.push.color_range : 1.0f;
            float clip_lo = command.value_clip_min;
            float clip_hi = command.value_clip_max;
            float ref_min = ctx.push.color_min;
            if (ctx.push.color_source ==
                static_cast<std::uint32_t>(
                    AttrPhysicalSource::Z)) {
                const float oz =
                    static_cast<float>(ctx.dataset.origin_z());
                clip_lo -= oz;
                clip_hi -= oz;
                // ref_min (push.color_min) 已经是相对值，不调整
            }
            const float norm_lo =
                (clip_lo - ref_min) / cr;
            const float norm_hi =
                (clip_hi - ref_min) / cr;
            ctx.push.clip_min[3] = std::clamp(norm_lo, 0.0f, 1.0f);
            ctx.push.clip_max[3] = std::clamp(norm_hi, 0.0f, 1.0f);
        } else {
            ctx.push.flags &= ~gs3d::render::PointFlags::kValueClip;
        }
        ctx.navigation_map.dirty = true;
    }
    if (command.point_shape_changed) {
        // 清零 point_shape bits 再写入新索引
        ctx.push.flags &= ~gs3d::render::PointFlags::kPointShapeMask;
        ctx.push.flags |= (static_cast<std::uint32_t>(command.point_shape)
            << gs3d::render::PointFlags::kPointShapeShift)
            & gs3d::render::PointFlags::kPointShapeMask;
    }
}

void ViewerApp::apply_project_open_commands(
    const UiActions& gui_cmds,
    gs3d::platform::Window& window
) {
    if (gui_cmds.show_welcome_requested) {
        open_request_ = ViewerOpenRequest{
            .kind = ViewerOpenRequestKind::Welcome
        };
        window.request_close();
    } else if (!gui_cmds.open_project_path.empty()) {
        open_request_ = ViewerOpenRequest{
            .kind = ViewerOpenRequestKind::Project,
            .path = gui_cmds.open_project_path
        };
        window.request_close();
    } else if (!gui_cmds.open_raw_data_path.empty()) {
        open_request_ = ViewerOpenRequest{
            .kind = ViewerOpenRequestKind::RawData,
            .path = gui_cmds.open_raw_data_path
        };
        window.request_close();
    } else if (gui_cmds.open_bundle_requested) {
        const auto result = gs3d::platform::choose_project_directory();
        if (!result.error.empty()) {
            gs3d::util::log::error() << "[OPEN] " << result.error << '\n';
        } else if (result.path.has_value()) {
            std::error_code ec;
            const auto manifest_path = *result.path / "manifest.toml";
            if (!std::filesystem::is_regular_file(manifest_path, ec)) {
                gs3d::util::log::error()
                    << "[OPEN] 请选择包含 manifest.toml 的 "
                    << ".gs3d.bundle 项目目录。\n";
            } else {
                open_request_ = ViewerOpenRequest{
                    .kind = ViewerOpenRequestKind::Project,
                    .path = *result.path
                };
                window.request_close();
            }
        }
    } else if (gui_cmds.open_requested) {
        const auto result = gs3d::platform::choose_raw_data_file();
        if (!result.error.empty()) {
            gs3d::util::log::error() << "[OPEN] " << result.error << '\n';
        } else if (result.path.has_value()) {
            open_request_ = ViewerOpenRequest{
                .kind = ViewerOpenRequestKind::RawData,
                .path = *result.path
            };
            window.request_close();
        }
    }
}

} // namespace gs3d::app
