# Plan: GeoScatter3D 比赛版查看器

依据：[docs/spec/geoscatter3d-competition.md](../spec/geoscatter3d-competition.md)

> 状态: 历史规划 | 核对: 2026-08-14 (main@1f0eb84)
> 本计划的六项 Layer 2 功能中，E/F/G/I/J 已实现；H（区域书签）未实现。
> 完成度跟踪见 [geoscatter3d-tasks.md](geoscatter3d-tasks.md)。

## 现状核查（codegraph 实测，非 docs/architecture.md 转述）

- `Gs3dPoint{x,y,z,value}`（[Gs3dFormat.hpp:42](../../include/data/Gs3dFormat.hpp#L42)）已经够用：
  `z` 就是 elevation（几何高度），`value` 已映射自 CSV 的 fold 列
  （[CsvToGs3dConverter.cpp:334](../../src/preprocess/CsvToGs3dConverter.cpp#L334)）。
  **不需要改格式即可同时拿到 fold 和 elevation 两个着色属性。**
- 属性着色切换已经实现：`PointPushConstants.attr_index`
  （[PointPipeline.hpp:41-48](../../include/render/PointPipeline.hpp#L41-L48)），
  0=value(fold)、1=z(elevation)，切换零成本（只改 push constant，不重新上传）。
  仿照 Potree/CloudCompare 的 scalar field 切换设计。
- 相机联动分组已实现：`CameraHub`（[CameraHub.hpp:24](../../include/camera/CameraHub.hpp#L24)）。
- 鼠标射线/平面求交已实现：`MouseRay`（[MouseRay.hpp:20](../../include/camera/MouseRay.hpp#L20)），
  可直接复用做拾取和框选反投影。
- 全文搜索确认**完全不存在**：`bookmark`（自有代码中）——区域书签仍是纯新功能，
  没有可复用的半成品。坐标轴/网格（`include/render/AxisGrid.hpp` + 自适应刻度）、
  悬浮提示（hover tooltip：GPU pick 生产路径 + 十字线/数值读出）在本文档撰写后
  **已实现**，不再是新功能。
- 瓦片流式 LOD 主干（`PointCloudTileGpu` 后台读取+主线程上传两步法、`TilePointCache`
  字节预算 LRU、`FrameUploadBudget` 限速）已经成型，本计划是在这套主干上调参和补功能，
  不重写。

## 风险处理：核显验证

手头暂不确定有无核显测试机。**按只有独显规划主线**：独显达到 60fps/20GB 目标作为
主验收线；核显验证作为有条件时的加项，不卡主线进度。设计上仍要为核显的共享内存
带宽留余量（比如 GPU 上传预算、tile 像素阈值这些参数要做成可调，不要硬编码假设
独显显存带宽），方便核显机到位后只调参数、不用改代码结构。

## 组件与依赖顺序

### Layer 0 — 性能基准工具（最先做，其余阶段的"完成"判定都依赖它）

**A. `GeoScatter3DBenchmark`**：新增可执行目标，复用 `util::Stopwatch`。测量项：
帧时间分布、视角/LOD 切换后的重新填充延迟、（内存峰值留人工读数的记录位）。
先用现有 `data/test.csv`（3300 万行）跑出当前基线——这是 spec 里 3 个待定阈值
（重载延迟、保真指标、规模外推）的唯一数据来源。

**验证点**：基准工具能跑通并产出一份基线数字报告。

### Layer 1 — 规模与性能加固（依赖 A 的基线，B/D 可并行，D 早做有利于及早发现问题）

**B. 内存预算调优**：按 1 亿点目标重算 `config/viewer.toml` 里的
`target_point_counts`（目前阶梯顶端只有 3,000,000；已落地为 Potree 式自动分层
`finest_target_points`/`growth_factor`/`min_points_per_level`）、
`cpu_cache_max_bytes`（已落地 4GiB）、`gpu_cache_max_tiles`（已落地 288）、
`gpu_upload_budget_bytes`（已落地 32MiB）。先做字节预算的纸面推导
（16 字节/点 × 各层级驻留点数，对照 20GB 上限留安全边际），再用基准工具实测校准。

**D. 防闪烁**：审查 tile 加载/驱逐的过渡逻辑，确认没有 LRU 驱逐导致的突然消隐/
重新出现（pop），在快速拖拽下用基准工具压测帧时间方差。

**C. 核显验证**（条件触发，机器到位后再做，不卡主线）：复用 A/B 的产出，只调参数。

**验证点**：独显平台上，3300 万行数据交互期间帧时间 P95 ≤16.6ms；1 亿点的内存预算
纸面推导有据可查、留有安全边际。

### Layer 2 — 新交互功能（相互独立，文件不重叠，可并行/可分给子任务并行实现）

| 任务 | 改动范围 | 复用现有能力 |
|---|---|---|
| E. 坐标轴/网格渲染 | 新渲染组件 | 复用相机投影矩阵 |
| F. 悬浮 tooltip | 新拾取模块 + UI 接线 | `MouseRay` 反投影 + 仅查当前驻留 tile（不对 1 亿点暴力搜索） |
| G. 框选放大 | 输入处理 + 相机 | `MouseRay::intersect_plane` 反投影矩形角点 → `Camera::fit_bounds`（已存在） |
| H. 区域书签 | 新 sidecar 格式+读写器 + UI 面板 | 仿 `Gs3dWriter`/`Gs3dReader` 既有模式，独立于 `Gs3dPoint` 格式，不算格式破坏性变更 |
| I. 属性切换 UI 接线 | 仅 `UiRoot`/`ImGuiLayer` | 直接复用 `attr_index`，标签从 value/z 改为 fold/elevation |
| J. Docking 布局持久化 | `ImGuiLayer`/`ViewerApp` 初始化 | ImGui 自带 `.ini` 机制，确认/开启即可 |

**验证点**：每项对照 spec 的 Success Criteria 逐条手动验证；I、J 工作量明显小于
E/F/G/H，可作为同一批次里的"快速收尾项"。

### Layer 3 — 保真验证（依赖 B 的阶梯定下来）

**K. LOD 保真量化指标**：候选方案——抽稀前后 elevation 分布的统计偏差
（如直方图差异）、关键地形特征点（局部极值/fold 边界）保留率。在 B 完成、
1 亿点阶梯参数定下来之后细化为可执行脚本/测试，跑遍现有 3 级 LOD 阶梯并记录数字。

## 风险清单

1. **核显机器不确定**（已处理，见上）——按独显规划主线，核显为条件加项。
2. **框选放大是平面近似**：2.5D 地形不是绝对平面，反投影矩形角点时用一个参考
   平面（如当前视角法向的近似平面）求交，得到的不是精确贴合地形的框，而是一个
   够用的缩放目标框。这是有意简化，不是精度问题——足够支撑"框选聚焦"这个交互
   目的。
3. **悬浮 tooltip 的查询范围**：必须限定在当前 GPU/CPU 驻留的 tile 集合内做最近点
   查询，不能对全量 1 亿点做暴力搜索，否则会拖垂帧率。这是设计约束，不是事后
   优化项。
4. **新增书签 sidecar 格式**：是全新的二进制/文本格式，不属于 spec 里"Ask first"
   清单中的"修改 GS3D/LOD/Tile 格式"（那条只管现有三种格式），但仍建议设计阶段
   过一遍现有 `Gs3dWriter`/`Gs3dReader` 的版本号/校验惯例，保持风格一致。

## 建议的执行顺序

1. A（基准工具）——单独先做，出基线数字。
2. B + D 并行——都是调现有系统的参数/逻辑，不新增文件结构。
3. E / F / G / H / I / J ——六项互相独立，可拆成 6 个并行任务（每项改动文件都不
   重叠），I 和 J 工作量最小可优先收尾出"看得到的进展"。
4. C ——核显机器到位后随时插入，不阻塞前面进度。
5. K ——等 B 的阶梯参数定稿后做。

确认这个计划：yes / no / 要调整？确认后可以用 `planning-and-task-breakdown` 把
Layer 2 的六项拆成更细的可执行任务清单。
