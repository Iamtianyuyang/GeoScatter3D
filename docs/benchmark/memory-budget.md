# 1 亿点内存预算 — 纸面推算（Task 3）

对应 [docs/plan/geoscatter3d-tasks.md](../plan/geoscatter3d-tasks.md) Task 3。
按 spec 的 Resolved Decisions #1，这里不外推生成 1 亿行数据，只用已知的字节常量
（`Gs3dPoint` 16 字节/点，已用 `data/test.gs3d` 528,346,064 字节 / 33,021,622 点
≈16.0 字节/点验证）做公式推算，等拿到更大/真实数据后用 Task 1 的基准工具复测校准。

## 核心结论：内存不是 1 亿点的瓶颈

`Gs3dPoint{x,y,z,value}` 是 16 字节/点。**1 亿点的全量原始数据总共只有
1e8 × 16 字节 = 1.6 GB**——只占 20GB 预算的 8%。即使把全量数据同时摆在 CPU 内存、
CPU tile 缓存、GPU 显存三份（最浪费的设计），也只是 4.8GB，仍有 15GB 富余。

也就是说：**LOD/tile 流式架构存在的理由是渲染吞吐量（fps）和磁盘 IO 时间，
不是内存容量**。Layer 1 的 B（内存预算）可以设得很宽松，真正要盯的是 C/D
（核显 60fps、防闪烁）。

## 分项推算

| 项 | 当前实测（3300万点） | 1 亿点推算 | 依据 |
|---|---|---|---|
| 全量 GS3D 磁盘文件 | 528.3 MB | 1.6 GB | 16 字节/点 × 点数；**不计入运行时内存**——`lod_enabled && !keep_full_buffer` 时启动只读 header（已验证日志 `loaded_point_bytes=0, metadata_only=true`） |
| LOD sidecar（3 级，CPU+GPU 各一份） | 34.85 MB（实际降采样后约 217.8 万点） | 量级不变，约 35-70MB | `target_point_counts` 是绝对值（3M/1M/0.3M 目标），不随源数据量自动放大；即使个别级别降采样不充分顶到目标上限，3.3M 点 × 16 字节 = 52.8MB，CPU+GPU 两份 ≈106MB，仍是小数 |
| Tile 索引（159 个 tile） | 12,856 字节 | 即使 tile 数涨到 5000+，索引仍 <1MB | 80.9 字节/tile 条目 × tile 数，量级始终是 KB-MB |
| Tile CPU 缓存（LRU，`cpu_cache_max_bytes`） | 配置 512MB 上限 | 建议提到 ~2-4GB | 若 1 亿点与当前数据同 bbox（同空间范围、密度×3.03），每 tile 点数同比增长，512MB 缓存只能装下更少 tile，命中率会下降；提到 2-4GB 可让大部分/全部 tile 常驻 CPU，减少重复磁盘读取 |
| Tile GPU 缓存（`gpu_cache_max_tiles` × 平均每 tile 字节） | 128 tile × ~3.32MB ≈ 425MB | 若 tile 数不变、平均点数 ×3.03：128 × ~10.06MB ≈ 1.29GB；若把 `gpu_cache_max_tiles` 提到能覆盖全部 tile（设当前 tile 数量级 ~159-500），全量驻留也只是 1.6-2.5GB | 这是 LRU 软预算：可见 tile 会被 pin，极端视图可暂时超过它；不能把此表当作显存硬上限 |
| Vulkan/ImGui/多视口固定开销（4 视口离屏 framebuffer + swapchain + ImGui 字体纹理等） | 未单独测量，估计 <300MB | 同量级，不随点数变化 | 固定大小的渲染目标和 UI 资源，与点云规模无关 |
| 进程基线开销（glibc/Vulkan loader/GLFW/std 容器开销） | 未单独测量，估计 <500MB-1GB | 同量级 | 经验值，不随点数变化 |

## 总预算（保守加总，最坏情况）

```
CPU tile 缓存上限（建议新值）        4.0 GB
GPU tile 缓存（全量驻留，估算）       2.5 GB（不是显存硬上限）
LOD CPU + GPU                       0.1 GB
渲染/UI 固定开销                     0.3 GB
进程基线开销                         1.0 GB
─────────────────────────────────────────
合计                                7.9 GB   （20GB 预算的 ~40%，留 12GB 余量）
```

即使数据集再大几倍（比如赛方给的是 2-3 亿点），按同样比例放大 CPU/GPU tile 缓存
两项（线性于点数），合计也只会到 ~15-20GB 区间，前提是可见 tile 集本身没有异常
膨胀。**这不是显存硬保证**：硬截断会产生块状细节伪影，当前实现选择完整可见集，
所以极端视角必须以实际 benchmark 的 selected/required/resident tile 数复核每 tile
的字节规模。后续需要连续的自适应 LOD/质量降级来建立真正的资源上界。

## 建议的新配置值（Task 4 落地）

| 字段 | 当前值 | 建议新值 | 理由 |
|---|---|---|---|
| `tile.cpu_cache_max_bytes` | 536,870,912（512MB） | 4,294,967,296（4GB） | 让大部分/全部 tile 常驻 CPU 缓存，减少重复磁盘读取，直接帮助"减少重载时间"这条验收标准 |
| `tile.gpu_cache_max_tiles` | 128 | 512 | LRU 软预算；调大它给相邻视野留余量，但可见 tile pinning 仍可在极端视图超过该值 |
| `tile.gpu_upload_budget_bytes` | 8,388,608（8MB/帧） | 暂不改 | 这是吞吐限速参数（防止单帧上传过多拖慢 fps），不是容量参数，留给 Layer 1 D（防闪烁）阶段根据帧时间实测再调 |
| `lod.target_point_counts` | [3000000, 1000000, 300000] | 暂不改 | 内存占用可忽略，调整这个阶梯是为了 fps 不是为了内存，留给 Layer 1 C/D |

## 已知局限

- 这是纸面推算，前提假设是"1 亿点与现有数据同字节结构（16 字节/点）、tile
  分块策略不变（固定 512×512 空间网格，不是固定点数分块）"。如果赛方数据的空间
  分布或密度差异很大，需要用 Task 1 的基准工具在真实/更大数据上复测校准
  （spec Resolved Decisions #1 已确认这个顺序）。
- 没有计入 CSV→GS3D 预处理阶段的一次性峰值内存（解析时的点缓冲区、并行 chunk
  缓冲），那是预处理时的瞬时开销，不是查看阶段的稳态内存，且现有代码已经在写盘后
  释放 chunk 缓冲（`std::vector<Gs3dPoint>().swap(chunk_points)`）。若赛方要求
  "现场用 CSV 实时转换并查看"而不是"预转换好再查看"，需要单独估算这部分峰值。
