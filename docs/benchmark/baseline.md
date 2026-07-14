# 性能基准 — 基线数字（Task 2）

来自 [GeoScatter3DBenchmark](../../tests/BenchmarkSuite.cpp)（Task 1），用现有
`data/test.csv`/`data/test.gs3d`（3300 万行）跑出的基线，对应
[docs/plan/geoscatter3d-tasks.md](../plan/geoscatter3d-tasks.md) 的 Task 2。

## 测试环境

- CPU：AMD Ryzen 9 7945HX with Radeon Graphics（笔记本，内置 Radeon 核显）
- GPU（本次实测用）：NVIDIA GeForce RTX 5060 Laptop GPU（独显，`VulkanContext`
  默认选中的物理设备）
- OS：Linux 6.14（Ubuntu 24.04 系）
- 数据集：`data/test.gs3d`，33,021,622 点，LOD 3 级（3M/1M/0.3M 目标点数阶梯），
  tile 159 个

**备注**：这台机器的 CPU 本身带 Radeon 核显，理论上可以不换机器、只切换 Vulkan
物理设备选择就测核显路径（Layer 1 C）。目前 `VulkanContext` 还没有暴露选择物理
设备的开关，这是 Task 1 之外的后续小任务，留在 Layer 1 C 里处理。

## 运行命令

```bash
GS3D_BENCHMARK_FRAMES=1800 ./build/GeoScatter3DBenchmark --config config/viewer.toml
```

600 帧轨道旋转+缓慢推近（制造真实的 tile 重选需求），随后 1200 帧静止（让 tile
异步加载/上传完整跑完一轮，用于测重载延迟）。

### 协议完整性

上述 1/3 运动、2/3 静止的比例现在由 `BenchmarkSession` 强制执行并有单元测试。
此前实现曾错误地执行 2/3 运动、1/3 静止；因此在修正前采集的历史数字不能和新协议
下的数字直接比较。每次比较必须报告帧数、GPU、present mode、数据集、缓存状态，并把
GPU P50 作为首要帧时间判据。普通 benchmark 在刚进入静止段时会清空 CPU/GPU tile
缓存、重新选择并重新上传一次；只有完成该事件的运行才能声称验证了 reload 延迟。
每个完成的 reload 还会输出 `selected_tiles`、`required_tiles` 和
`resident_tiles`；只有这些工作负载相同（或明确归一化）时，延迟数字才能横向比较。

## 结果

### 帧时间（独显，1800 帧）

| 指标 | 数值 |
|---|---|
| P50 | 4.18 ms |
| P95 | 8.52 ms |
| P99 | 9.56 ms |

P95/P99 在 tile 重新选择触发异步上传期间偏高（每帧花费在
`gpu_upload_budget_bytes` 限速的上传切片上），但仍远低于 60fps 对应的 16.6ms 阈值，
即使在异步重载进行中也没有掉到目标线以下。

### 重载延迟（一次完整的 tile 重选事件）

| 指标 | 数值 |
|---|---|
| 触发原因 | 缩放（推近）导致可见区域变化，126 个 tile 全部 cache miss 需要重新选择 |
| 候选 tile 数 | 126 |
| 上传后点数 | 26,922,277 |
| 磁盘读取耗时 | 0.654 s |
| GPU 上传耗时 | 0.0018 s（受 8MiB/帧预算限速，分多帧切片完成） |
| **总重载延迟** | **0.896 s** |

这是当前 33,021,622 点数据集上、126/159 tile 全部 cache miss（最坏情况之一，
非缓存命中场景）的重载延迟基线。Task 4（内存预算调优后）和未来 1 亿点数据集
应在此基础上复测，重载延迟阈值的具体目标数字留待那时再定
（spec 的 Resolved Decisions #3 已确认这个顺序）。

## Task 4 回归验证（应用新内存预算配置后）

把 [memory-budget.md](memory-budget.md) 建议的 `tile.gpu_cache_max_tiles: 128→512`、
`tile.cpu_cache_max_bytes: 512MB→4GB` 写入 `config/viewer.toml` 后，同样命令重跑：

| 指标 | 基线（旧配置） | 新配置 | 结论 |
|---|---|---|---|
| frame_time_ms P50/P95/P99 | 4.18 / 8.52 / 9.56 | 4.18 / 8.45 / 9.33 | 无回归（噪声范围内一致） |
| reload_latency_seconds | 0.896（磁盘读 0.654s） | 0.422（磁盘读 0.195s） | 两次 `cache_hit_tiles` 都是 0/126，**这次变快更可能是两次连续运行之间 Linux 页缓存（page cache）发热，不是配置改动本身的效果**——按 Task 4 的验收标准（不劣于基线）已经满足，但不能据此宣称"4GB CPU 缓存让重载快了一倍"，需要更受控的冷/热缓存对比才能下这个结论。

## 已知局限

- 该基线只覆盖独显路径；核显路径待 Layer 1 C 任务（需要给 `VulkanContext` 加
  物理设备选择开关）。
- 重载延迟目前只采到一次完整事件（126 tile 全 miss 的最坏情况），还没有采集
  "部分命中"场景（比如只有少数 tile 需要重新读取）下的延迟分布，后续基准跑里
  应该多采几种相机轨迹来覆盖。
- 当前基准用的是合成轨道+推近运动，不是真实用户交互手感，数字是"机器能跑多快"
  的上限参考，不是"用户实际感受到的延迟"的直接替代。
- `tile.max_visible_tiles` 只为兼容旧配置保留，**不会**截断活动集：实际视觉验证显示
  硬截断会产生块状细节伪影。`tile.gpu_cache_max_tiles` 因而是软预算；极端视图的
  无上限活动集仍是开放风险，必须用连续的自适应 LOD/质量降级解决，而不能恢复硬截断。
  仍不能把不同 `selected_tiles`/`required_tiles` 的 reload 延迟当作同一工作量的回归；
  报告中的对应字段是比较前置条件。
