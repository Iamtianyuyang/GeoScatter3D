#include "app/ViewerRenderSettingsSystem.hpp"

#include "util/Log.hpp"

#include <algorithm>

namespace gs3d::app {

void ViewerRenderSettingsSystem::apply_commands(
    const std::vector<RenderSettingsCommand>& commands,
    AppState& app_state,
    std::vector<gs3d::render::PointPushConstants>& viewport_pushes,
    std::vector<gs3d::scene::SceneState>& viewport_scenes,
    const std::vector<AttrDescriptor>& attributes,
    const gs3d::data::Gs3dDataset& dataset,
    std::vector<float>& viewport_height_exaggerations
) const {
    for (const auto& command : commands) {
        const auto main_targets = [&app_state]() {
            return std::vector<int>{app_state.active_viewport_index};
        };
        const auto targets = command.has_viewport_scope
            ? command.viewport_indices
            : main_targets();
        for (const int viewport_index : targets) {
            if (viewport_index < 0 ||
                viewport_index >= static_cast<int>(viewport_pushes.size()) ||
                static_cast<std::size_t>(viewport_index) >=
                    viewport_scenes.size() ||
                static_cast<std::size_t>(viewport_index) >=
                    viewport_height_exaggerations.size()) {
                continue;
            }
            const auto index = static_cast<std::size_t>(viewport_index);
            ViewerRenderSettingsContext context{
                .push = viewport_pushes[index],
                .scene_state = viewport_scenes[index],
                .navigation_map = navigation_map_for_view(
                    app_state,
                    viewport_index
                ),
                .attributes = attributes,
                .dataset = dataset,
                .height_exaggeration = viewport_height_exaggerations[index]
            };
            apply(command, context);
        }
    }
}

void ViewerRenderSettingsSystem::apply(
    const RenderSettingsCommand& command,
    ViewerRenderSettingsContext& context
) const {
    const float elevation_min =
        static_cast<float>(context.dataset.bbox_min_z());
    const float elevation_range = static_cast<float>(
        context.dataset.bbox_max_z() - context.dataset.bbox_min_z()
    );

    const auto apply_height_attribute =
        [&](const AttrDescriptor& attribute, const float exaggeration) {
            context.push.height_source =
                static_cast<std::uint32_t>(attribute.source);
            if (attribute.source == AttrPhysicalSource::Z) {
                context.push.height_mult = exaggeration;
                context.push.height_offset = 0.0f;
                return;
            }

            const float range = attribute.range();
            const float multiplier = range > 0.0f
                ? elevation_range / range * exaggeration
                : exaggeration;
            context.push.height_mult = multiplier;
            context.push.height_offset =
                elevation_min - attribute.min_val * multiplier;
        };

    if (command.point_size_changed) {
        context.push.point_size = std::clamp(command.point_size, 1.0f, 10.0f);
    }
    if (command.color_by_changed) {
        const int new_index = std::clamp(
            command.color_by_index,
            0,
            static_cast<int>(context.attributes.size()) - 1
        );
        context.scene_state.active_attribute_index = new_index;
        const auto& attribute =
            context.attributes[static_cast<std::size_t>(new_index)];
        context.push.color_source =
            static_cast<std::uint32_t>(attribute.source);
        context.push.color_min = attribute.min_val;
        context.push.color_range = attribute.range();
        if (context.push.color_range <= 0.0f) {
            context.push.color_range = 1.0f;
        }
        gs3d::util::log::info()
            << "[COLOR] switched to: " << attribute.name << '\n';
        context.navigation_map.dirty = true;
    }
    if (command.height_by_changed) {
        const int new_index = std::clamp(
            command.height_by_index,
            0,
            static_cast<int>(context.attributes.size()) - 1
        );
        context.scene_state.active_height_index = new_index;
        apply_height_attribute(
            context.attributes[static_cast<std::size_t>(new_index)],
            context.height_exaggeration
        );
        gs3d::util::log::info()
            << "[HEIGHT] switched to: "
            << context.attributes[static_cast<std::size_t>(new_index)].name
            << '\n';
    }
    if (command.height_exag_changed) {
        context.height_exaggeration = command.height_exag;
        apply_height_attribute(
            context.attributes[static_cast<std::size_t>(
                context.scene_state.active_height_index
            )],
            context.height_exaggeration
        );
    }
    if (command.colormap_changed) {
        context.push.flags &= ~gs3d::render::PointFlags::kColormapMask;
        context.push.flags |=
            (static_cast<std::uint32_t>(command.colormap_index) << 1) &
            gs3d::render::PointFlags::kColormapMask;
        context.navigation_map.dirty = true;
    }
    if (command.value_clip_changed) {
        if (command.value_clip_enabled) {
            context.push.flags |= gs3d::render::PointFlags::kValueClip;
            const float color_range = context.push.color_range > 0.0f
                ? context.push.color_range
                : 1.0f;
            float clip_min = command.value_clip_min;
            float clip_max = command.value_clip_max;
            const float color_min = context.push.color_min;
            if (context.push.color_source == static_cast<std::uint32_t>(
                    AttrPhysicalSource::Z
                )) {
                const float origin_z =
                    static_cast<float>(context.dataset.origin_z());
                clip_min -= origin_z;
                clip_max -= origin_z;
            }
            context.push.clip_min[3] = std::clamp(
                (clip_min - color_min) / color_range,
                0.0f,
                1.0f
            );
            context.push.clip_max[3] = std::clamp(
                (clip_max - color_min) / color_range,
                0.0f,
                1.0f
            );
        } else {
            context.push.flags &= ~gs3d::render::PointFlags::kValueClip;
        }
        context.navigation_map.dirty = true;
    }
    if (command.point_shape_changed) {
        context.push.flags &= ~gs3d::render::PointFlags::kPointShapeMask;
        context.push.flags |=
            (static_cast<std::uint32_t>(command.point_shape)
                << gs3d::render::PointFlags::kPointShapeShift) &
            gs3d::render::PointFlags::kPointShapeMask;
    }
}

} // namespace gs3d::app
