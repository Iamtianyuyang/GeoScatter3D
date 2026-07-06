#pragma once

#include "app/RecentProjects.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace gs3d::app {

enum class WelcomeWindowResultKind {
    Cancelled,
    ContinueCurrent,
    OpenProject,
    NewProject
};

struct WelcomeWindowConfig {
    bool enable_validation_layers = true;
    float ui_scale_multiplier = 1.0f;
    std::filesystem::path current_path;
    std::vector<RecentProjectEntry> recent_projects;
};

struct WelcomeWindowResult {
    WelcomeWindowResultKind kind =
        WelcomeWindowResultKind::Cancelled;
    std::filesystem::path path;
    std::string project_name;
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
