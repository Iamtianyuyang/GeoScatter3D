#include "app/ViewerBenchmarkController.hpp"

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
const std::vector<BenchmarkPickScriptQuery>& ViewerBenchmarkController::queries() const noexcept { return queries_; }
std::size_t& ViewerBenchmarkController::issue_index() noexcept { return issue_index_; }
std::vector<double>& ViewerBenchmarkController::issue_cpu_ms() noexcept { return issue_cpu_ms_; }
std::vector<BenchmarkPickIssuedMetadata>& ViewerBenchmarkController::issue_metadata() noexcept { return issue_metadata_; }
std::vector<BenchmarkPickObservedResult>& ViewerBenchmarkController::results() noexcept { return results_; }
const std::vector<BenchmarkPickObservedResult>& ViewerBenchmarkController::results() const noexcept { return results_; }

} // namespace gs3d::app
