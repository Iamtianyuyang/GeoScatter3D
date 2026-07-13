#pragma once

#include "app/AppState.hpp"
#include "data/Gs3dDataset.hpp"
#include "render/PointPipeline.hpp"

#include <string>
#include <vector>

namespace gs3d::app {

// Owns the logical-to-physical mapping for the two attributes currently
// supported by GS3D (Value and Z), including their push-constant encoding.
class ViewerAttributeMapping {
public:
    ViewerAttributeMapping(
        const gs3d::data::Gs3dDataset& dataset,
        std::string primary_value_name,
        std::string z_field_name
    );

    [[nodiscard]] const std::vector<AttrDescriptor>& descriptors() const noexcept;
    [[nodiscard]] const std::string& primary_value_name() const noexcept;
    [[nodiscard]] const std::string& z_field_name() const noexcept;

    [[nodiscard]] gs3d::render::PointPushConstants make_initial_push(
        float point_size,
        float height_exaggeration = 1.0f
    ) const;

    void apply_height_to(
        gs3d::render::PointPushConstants& push,
        const AttrDescriptor& attribute,
        float height_exaggeration
    ) const noexcept;

private:
    std::vector<AttrDescriptor> descriptors_;
    float elevation_min_ = 0.0f;
    float elevation_range_ = 0.0f;
};

} // namespace gs3d::app
