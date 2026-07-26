#pragma once

#include "app/RecentProjects.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace gs3d::app {

enum class WelcomeWindowResultKind {
    Cancelled,
    ContinueCurrent,
    OpenProject,
    NewProject
};

struct ProjectPreprocessProgress {
    float fraction = 0.0f;
    std::uint32_t stage_index = 0;
    std::string stage;
    std::string detail;
};

using ProjectPreprocessProgressCallback =
    std::function<void(const ProjectPreprocessProgress&)>;

using NewProjectPreprocessTask = std::function<void(
    const std::filesystem::path& source_path,
    const std::string& project_name,
    std::uint32_t thread_count,
    const ProjectPreprocessProgressCallback& report_progress
)>;

struct WelcomeWindowConfig {
    bool enable_validation_layers = true;
    float ui_scale_multiplier = 1.0f;
    std::filesystem::path current_path;
    std::vector<RecentProjectEntry> recent_projects;
    // "auto" or "uuid:<hex>"
    std::string preferred_gpu = "auto";
    NewProjectPreprocessTask preprocess_new_project;
};

struct WelcomeWindowResult {
    WelcomeWindowResultKind kind =
        WelcomeWindowResultKind::Cancelled;
    std::filesystem::path path;
    std::string project_name;
    std::uint32_t thread_count = 1;
    bool preprocessed = false;
};

class WelcomeWindow {
public:
    explicit WelcomeWindow(WelcomeWindowConfig config);

    [[nodiscard]]
    WelcomeWindowResult run();

private:
    WelcomeWindowConfig config_;
};

} // namespace gs3d::app
