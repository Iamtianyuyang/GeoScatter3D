#include "app/ViewerApp.hpp"

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "data/Gs3dExporter.hpp"

#include <chrono>
#include <future>
#include <utility>

namespace gs3d::app {

void ViewerApp::handle_dataset_export_commands(
    const UiActions& gui_cmds,
    AppState& app_state
) {
    auto& state = app_state.dataset_export;
    if (dataset_export_future_.valid() &&
        dataset_export_future_.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready) {
        const auto result = dataset_export_future_.get();
        state.in_progress = false;
        state.completed_request_id = state.active_request_id;
        state.success = result.success;
        state.point_count = result.point_count;
        state.error = result.error;
    }

    for (const auto& command : gui_cmds.dataset_export_commands) {
        if (dataset_export_future_.valid()) {
            state.completed_request_id = command.request_id;
            state.success = false;
            state.error = "another dataset export is already in progress";
            continue;
        }

        state.in_progress = true;
        state.active_request_id = command.request_id;
        state.success = false;
        state.point_count = 0;
        state.output_path = command.output_path;
        state.format = command.format;
        state.error.clear();
        const auto source_path = config_.input.gs3d_path;
        const auto output_path = std::filesystem::path(command.output_path);
        const auto format = command.format;
        dataset_export_future_ = std::async(
            std::launch::async,
            [source_path, output_path, format]() {
                return gs3d::data::export_gs3d_points(
                    source_path, output_path, format
                );
            }
        );
    }
}

} // namespace gs3d::app
