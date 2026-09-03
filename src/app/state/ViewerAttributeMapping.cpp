#include "app/ViewerAttributeMapping.hpp"

namespace gs3d::app {

ViewerAttributeMapping::ViewerAttributeMapping(
    const gs3d::data::Gs3dDataset& dataset,
    std::string primary_value_name,
    std::string z_field_name
)
    : descriptors_{
        {
            primary_value_name.empty() ? "value" : std::move(primary_value_name),
            AttrPhysicalSource::Value,
            dataset.value_min(),
            dataset.value_max()
        },
        {
            z_field_name.empty() ? "z" : std::move(z_field_name),
            AttrPhysicalSource::Z,
            dataset.bbox_min_z(),
            dataset.bbox_max_z()
        }
      },
      elevation_min_(dataset.bbox_min_z()),
      elevation_range_(dataset.bbox_max_z() - dataset.bbox_min_z())
{
}

const std::vector<AttrDescriptor>& ViewerAttributeMapping::descriptors() const noexcept
{
    return descriptors_;
}

const std::string& ViewerAttributeMapping::primary_value_name() const noexcept
{
    return descriptors_.front().name;
}

const std::string& ViewerAttributeMapping::z_field_name() const noexcept
{
    return descriptors_.back().name;
}

gs3d::render::PointPushConstants ViewerAttributeMapping::make_initial_push(
    float point_size,
    float height_exaggeration
) const {
    gs3d::render::PointPushConstants push{};
    push.point_size = point_size;

    const auto& color = descriptors_.front();
    push.color_source = static_cast<std::uint32_t>(color.source);
    push.color_min = color.min_val;
    push.color_range = color.range();
    if (push.color_range <= 0.0f) {
        push.color_range = 1.0f;
    }
    apply_height_to(push, descriptors_.back(), height_exaggeration);
    push.flags &= ~gs3d::render::PointFlags::kColormapMask;
    push.flags |= (8u << 1) & gs3d::render::PointFlags::kColormapMask;
    return push;
}

void ViewerAttributeMapping::apply_height_to(
    gs3d::render::PointPushConstants& push,
    const AttrDescriptor& attribute,
    float height_exaggeration
) const noexcept {
    push.height_source = static_cast<std::uint32_t>(attribute.source);
    if (attribute.source == AttrPhysicalSource::Z) {
        push.height_mult = height_exaggeration;
        push.height_offset = 0.0f;
        return;
    }

    const float range = attribute.range();
    const float multiplier = range > 0.0f
        ? elevation_range_ / range * height_exaggeration
        : height_exaggeration;
    push.height_mult = multiplier;
    push.height_offset = elevation_min_ - attribute.min_val * multiplier;
}

} // namespace gs3d::app
