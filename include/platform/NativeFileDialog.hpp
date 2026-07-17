#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace gs3d::platform {

struct NativeFileDialogResult {
    std::optional<std::filesystem::path> path;
    std::string error;
};

[[nodiscard]]
NativeFileDialogResult choose_project_directory();

[[nodiscard]]
NativeFileDialogResult choose_raw_data_file();

// Presents the platform's system save panel without blocking the render loop.
// On macOS, begin() must be called from the application main thread so AppKit
// can attach NSSavePanel as a sheet to the active window.
class NativeSavePanel {
public:
    NativeSavePanel();
    ~NativeSavePanel();

    NativeSavePanel(const NativeSavePanel&) = delete;
    NativeSavePanel& operator=(const NativeSavePanel&) = delete;

    [[nodiscard]] bool begin(
        const std::string& default_filename,
        const std::string& title,
        const std::string& filters
    );

    [[nodiscard]]
    std::optional<NativeFileDialogResult> poll();

    [[nodiscard]] bool active() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gs3d::platform
