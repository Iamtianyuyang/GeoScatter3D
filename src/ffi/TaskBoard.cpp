#include "ffi/TaskBoard.hpp"

#include <algorithm>
#include <utility>

namespace gs3d::ffi {

namespace {

bool terminal(TaskBoard::State state) {
    return state == TaskBoard::State::Done || state == TaskBoard::State::Failed ||
           state == TaskBoard::State::Cancelled;
}

}  // namespace

std::uint64_t TaskBoard::add(const std::string& stage) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slots_.size() >= kMaxTasks) {
        return 0;
    }
    Slot slot;
    slot.entry.id = next_id_++;
    slot.entry.state = State::Queued;
    slot.entry.stage = stage;
    slots_.push_back(std::move(slot));
    ++generation_;
    return slots_.back().entry.id;
}

bool TaskBoard::begin(std::uint64_t id, const std::string& stage) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Slot& slot : slots_) {
        if (slot.entry.id != id) {
            continue;
        }
        if (terminal(slot.entry.state)) {
            return false;
        }
        if (slot.cancel_requested) {
            slot.entry.state = State::Cancelled;
            slot.entry.stage = "cancelled";
            slot.entry.progress = 1000;
            ++generation_;
            return false;
        }
        slot.entry.state = State::Running;
        slot.entry.stage = stage;
        ++generation_;
        return true;
    }
    return false;
}

void TaskBoard::finish(std::uint64_t id, State state, std::string message) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Slot& slot : slots_) {
        if (slot.entry.id != id) {
            continue;
        }
        if (slot.cancel_requested && state == State::Done) {
            slot.entry.state = State::Cancelled;
            slot.entry.stage = "cancelled";
        } else {
            slot.entry.state = state;
            slot.entry.stage = state == State::Done ? "done" : "failed";
        }
        slot.entry.progress = 1000;
        if (!message.empty()) {
            error_ = std::move(message);
        }
        ++generation_;
        return;
    }
}

bool TaskBoard::cancel(std::uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Slot& slot : slots_) {
        if (slot.entry.id != id) {
            continue;
        }
        if (terminal(slot.entry.state)) {
            return false;
        }
        slot.cancel_requested = true;
        if (slot.entry.state == State::Queued) {
            slot.entry.state = State::Cancelled;
            slot.entry.stage = "cancelled";
            slot.entry.progress = 1000;
        }
        ++generation_;
        return true;
    }
    return false;
}

void TaskBoard::set_dataset(bool loaded, std::uint64_t point_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    dataset_loaded_ = loaded;
    point_count_ = point_count;
    ++generation_;
}

void TaskBoard::set_error(std::string message) {
    std::lock_guard<std::mutex> lock(mutex_);
    error_ = std::move(message);
    ++generation_;
}

bool TaskBoard::poll(Snapshot* out) {
    if (out == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation_ == 0) {
        return false;
    }
    out->generation = generation_;
    out->point_count = point_count_;
    out->dataset_loaded = dataset_loaded_;
    out->error = error_;
    out->tasks.clear();
    out->tasks.reserve(slots_.size());
    for (const Slot& slot : slots_) {
        out->tasks.push_back(slot.entry);
    }
    // 终态只交付一次；保留排队/运行中的槽位。
    slots_.erase(std::remove_if(slots_.begin(), slots_.end(),
                                [](const Slot& slot) {
                                    return terminal(slot.entry.state);
                                }),
                 slots_.end());
    return true;
}

}  // namespace gs3d::ffi
