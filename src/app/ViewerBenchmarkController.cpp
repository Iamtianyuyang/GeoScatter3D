#include "app/ViewerBenchmarkController.hpp"
#include "app/UiActions.hpp"

#include <algorithm>
#include <utility>

namespace gs3d::app {

ViewerBenchmarkController::ViewerBenchmarkController(
    bool enabled,
    std::uint32_t frame_count,
    std::uint32_t frames_in_flight,
    const std::filesystem::path& pick_script_path
)
    : session_(enabled, frame_count, frames_in_flight),
      pick_enabled_(enabled && !pick_script_path.empty()),
      queries_(pick_enabled_
          ? load_benchmark_pick_script(pick_script_path)
          : std::vector<BenchmarkPickScriptQuery>{}),
      issue_cpu_ms_(queries_.size(), 0.0),
      issue_metadata_(queries_.size())
{
    results_.reserve(queries_.size());
    if (pick_enabled_) {
        session_.configure_pick_script(queries_.size());
    }
}

BenchmarkSession& ViewerBenchmarkController::session() noexcept { return session_; }
const BenchmarkSession& ViewerBenchmarkController::session() const noexcept { return session_; }
bool ViewerBenchmarkController::pick_enabled() const noexcept { return pick_enabled_; }
bool ViewerBenchmarkController::has_active_query() const noexcept {
    return pick_enabled_ &&
           session_.frame_index() >= BenchmarkSession::kPickWarmupFrames &&
           issue_index_ < queries_.size();
}

void ViewerBenchmarkController::apply_scripted_viewport(
    UiActions& actions,
    std::uint32_t viewport_width,
    std::uint32_t viewport_height
) const {
    if (!pick_enabled_) {
        return;
    }

    const bool query_active = has_active_query();
    const auto& query =
        query_active
            ? queries_[issue_index_]
            : BenchmarkPickScriptQuery{};
    const std::uint32_t width = std::max(viewport_width, 1u);
    const std::uint32_t height = std::max(viewport_height, 1u);
    for (auto& frame : actions.viewport_frames) {
        if (frame.index != 0) {
            continue;
        }
        frame.hovered = query_active;
        frame.active = false;
        frame.width = width;
        frame.height = height;
        frame.mouse_local_x = query.mouse_x;
        frame.mouse_local_y = query.mouse_y;
        frame.mouse_on_image = query_active;
        frame.rotate = false;
        frame.pan = false;
        frame.mouse_delta_x = 0.0f;
        frame.mouse_delta_y = 0.0f;
        frame.mouse_wheel = 0.0f;
        frame.box_select_completed = false;
        return;
    }

    actions.viewport_frames.push_back({
        .index = 0,
        .hovered = query_active,
        .active = false,
        .width = width,
        .height = height,
        .mouse_delta_x = 0.0f,
        .mouse_delta_y = 0.0f,
        .mouse_wheel = 0.0f,
        .mouse_local_x = query.mouse_x,
        .mouse_local_y = query.mouse_y,
        .mouse_on_image = query_active,
        .rotate = false,
        .pan = false,
        .box_select_completed = false
    });
}

std::optional<std::size_t>
ViewerBenchmarkController::take_active_query_for_viewport(
    int viewport_index
) noexcept {
    if (viewport_index != 0 || !has_active_query()) {
        return std::nullopt;
    }
    return queries_[issue_index_++].query_index;
}

bool ViewerBenchmarkController::write_pick_results(
    const std::filesystem::path& output_path
) const {
    if (!pick_enabled_ || output_path.empty()) {
        return false;
    }
    gs3d::app::write_benchmark_pick_results(output_path, results_);
    return true;
}

const std::vector<BenchmarkPickScriptQuery>& ViewerBenchmarkController::queries() const noexcept { return queries_; }
std::vector<double>& ViewerBenchmarkController::issue_cpu_ms() noexcept { return issue_cpu_ms_; }
std::vector<BenchmarkPickIssuedMetadata>& ViewerBenchmarkController::issue_metadata() noexcept { return issue_metadata_; }
std::vector<BenchmarkPickObservedResult>& ViewerBenchmarkController::results() noexcept { return results_; }
const std::vector<BenchmarkPickObservedResult>& ViewerBenchmarkController::results() const noexcept { return results_; }

} // namespace gs3d::app
