#pragma once

#include "app/RecentProjects.hpp"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <string>
#include <vector>

namespace gs3d::ui {

enum class WelcomePageActionKind {
    None,
    ContinueCurrent,
    OpenProject,
    OpenRawData,
    ClearRecent
};

struct WelcomePageModel {
    VkDescriptorSet logo_texture = VK_NULL_HANDLE;
    std::filesystem::path current_path;
    std::vector<gs3d::app::RecentProjectEntry> recent_projects;
};

struct WelcomePageAction {
    WelcomePageActionKind kind = WelcomePageActionKind::None;
    std::filesystem::path path;
};

// Draws one frame of the standalone welcome window.
WelcomePageAction draw_welcome_page(
    const WelcomePageModel& model,
    float ui_scale
);

} // namespace gs3d::ui
