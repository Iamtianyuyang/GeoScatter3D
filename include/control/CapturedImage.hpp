#pragma once

/*
 * TIA-109：控制面截图结果（内存 PNG）。由渲染层（ScreenshotService）填充，
 * 控制面分发器通过 CaptureCallbacks::take_png 取回。
 */

#include <cstdint>
#include <vector>

namespace gs3d::control {

struct CapturedImage {
    std::vector<std::uint8_t> png_bytes;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

} // namespace gs3d::control
