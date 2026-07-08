#include "app/ViewerApp.hpp"
#include "app/ViewerAppRunState.hpp"

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "data/Gs3dDataset.hpp"
#include "platform/Window.hpp"
#include "render/PointPipeline.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <iostream>

namespace gs3d::app {

void ViewerApp::apply_render_setting_commands(
    const UiActions& gui_cmds,
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

    if (gui_cmds.point_size_changed) {
        ctx.push.point_size = std::clamp(gui_cmds.point_size, 1.0f, 10.0f);
    }
    if (gui_cmds.color_by_changed) {
        const int new_idx = std::clamp(
            gui_cmds.color_by_index, 0,
            static_cast<int>(ctx.attr_list.size()) - 1
        );
        ctx.scene_state.active_attribute_index = new_idx;
        const auto& a = ctx.attr_list[static_cast<std::size_t>(new_idx)];
        ctx.push.color_source = static_cast<std::uint32_t>(a.source);
        ctx.push.color_min    = a.min_val;
        ctx.push.color_range  = a.range();
        if (ctx.push.color_range <= 0.0f) ctx.push.color_range = 1.0f;
        std::cout << "[COLOR] switched to: " << a.name << '\n';
        ctx.navigation_map.dirty = true;
    }
    if (gui_cmds.height_by_changed) {
        const int new_idx = std::clamp(
            gui_cmds.height_by_index, 0,
            static_cast<int>(ctx.attr_list.size()) - 1
        );
        ctx.scene_state.active_height_index = new_idx;
        apply_height_attr(ctx.attr_list[static_cast<std::size_t>(new_idx)], ctx.height_exag);
        std::cout << "[HEIGHT] switched to: "
                  << ctx.attr_list[static_cast<std::size_t>(new_idx)].name << '\n';
    }
    if (gui_cmds.height_exag_changed) {
        ctx.height_exag = gui_cmds.height_exag;
        apply_height_attr(
            ctx.attr_list[static_cast<std::size_t>(ctx.scene_state.active_height_index)],
            ctx.height_exag
        );
    }
    if (gui_cmds.colormap_changed) {
        // 清零 colormap bits 再写入新索引
        ctx.push.flags &= ~gs3d::render::PointFlags::kColormapMask;
        ctx.push.flags |= (static_cast<std::uint32_t>(gui_cmds.colormap_index) << 1)
            & gs3d::render::PointFlags::kColormapMask;
        ctx.navigation_map.dirty = true;
    }
    if (gui_cmds.value_clip_changed) {
        if (gui_cmds.value_clip_enabled) {
            ctx.push.flags |= gs3d::render::PointFlags::kValueClip;
            // 将原始数据值转换为归一化 [0,1] 传给 shader。
            // UI 输入的是绝对属性值（与 data_value 同体系），
            // 对着色器需要转回 push.color_min 所在的空间。
            const float cr = ctx.push.color_range > 0.0f
                ? ctx.push.color_range : 1.0f;
            float clip_lo = gui_cmds.value_clip_min;
            float clip_hi = gui_cmds.value_clip_max;
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
    if (gui_cmds.point_shape_changed) {
        // 清零 point_shape bits 再写入新索引
        ctx.push.flags &= ~gs3d::render::PointFlags::kPointShapeMask;
        ctx.push.flags |= (static_cast<std::uint32_t>(gui_cmds.point_shape)
            << gs3d::render::PointFlags::kPointShapeShift)
            & gs3d::render::PointFlags::kPointShapeMask;
    }
}

void ViewerApp::apply_project_open_commands(
    const UiActions& gui_cmds,
    gs3d::platform::Window& window
) {
    if (!gui_cmds.open_project_path.empty()) {
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
    }
}

} // namespace gs3d::app
