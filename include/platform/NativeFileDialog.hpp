#pragma once

#include <filesystem>
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

[[nodiscard]]
NativeFileDialogResult choose_save_file(
    const std::string& default_filename,
    const std::string& title,
    const std::string& filters
);

} // namespace gs3d::platform
