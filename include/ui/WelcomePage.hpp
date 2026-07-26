#pragma once

#include "app/RecentProjects.hpp"
#include "render/VulkanGpuInfo.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <functional>
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
    std::uint32_t thread_count = 1;
    std::uint32_t recommended_thread_count = 1;
    std::uint32_t max_thread_count = 1;
};

struct WelcomePageModel {
    VkDescriptorSet logo_texture = VK_NULL_HANDLE;
    std::filesystem::path current_path;
    std::vector<gs3d::app::RecentProjectEntry> recent_projects;
    NewProjectDialogState new_project_dialog;

    // GPU selection state (populated once after Vulkan init).
    std::vector<gs3d::render::VulkanGpuInfo> gpu_list;
    std::size_t active_gpu_index = 0;
    std::string preferred_gpu;  // "auto" or "uuid:<hex>"
    bool gpu_dialog_active = false;
    bool gpu_dialog_should_open = false;

    // Called by the welcome page when the user picks a new GPU.
    // The caller (WelcomeWindow) wires this to persist the config.
    std::function<void(const std::string&)> on_preferred_gpu_changed;
};

struct WelcomePageAction {
    WelcomePageActionKind kind = WelcomePageActionKind::None;
    std::filesystem::path path;
    std::string project_name;
    std::uint32_t thread_count = 1;
};

struct ProjectPreprocessView {
    std::filesystem::path source_path;
    std::string project_name;
    std::string stage;
    std::string detail;
    std::string error_message;
    float progress = 0.0f;
    double elapsed_seconds = 0.0;
    std::uint32_t thread_count = 1;
    std::uint32_t stage_index = 0;
    bool failed = false;
};

enum class ProjectPreprocessPageAction {
    None,
    BackToWelcome
};

// Draws one frame of the standalone welcome window.
WelcomePageAction draw_welcome_page(
    WelcomePageModel& model,
    float ui_scale
);

// Draws one frame of the new-project preprocessing state in the same visual
// language as the standalone welcome page.
ProjectPreprocessPageAction draw_project_preprocess_page(
    const WelcomePageModel& model,
    const ProjectPreprocessView& progress,
    float ui_scale
);

} // namespace gs3d::ui
