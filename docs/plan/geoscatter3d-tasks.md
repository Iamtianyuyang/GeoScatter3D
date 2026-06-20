# Implementation Plan: GeoScatter3D 比赛版查看器 — 任务拆解

依据：[docs/plan/geoscatter3d-competition.md](geoscatter3d-competition.md)（已确认）

## Overview

把已确认的四层计划（Layer 0 基准 → Layer 1 规模/性能 → Layer 2 六项新交互功能 →
Layer 3 保真验证）拆成可在单次会话内完成、有明确验收标准的任务。Layer 2 的六项
彼此独立、文件不重叠，按"垂直切片"组织（每项功能从底层逻辑到 UI 接线算一个切片），
不是"先写完所有渲染再写所有 UI"那种横切。

## Architecture Decisions

- 不新增/不修改 `Gs3dPoint` 等现有二进制格式（fold/elevation 已经够用，见 plan 文档）。
- 悬浮 tooltip 的最近点查询范围限定在当前驻留 tile 集合内，不对全量点暴力搜索
  （架构约束，不是后续优化项）。
- 框选放大用参考平面反投影，不要求精确贴合地形（有意简化）。
- 核显验证（Layer 1 C）作为条件触发的 backlog 任务，不进主线任务序号。

## Task List

### Phase 0: 基准工具（Layer 0，最先做）

- [ ] **Task 1: 新增性能基准可执行目标**
  - **Description**：新增 `GeoScatter3DBenchmark` CMake 目标，复用
    `util::Stopwatch` 测帧时间分布和视角/LOD 切换后的重新填充延迟，加载
    `config/viewer.toml` 指向的现有数据集运行一段固定交互脚本（如自动旋转+缩放
    N 帧）后打印统计数字。**新增可执行目标改动了 `CMakeLists.txt` 的目标结构，
    按 spec 的"Ask first"边界，开工前需要再次确认。**
  - **Acceptance criteria**:
    - [ ] `cmake --build build -j` 能编译出新目标，不影响现有 `GeoScatter3D`/
          `GeoScatter3DDataTests`/`GeoScatter3DRuntimeTests` 目标。
    - [ ] 运行后输出帧时间 P50/P95/P99 和重新填充延迟数字到 stdout。
  - **Verification**:
    - [ ] `cmake --build build -j` 成功；
    - [ ] 手动运行新目标，确认输出数字非零且合理（不是 NaN/0）。
  - **Dependencies**: None
  - **Files likely touched**: `CMakeLists.txt`、新文件 `tests/BenchmarkSuite.cpp`
    （或 `tools/Benchmark.cpp`，命名待定）
  - **Estimated scope**: Medium (2-3 files)

- [ ] **Task 2: 跑出 3300 万行数据集的基线数字**
  - **Description**：用 Task 1 的工具跑现有 `data/test.csv`/`data/test.gs3d`，
    记录当前帧时间、重载延迟基线，写入 `docs/benchmark/baseline.md`。这是 spec
    里 3 个待定阈值（重载延迟目标、保真指标基准、1 亿点外推依据）的唯一数据来源。
  - **Acceptance criteria**:
    - [ ] `docs/benchmark/baseline.md` 包含至少一次完整运行的 P50/P95/P99 帧时间、
          重载延迟数字，并注明测试机硬件（独显型号/CPU）。
  - **Verification**:
    - [ ] 文档存在且数字非占位符。
  - **Dependencies**: Task 1
  - **Files likely touched**: `docs/benchmark/baseline.md`（新建）
  - **Estimated scope**: Small (1 file)

### Checkpoint: Phase 0
- [ ] 基准工具可重复运行，基线数字已记录。
- [ ] 与用户确认：基线数字是否符合预期，是否需要调整 Task 1 的测量方法
      （比如要不要加内存峰值的人工读数记录位）。

### Phase 1: 规模与性能加固（Layer 1 B + D，依赖 Phase 0）

- [ ] **Task 3: 1 亿点内存预算纸面推算**
  - **Description**：基于 `Gs3dPoint`（16 字节/点）+ LOD 阶梯 + tile 索引开销，
    推算 1 亿点在当前架构下 CPU 缓存/GPU 驻留/LOD sidecar 的总内存占用，对照
    20GB 上限留安全边际，产出 `target_point_counts`、`cpu_cache_max_bytes`、
    `gpu_cache_max_tiles`、`gpu_upload_budget_bytes` 的新建议值。
  - **Acceptance criteria**:
    - [ ] 推算文档列出每一层（LOD 各级 + tile 缓存 + GPU 驻留）的字节占用公式
          和具体数字，总和 ≤20GB 并标注安全边际。
  - **Verification**:
    - [ ] 数字经人工复核（加总不超预算）。
  - **Dependencies**: None（可与 Task 1-2 并行开始，但建议先有基线数字再核对）
  - **Files likely touched**: `docs/benchmark/memory-budget.md`（新建）
  - **Estimated scope**: Small (1 file, 无代码改动)

- [ ] **Task 4: 应用新内存预算配置**
  - **Description**：把 Task 3 算出的数值写入 `config/viewer.toml`，用 Task 1
    工具在现有 3300 万行数据上回归验证没有性能退化。
  - **Acceptance criteria**:
    - [ ] `config/viewer.toml` 的 LOD/tile 相关字段更新；
    - [ ] 基准工具在新配置下跑出的帧时间不比 Task 2 基线差。
  - **Verification**:
    - [ ] `ctest --test-dir build --output-on-failure` 通过；
    - [ ] 基准工具重跑，数字记录对比。
  - **Dependencies**: Task 2, Task 3
  - **Files likely touched**: `config/viewer.toml`
  - **Estimated scope**: Small (1 file)

- [ ] **Task 5: 审查 tile 加载/驱逐过渡是否闪烁**
  - **Description**：审查 `TileSelection.cpp`/`PointCloudTileGpu.cpp` 中 LRU
    驱逐和新 tile 上传的时序，确认是否存在"先移除旧 tile 再等新 tile 上传完成"
    导致的瞬间空白/突跳。这是只读审查任务，产出问题清单，不是修复任务。
  - **Acceptance criteria**:
    - [ ] 产出文档列出（如果有）具体会导致闪烁的代码路径和触发条件。
  - **Verification**:
    - [ ] 人工复核审查结论。
  - **Dependencies**: None
  - **Files likely touched**: 无代码改动，新建 `docs/benchmark/flicker-audit.md`
  - **Estimated scope**: Small (审查 2 文件，0 改动)

- [ ] **Task 6: 修复 Task 5 发现的闪烁问题（如有）**
  - **Description**：视 Task 5 结论而定，可能是延后驱逐时机、双缓冲过渡等。
    具体方案待 Task 5 完成后细化（占位任务，范围未知）。
  - **Acceptance criteria**: 待细化
  - **Verification**: 待细化
  - **Dependencies**: Task 5
  - **Files likely touched**: `src/render/TileSelection.cpp`、
    `src/render/PointCloudTileGpu.cpp`（待定）
  - **Estimated scope**: 待 Task 5 结论后重新评估，如发现是 L 级工作量需再拆分

### Checkpoint: Phase 1
- [ ] 现有 3300 万行数据在独显上帧时间 P95 ≤16.6ms，无闪烁问题（或问题已记录/修复）。
- [ ] 1 亿点内存预算有据可查，与用户确认是否要在真实更大数据集到位前就接受这个推算。

### Phase 2: 新交互功能（Layer 2，六项独立，可并行）

#### 切片 I：属性切换 UI 接线（最小，建议最先做出"看得到的进展"）
- [ ] **Task 7: fold/elevation 着色切换 UI**
  - **Description**：在 `UiRoot`/`ImGuiLayer` 里把现有 `attr_index` 切换暴露为
    UI 控件，标签改为"fold"/"elevation"（而非内部的 value/z）。
  - **Acceptance criteria**:
    - [ ] UI 上有切换控件，点击后画面立即变色，无重新加载/卡顿。
  - **Verification**:
    - [ ] 手动测试：切换属性，观察渐变即时生效。
  - **Dependencies**: None
  - **Files likely touched**: `src/ui/UiRoot.cpp`、`include/ui/UiRoot.hpp`
  - **Estimated scope**: Small (1-2 文件)

#### 切片 J：Docking 布局持久化
- [ ] **Task 8: 启用/确认 ImGui `.ini` 布局持久化**
  - **Description**：确认 `ImGuiLayer` 初始化时 `io.IniFilename` 指向项目内固定
    路径（而非默认临时路径或被禁用），重启程序后面板布局（拖拽停靠/浮动窗口）
    应恢复。
  - **Acceptance criteria**:
    - [ ] 拖动/停靠面板后重启程序，布局与关闭前一致。
  - **Verification**:
    - [ ] 手动测试：调整布局→关闭→重新打开→对比。
  - **Dependencies**: None
  - **Files likely touched**: `src/gui/ImGuiLayer.cpp`、`src/app/ViewerApp.cpp`
  - **Estimated scope**: Small (1-2 文件)

#### 切片 G：框选放大
- [ ] **Task 9: 屏幕矩形拖拽捕获**
  - **Description**：在视口上实现鼠标拖拽画矩形框的输入处理（按下记录起点，
    拖动中实时画 ImGui 覆盖框，松开得到屏幕空间矩形）。
  - **Acceptance criteria**:
    - [ ] 拖拽时画面上能看到实时绘制的矩形框。
  - **Verification**: 手动测试。
  - **Dependencies**: None
  - **Files likely touched**: `src/app/ViewerApp.cpp` 或新文件
    `src/camera/BoxSelect.cpp`（命名待定）
  - **Estimated scope**: Small-Medium (1-2 文件)

- [ ] **Task 10: 矩形反投影 + 相机聚焦**
  - **Description**：用 `MouseRay::intersect_plane` 把矩形 4 角反投影到参考平面，
    取得世界空间包围盒，调用已有的 `Camera::fit_bounds` 完成聚焦缩放。
  - **Acceptance criteria**:
    - [ ] 拖框松开后相机平滑/直接聚焦到该区域，画面里能看清框内内容。
  - **Verification**: 手动测试，对比框选前后视野范围。
  - **Dependencies**: Task 9
  - **Files likely touched**: 同 Task 9 文件 + `include/camera/Camera.hpp`（如需扩展接口）
  - **Estimated scope**: Small (1-2 文件)

#### 切片 F：悬浮 tooltip
- [ ] **Task 11: 驻留 tile 内最近点查询**
  - **Description**：用 `MouseRay::from_screen` 取得鼠标射线，在当前驻留（已加载
    到 CPU/`TilePointCache`）的 tile 点集合内查询最近点，返回其 x/y/fold/elevation。
    范围限定在驻留 tile，不对全量点暴力搜索。
  - **Acceptance criteria**:
    - [ ] 给定鼠标屏幕坐标，能在毫秒级返回最近点数据（用 Task 1 工具或手动计时
          验证不卡帧）。
  - **Verification**: 单元测试覆盖一个已知小数据集的最近点查询正确性。
  - **Dependencies**: None
  - **Files likely touched**: 新文件 `src/render/NearestPointQuery.cpp` +
    对应头文件（命名待定）
  - **Estimated scope**: Medium (2-3 文件)

- [ ] **Task 12: Tooltip UI 展示**
  - **Description**：把 Task 11 的查询结果接到 ImGui tooltip，鼠标悬停时显示
    x/y/fold/elevation。
  - **Acceptance criteria**:
    - [ ] 鼠标悬停在点附近时弹出 tooltip，数值与该点实际属性一致。
  - **Verification**: 手动测试，对照已知数据行核对数值。
  - **Dependencies**: Task 11
  - **Files likely touched**: `src/ui/UiRoot.cpp`
  - **Estimated scope**: Small (1 文件)

#### 切片 E：坐标轴/网格
- [ ] **Task 13: 轴线几何与刻度计算**
  - **Description**：根据当前数据集 bbox（x/y/elevation 范围）和相机状态，计算
    需要渲染的轴线端点和刻度位置（世界坐标），不涉及 GPU 渲染，先把数学/数据
    结构做出来并单元测试。
  - **Acceptance criteria**:
    - [ ] 给定 bbox，输出的刻度间隔/位置符合预期（如整数倍最近"好看"间隔，
          类似常见绘图库的 tick 算法）。
  - **Verification**: 单元测试覆盖至少 2 组不同范围的 bbox。
  - **Dependencies**: None
  - **Files likely touched**: 新文件 `src/render/AxisGrid.cpp` + 头文件
  - **Estimated scope**: Small-Medium (2 文件)

- [ ] **Task 14: 轴线渲染**
  - **Description**：把 Task 13 的轴线端点渲染为 Vulkan 线段（新增简单线渲染
    管线，或确认能否复用现有点渲染管线的简化模式）。
  - **Acceptance criteria**:
    - [ ] 画面中可见三维坐标轴/网格框，随相机旋转缩放正确联动。
  - **Verification**: 手动测试，旋转/缩放视角观察轴线跟随。
  - **Dependencies**: Task 13
  - **Files likely touched**: 新文件 `src/render/AxisGridPipeline.cpp`（或复用
    `PointPipeline` 扩展），`src/render/VulkanRenderer.cpp` 接线
  - **Estimated scope**: Medium (2-3 文件)

- [ ] **Task 15: 刻度数值标注**
  - **Description**：把 Task 13 算出的刻度世界坐标投影到屏幕空间，用 ImGui 覆盖
    文字渲染数值标签（比 Vulkan 文字渲染简单，复用现成 ImGui 字体管线）。
  - **Acceptance criteria**:
    - [ ] 轴线旁能看到数值标签，随视角变化正确跟随且不和画面其他 UI 重叠到
          不可读的程度。
  - **Verification**: 手动测试。
  - **Dependencies**: Task 13, Task 14
  - **Files likely touched**: `src/ui/UiRoot.cpp`
  - **Estimated scope**: Small (1 文件)

#### 切片 H：区域书签
- [ ] **Task 16: 书签 sidecar 格式 + 读写器**
  - **Description**：仿照 `Gs3dWriter`/`Gs3dReader` 的版本号/校验惯例，设计新的
    书签 sidecar 格式（每数据集一份，存区域包围盒 + 相机状态 + 可见性标志 + 名称），
    实现写入/读取，配套数据格式正确性测试（参照 `Gs3dFormatTests.cpp` 的模式）。
  - **Acceptance criteria**:
    - [ ] 写入后能完整读回，字段一致；版本号/魔数校验存在。
  - **Verification**: 新增测试加入 `GeoScatter3DDataTests`（注意：这是往现有测试
    目标加文件，不是新增可执行目标，不触发"Ask first"）。
  - **Dependencies**: None
  - **Files likely touched**: 新文件 `include/data/BookmarkFormat.hpp`、
    `src/data/BookmarkFormat.cpp`、`tests/BookmarkFormatTests.cpp`（待命名）
  - **Estimated scope**: Medium (3 文件)

- [ ] **Task 17: 书签 UI 面板**
  - **Description**：UI 面板列出已保存书签，支持新建（从当前框选/视角保存）、
    跳转（恢复相机状态）、隐藏/显示切换。
  - **Acceptance criteria**:
    - [ ] 能新建、列出、跳转、隐藏书签，重启程序后书签仍在。
  - **Verification**: 手动测试完整流程。
  - **Dependencies**: Task 16, Task 10（书签新建复用框选得到的区域）
  - **Files likely touched**: `src/ui/UiRoot.cpp`、`src/app/ViewerApp.cpp`
  - **Estimated scope**: Medium (2-3 文件)

### Checkpoint: Phase 2
- [ ] Layer 2 六项切片逐条对照 spec 的 Success Criteria 手动验证通过。
- [ ] 与用户走一遍完整演示流程（属性切换→悬浮查看→框选放大→保存书签→
      多视口对比），确认交互手感符合"Adobe 风格"的预期。

### Phase 3: 保真验证（Layer 3，依赖 Phase 1 的阶梯参数定稿）

- [ ] **Task 18: 设计并实现 LOD 保真量化指标**
  - **Description**：实现抽稀前后 elevation 分布的统计偏差对比（如直方图差异）
    和关键特征点（局部极值/fold 边界）保留率计算，作为可重复运行的脚本/测试。
  - **Acceptance criteria**:
    - [ ] 输入两个 LOD 级别的数据，输出量化偏差数字。
  - **Verification**: 单元测试覆盖一个已知合成小数据集（构造已知偏差，验证脚本
    能测出预期数字）。
  - **Dependencies**: Task 4（阶梯参数定稿）
  - **Files likely touched**: 新文件 `tests/LodFidelityTests.cpp` 或独立脚本
  - **Estimated scope**: Medium (2 文件)

- [ ] **Task 19: 跑遍现有 LOD 阶梯并记录结果**
  - **Description**：用 Task 18 的工具跑现有 3 级 LOD 阶梯（及 Task 4 后的新阶梯），
    把保真数字记录进文档，作为演示/答辩时可以拿出来的证据。
  - **Acceptance criteria**:
    - [ ] 文档列出每级 LOD 的保真数字。
  - **Verification**: 文档存在且数字非占位符。
  - **Dependencies**: Task 18
  - **Files likely touched**: `docs/benchmark/lod-fidelity.md`（新建）
  - **Estimated scope**: Small (1 文件)

### Checkpoint: Complete
- [ ] spec 的全部 Success Criteria 逐条打勾。
- [ ] 基准/内存/保真三份记录文档（`docs/benchmark/*.md`）齐全，可作为答辩材料。
- [ ] 与用户确认是否需要核显验证（Layer 1 C，backlog，视设备情况插入）。

## Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Task 1 新增 CMake 可执行目标需要"Ask first" | Low（流程性） | 开工前单独确认一次，不影响任务内容本身 |
| Task 6（闪烁修复）范围未知，可能是 L 级 | Medium | Task 5 审查结论出来后单独评估，必要时再拆分 |
| 1 亿点真实数据未到位前，Task 3/4 的预算只是推算 | Medium | 拿到赛方/更大数据后用 Task 1 工具复测校准（已在 spec Resolved Decisions 里确认这个顺序） |
| 核显机器不确定 | Low（已降级为 backlog，不卡主线） | 见 Layer 1 C 的条件触发处理 |

## Open Questions

- Task 1 的基准工具命名/位置（`tests/BenchmarkSuite.cpp` vs `tools/Benchmark.cpp`）
  —— 待开工前确认，不影响任务拆分本身。
- Task 9/13 涉及的新文件命名（`BoxSelect`/`AxisGrid` 等）是占位名，实现时可按
  代码库惯例调整，不算偏离计划。
