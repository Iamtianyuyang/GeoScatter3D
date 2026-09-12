#pragma once

namespace gs3d::render {

// 与窗口系统和 Vulkan 句柄无关的渲染参数。
// 该类型可被离屏目标、交换链目标和未来的其他前端共同使用。
struct ClearColor {
    float r = 0.02f;
    float g = 0.02f;
    float b = 0.025f;
    float a = 1.0f;
};

} // namespace gs3d::render
