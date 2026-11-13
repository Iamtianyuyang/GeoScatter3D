#include "ffi/AbiEngine.hpp"

#include <chrono>
#include <utility>

namespace gs3d::abi {
namespace {

constexpr std::size_t kMaxPayloadBytes = 64 * 1024;
constexpr std::size_t kFinishedTasksKept = 32;

bool header_ok(const gs3d_abi_header& header, std::size_t expected) {
    return header.struct_size >= expected && header.abi_version == GS3D_ABI_VERSION;
}

int32_t command_error(int32_t code) {
    return code;
}

} // namespace

void AbiEngine::Task::set_message(const std::string& text) {
    std::lock_guard<std::mutex> lock(message_mutex);
    message = text;
}

std::string AbiEngine::Task::message_copy() {
    std::lock_guard<std::mutex> lock(message_mutex);
    return message;
}

void AbiEngine::Sink::report(float progress) {
    const int32_t permille = static_cast<int32_t>(progress < 0.0f   ? 0.0f
                                                  : progress > 1.0f ? 1.0f
                                                                    : progress) *
                             1000;
    int32_t previous = task_.progress_permille.load();
    while (permille > previous &&
           !task_.progress_permille.compare_exchange_weak(previous, permille)) {
    }
    engine_.notify(GS3D_WAKE_PROGRESS | GS3D_WAKE_TASK);
}

bool AbiEngine::Sink::cancelled() const {
    return task_.cancel_requested.load();
}

AbiEngine::AbiEngine(Core& core) : core_(core), worker_([this] { worker_loop(); }) {}

AbiEngine::~AbiEngine() {
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        stopping_ = true;
    }
    tasks_cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

int32_t AbiEngine::set_wake(gs3d_wake_fn on_wake, void* user) {
    std::lock_guard<std::mutex> lock(wake_mutex_);
    wake_fn_ = on_wake;
    wake_user_ = user;
    return GS3D_OK;
}

void AbiEngine::notify(std::uint32_t mask) {
    pending_wake_.fetch_or(mask);
    gs3d_wake_fn fn = nullptr;
    void* user = nullptr;
    {
        std::lock_guard<std::mutex> lock(wake_mutex_);
        fn = wake_fn_;
        user = wake_user_;
    }
    if (fn != nullptr) {
        fn(user, mask);
    }
}

std::shared_ptr<AbiEngine::Task> AbiEngine::find_task(std::uint64_t task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    const auto found = tasks_.find(task_id);
    return found == tasks_.end() ? nullptr : found->second;
}

int32_t AbiEngine::submit(const gs3d_command& command, std::uint64_t* out_task_id) {
    if (!header_ok(command.header, sizeof(gs3d_command))) {
        last_error_.store(GS3D_ERR_ABI);
        return GS3D_ERR_ABI;
    }
    if (command.reserved != 0 || command.reserved2 != 0) {
        last_error_.store(GS3D_ERR_ARG);
        return GS3D_ERR_ARG;
    }
    if (command.payload != nullptr && command.payload_bytes > kMaxPayloadBytes) {
        last_error_.store(GS3D_ERR_ARG);
        return GS3D_ERR_ARG;
    }
    std::string payload;
    if (command.payload != nullptr) {
        const std::size_t length =
            command.payload_bytes == 0 ? std::string::npos : command.payload_bytes;
        payload.assign(command.payload, length);
        if (payload.size() > kMaxPayloadBytes) {
            last_error_.store(GS3D_ERR_ARG);
            return GS3D_ERR_ARG;
        }
    }

    switch (command.kind) {
    case GS3D_CMD_CANCEL:
        return cancel(command.task_id);
    case GS3D_CMD_CAMERA:
    case GS3D_CMD_RENDER_OPTIONS:
    case GS3D_CMD_PICK:
    case GS3D_CMD_SET_VIEWPORT:
        break;
    case GS3D_CMD_LOAD_DATASET:
    case GS3D_CMD_EXPORT:
    case GS3D_CMD_PREPROCESS: {
        auto task = std::make_shared<Task>();
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            task->id = next_task_id_++;
            task->kind = command.kind;
            tasks_[task->id] = task;
            queue_.push_back(task->id);
            prune_finished_locked();
        }
        if (out_task_id != nullptr) {
            *out_task_id = task->id;
        }
        tasks_cv_.notify_one();
        notify(GS3D_WAKE_TASK);
        return GS3D_OK;
    }
    default:
        last_error_.store(GS3D_ERR_ARG);
        return GS3D_ERR_ARG;
    }

    // Immediate command: forwarded on the caller thread; the core marshals GPU
    // work onto its render thread and does not wait for the GPU itself.
    try {
        switch (command.kind) {
        case GS3D_CMD_CAMERA:
            core_.camera(payload);
            break;
        case GS3D_CMD_RENDER_OPTIONS:
            core_.render_options(payload);
            break;
        case GS3D_CMD_PICK:
            core_.pick(payload);
            break;
        default:
            core_.set_viewport(payload);
            break;
        }
    } catch (const std::exception& error) {
        last_error_.store(GS3D_ERR_TASK);
        notify(GS3D_WAKE_ERROR);
        (void)error;
        return GS3D_ERR_TASK;
    }
    last_error_.store(GS3D_OK);
    return GS3D_OK;
}

int32_t AbiEngine::poll_state(gs3d_state* out_state) {
    if (out_state == nullptr ||
        !header_ok(out_state->header, sizeof(gs3d_state))) {
        return GS3D_ERR_ABI;
    }
    const Snapshot snapshot = core_.snapshot();
    const std::uint32_t wake = pending_wake_.exchange(0);
    std::uint32_t active = 0;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (const auto& entry : tasks_) {
            const int32_t status = entry.second->status.load();
            if (status == GS3D_TASK_PENDING || status == GS3D_TASK_RUNNING) {
                ++active;
            }
        }
    }
    out_state->wake_mask = wake;
    out_state->last_error = last_error_.load();
    out_state->frame_sequence = snapshot.frame_sequence;
    out_state->point_count = snapshot.point_count;
    out_state->viewport_w = snapshot.viewport_w;
    out_state->viewport_h = snapshot.viewport_h;
    out_state->active_tasks = active;
    out_state->engine_abi_version = GS3D_ABI_VERSION;
    for (int i = 0; i < 3; ++i) {
        out_state->camera_pos[i] = snapshot.camera_pos[i];
        out_state->camera_target[i] = snapshot.camera_target[i];
        out_state->camera_up[i] = snapshot.camera_up[i];
    }
    out_state->last_render_ms = snapshot.last_render_ms;
    return GS3D_OK;
}

int32_t AbiEngine::task_state(std::uint64_t task_id, gs3d_task_state* out_state) {
    if (out_state == nullptr || !header_ok(out_state->header, sizeof(gs3d_task_state))) {
        return GS3D_ERR_ABI;
    }
    const std::shared_ptr<Task> task = find_task(task_id);
    if (!task) {
        return GS3D_ERR_NOT_FOUND;
    }
    out_state->task_id = task->id;
    out_state->kind = task->kind;
    out_state->status = task->status.load();
    out_state->progress = static_cast<float>(task->progress_permille.load()) / 1000.0f;
    out_state->error_code = task->error_code;
    {
        std::lock_guard<std::mutex> lock(task->message_mutex);
        out_state->message = task->message.c_str();
    }
    return GS3D_OK;
}

int32_t AbiEngine::cancel(std::uint64_t task_id) {
    const std::shared_ptr<Task> task = find_task(task_id);
    if (!task) {
        return GS3D_ERR_NOT_FOUND;
    }
    const int32_t status = task->status.load();
    if (status == GS3D_TASK_DONE || status == GS3D_TASK_FAILED ||
        status == GS3D_TASK_CANCELLED) {
        return GS3D_ERR_STATE;
    }
    task->cancel_requested.store(true);
    notify(GS3D_WAKE_TASK);
    return GS3D_OK;
}

int32_t AbiEngine::read_frame(std::uint8_t* out_rgba, std::uint32_t max_bytes,
                              std::uint32_t* out_w, std::uint32_t* out_h,
                              std::uint32_t* out_stride, std::uint64_t* out_sequence) {
    const int32_t result =
        core_read_frame(out_rgba, max_bytes, out_w, out_h, out_stride, out_sequence);
    if (result == GS3D_OK) {
        notify(GS3D_WAKE_FRAME);
    }
    return result;
}

void AbiEngine::finish_task(Task& task, int32_t status, int32_t error_code,
                            const std::string& message) {
    task.error_code = error_code;
    task.set_message(message);
    task.status.store(status);
    notify(GS3D_WAKE_TASK | (status == GS3D_TASK_FAILED ? GS3D_WAKE_ERROR : 0));
}

void AbiEngine::run_task(Task& task, const std::string& payload) {
    if (task.cancel_requested.load()) {
        finish_task(task, GS3D_TASK_CANCELLED, GS3D_OK, "");
        return;
    }
    task.status.store(GS3D_TASK_RUNNING);
    Sink sink(*this, task);
    try {
        switch (task.kind) {
        case GS3D_CMD_LOAD_DATASET:
            core_.load_dataset(payload, sink);
            break;
        case GS3D_CMD_EXPORT: {
            const std::string path = json_string(payload, "path");
            const std::string format = json_string(payload, "format");
            core_.export_points(path, format, sink);
            break;
        }
        case GS3D_CMD_PREPROCESS:
            core_.preprocess(payload, sink);
            break;
        default:
            throw std::runtime_error("unsupported task kind");
        }
        sink.report(1.0f);
        finish_task(task, GS3D_TASK_CANCELLED == 0 ? GS3D_TASK_DONE : GS3D_TASK_DONE, GS3D_OK, "");
    } catch (const TaskCancelled&) {
        finish_task(task, GS3D_TASK_CANCELLED, GS3D_OK, "");
    } catch (const std::exception& error) {
        finish_task(task, GS3D_TASK_FAILED, GS3D_ERR_TASK, error.what());
    }
}

void AbiEngine::worker_loop() {
    for (;;) {
        std::uint64_t task_id = 0;
        {
            std::unique_lock<std::mutex> lock(tasks_mutex_);
            tasks_cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) {
                return;
            }
            task_id = queue_.front();
            queue_.pop_front();
        }
        std::shared_ptr<Task> task;
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            const auto found = tasks_.find(task_id);
            if (found == tasks_.end()) {
                continue;
            }
            task = found->second;
        }
        std::string payload;
        {
            std::lock_guard<std::mutex> lock(task->message_mutex);
            payload = task->message;
            task->message.clear();
        }
        run_task(*task, payload);
    }
}

} // namespace gs3d::abi
