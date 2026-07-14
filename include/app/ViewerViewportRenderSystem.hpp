#pragma once

#include "app/ViewerAppRunState.hpp"

#include <vulkan/vulkan.h>

namespace gs3d::app {

struct ViewerViewportRenderOptions {
    bool lod_enabled = false;
    bool render_tiles_while_interacting = true;
    bool pick_debug_dump_enabled = false;
    bool pick_debug_dump_once_on_hover = true;
};

// Records each visible offscreen viewport and its associated GPU-pick work.
// ViewerApp supplies the per-frame state; this system owns the stable render
// policy that was previously read directly from ViewerAppConfig.
class ViewerViewportRenderSystem {
public:
    explicit ViewerViewportRenderSystem(ViewerViewportRenderOptions options)
        : options_(options)
    {
    }

    void record(
        VkCommandBuffer command_buffer,
        const ViewerViewportDrawContext& context
    );

private:
    ViewerViewportRenderOptions options_;
};

} // namespace gs3d::app
