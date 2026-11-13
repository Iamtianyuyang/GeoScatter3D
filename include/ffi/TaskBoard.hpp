#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace gs3d::ffi {

/*
 * 引擎任务表与已发布状态（docs/architecture.md「状态快照」）。
 *
 * - 槽位固定 4 个：终态在第一次被 poll 读取后释放，防止 Dart 错过终态。
 * - 只有渲染线程迁移任务状态；取消标记可由任意线程设置，装载前由渲染线程观察。
 * - poll 在同一把锁内复制整代数据，读者不会看到半条记录。
 */
class TaskBoard {
public:
    enum class State : std::uint32_t {
        Queued = 1,
        Running = 2,
        Done = 3,
        Failed = 4,
        Cancelled = 5,
    };

    struct Entry {
        std::uint64_t id = 0;
        State state = State::Queued;
        std::uint32_t progress = 0;
        std::string stage;
    };

    struct Snapshot {
        std::uint64_t generation = 0;
        std::uint64_t point_count = 0;
        bool dataset_loaded = false;
        std::string error;
        std::vector<Entry> tasks;
    };

    static constexpr std::size_t kMaxTasks = 4;

    /// 登记一个新任务；表满返回 0。
    std::uint64_t add(const std::string& stage);

    /// 开始执行。任务已被取消时置为终态并返回 false；调用者必须放弃结果。
    bool begin(std::uint64_t id, const std::string& stage);

    /// 置为终态。message 会覆盖快照里的错误文本。
    void finish(std::uint64_t id, State state, std::string message);

    /// 请求取消：排队中的任务立即终止，运行中的任务在装载前被丢弃。
    bool cancel(std::uint64_t id);

    void set_dataset(bool loaded, std::uint64_t point_count);
    void set_error(std::string message);

    /// 复制最近一代；从未发布过返回 false。
    bool poll(Snapshot* out);

private:
    struct Slot {
        Entry entry;
        bool cancel_requested = false;
    };

    std::mutex mutex_;
    std::vector<Slot> slots_;
    std::uint64_t next_id_ = 1;
    std::uint64_t generation_ = 0;
    std::uint64_t point_count_ = 0;
    bool dataset_loaded_ = false;
    std::string error_;
};

}  // namespace gs3d::ffi
