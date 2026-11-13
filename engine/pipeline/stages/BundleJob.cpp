#include "pipeline/stages/BundleJob.hpp"

#include <stdexcept>
#include <utility>

namespace gs3d::pipeline {
namespace {

struct Cancelled {};
std::atomic_flag active_job = ATOMIC_FLAG_INIT;

struct JobSlot {
    ~JobSlot() { active_job.clear(); }
};

} // namespace

BundleJob::~BundleJob() {
    cancel_requested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::uint64_t BundleJob::start(BundleBuildOptions options) {
    return launch([options = std::move(options)](const StageCallback& on_stage) {
        return build_bundle(options, on_stage);
    });
}

std::uint64_t BundleJob::resume(std::filesystem::path bundle_dir) {
    return launch([bundle_dir = std::move(bundle_dir)](const StageCallback& on_stage) {
        return resume_bundle(bundle_dir, on_stage);
    });
}

std::uint64_t BundleJob::launch(Work work) {
    std::lock_guard lock(mutex_);
    if (status_.state == BundleJobState::running) {
        throw std::runtime_error("bundle job: another job is running");
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    if (active_job.test_and_set()) {
        throw std::runtime_error("bundle job: another pipeline job is running");
    }
    cancel_requested_.store(false);
    status_ = {};
    status_.id = next_id_++;
    status_.state = BundleJobState::running;
    try {
        worker_ = std::thread([this, work = std::move(work)] {
            JobSlot slot;
            try {
                const auto result = work([this](const char* stage) {
                    {
                        std::lock_guard lock(mutex_);
                        status_.stage = stage;
                    }
                    if (cancel_requested_.load()) {
                        throw Cancelled{};
                    }
                });
                std::lock_guard lock(mutex_);
                status_.points = result.points;
                status_.chunks = result.chunks;
                status_.state = BundleJobState::done;
            } catch (const Cancelled&) {
                std::lock_guard lock(mutex_);
                status_.state = BundleJobState::cancelled;
            } catch (const std::exception& error) {
                std::lock_guard lock(mutex_);
                status_.error = error.what();
                status_.state = BundleJobState::failed;
            } catch (...) {
                std::lock_guard lock(mutex_);
                status_.error = "unknown build failure";
                status_.state = BundleJobState::failed;
            }
        });
    } catch (...) {
        active_job.clear();
        status_ = {};
        throw;
    }
    return status_.id;
}

bool BundleJob::cancel(std::uint64_t id) {
    std::lock_guard lock(mutex_);
    if (id == 0 || status_.id != id || status_.state != BundleJobState::running) {
        return false;
    }
    cancel_requested_.store(true);
    return true;
}

BundleJobStatus BundleJob::poll() const {
    std::lock_guard lock(mutex_);
    return status_;
}

} // namespace gs3d::pipeline
