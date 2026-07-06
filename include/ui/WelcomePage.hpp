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
    NewProject,
    ClearRecent
};

struct NewProjectDialogState {
    bool active = false;
    bool should_open = false;
    std::string data_file_path;
    char project_name[256]{};
    std::string error_message;
    bool name_conflict = false;
};

struct WelcomePageModel {
    VkDescriptorSet logo_texture = VK_NULL_HANDLE;
    std::filesystem::path current_path;
    std::vector<gs3d::app::RecentProjectEntry> recent_projects;
    NewProjectDialogState new_project_dialog;
};

struct WelcomePageAction {
    WelcomePageActionKind kind = WelcomePageActionKind::None;
    std::filesystem::path path;
    std::string project_name;
};

// Draws one frame of the standalone welcome window.
WelcomePageAction draw_welcome_page(
    WelcomePageModel& model,
    float ui_scale
);

} // namespace gs3d::ui
