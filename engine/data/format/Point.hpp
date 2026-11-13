#pragma once

namespace gs3d::data {

struct Gs3dPoint {
    float x;
    float y;
    float z;
    float value;
};

static_assert(sizeof(Gs3dPoint) == 16, "Gs3dPoint must be 16 bytes");

} // namespace gs3d::data
