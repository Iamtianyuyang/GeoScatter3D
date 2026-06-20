# Tile 加载/驱逐过渡闪烁审查（Task 5）

对应 [docs/plan/geoscatter3d-tasks.md](../plan/geoscatter3d-tasks.md) Task 5。
**只读审查，不含代码改动**——产出问题清单，修复留给 Task 6。

## 结论：确认存在一个会导致明显"掉细节再补回来"的闪烁路径

不是 GPU 资源驱逐（LRU eviction）本身的问题——`PointCloudTileGpu::evict_to_budget`
（[PointCloudTileGpu.cpp:307-337](../../src/render/PointCloudTileGpu.cpp#L307-L337)）
只在新选中的 tile 已经全部上传完成、标记为 `pinned_tile_ids` 之后才驱逐旧 tile，
不会驱逐当前要显示的 tile，这部分设计是对的。

**真正的问题在 `ViewerApp::run()` 的 tile 选择切换逻辑**：一旦相机移动导致
`tile_result.changed`（选中了一组新的 tile id），代码会立即清空当前正在显示的
`viewport_tile_ids[view_index]`——**即使旧的全分辨率 tile 此刻仍然完整驻留在 GPU
上、完全可以继续渲染**。

## 具体路径

[ViewerApp.cpp:1233-1246](../../src/app/ViewerApp.cpp#L1233-L1246)：

```cpp
const bool saved_selection_ready =
    viewport_tile_ids[view_index] == tile_result.tile_ids &&
    std::all_of(..., has_resident_tile...);
if (!saved_selection_ready) {
    viewport_tile_ids[view_index].clear();       // <-- 旧 tile 立即从渲染列表移除
    viewport_tile_query_boxes[view_index].reset();
}
```

只要新选中的 `tile_result.tile_ids` 和当前显示的不完全一致（任何一次相机移动后
几乎必然如此），`saved_selection_ready` 就是 false，于是立即清空
`viewport_tile_ids[view_index]`——**不管旧 tile 是否还在 GPU 上**。

渲染端 [ViewerApp.cpp:1578-1601](../../src/app/ViewerApp.cpp#L1578-L1601) 的
`any_tile_resident` 判断遍历的正是这个被清空的 `selected_tile_ids`：

```cpp
const auto& selected_tile_ids = viewport_tile_ids[view_index];
const bool any_tile_resident = std::any_of(selected_tile_ids..., has_resident_tile...);
...
const bool tile_will_render = !interacting && any_tile_resident;
```

`selected_tile_ids` 一旦被清空，`any_tile_resident` 必然是 false（空区间的
`any_of` 恒为 false），`tile_will_render` 变 false，全分辨率 tile 立即停止绘制，
画面退回到底层 LOD（精度骤降）。直到新选择异步读取+上传完成（按
[baseline.md](baseline.md) 实测，这一步耗时 0.2-0.9 秒），细节才会"补回来"。

**触发条件**：每一次相机移动导致 tile 重选（几乎是每次有效拖拽/缩放后停下来的
那一刻），都会经历"全分辨率细节消失 → 0.2-0.9 秒后重新出现"的过程。这正是用户
要求"交互时不要出现屏幕的闪烁"里最可能被感知到的那种闪烁——不是花屏，是一次
明显的细节骤降再恢复。

## 影响范围

- 只影响"停止交互后等 tile 刷新"这个阶段，不影响 LOD 渐进式渲染本身（LOD 选择
  逻辑——`LodSelector`——是平滑分级渐进，没有这个问题）。
- 数据量越大（比如 1 亿点目标），单次 tile 重选的磁盘读取/上传时间只会更长
  （baseline.md 里 33MB 数据集的一次重选已经要 0.2-0.9 秒），这个闪烁窗口会更明显，
  不会自己变好。
- 不是闪烁/花屏（没有渲染出错误数据），是一次可感知的细节骤降再恢复，但仍然
  违反"交互后不要有视觉跳变"的验收标准。

## 建议修复方向（留给 Task 6，本任务不实现）

核心思路：**清空 `viewport_tile_query_boxes`（避免用旧的裁剪框误裁剪新视角）
可以立即做，但不要清空 `viewport_tile_ids`（继续显示旧 tile，直到新 tile 真正
就绪再整体切换）**。即把"新选择是否就绪"和"该不该继续显示旧内容"解耦：旧
tile 集合只在新集合完全上传完成的那一帧被替换，而不是在选择刚改变的那一帧被
清空。这与 `evict_to_budget` 已经在用的"新内容就位后才替换"模式是一致的，只是
现在这个模式没有应用到 `viewport_tile_ids` 这个渲染侧列表上。

具体要看：旧 tile 是否仍然 `has_resident_tile`（大概率是，因为
`evict_to_budget` 只在预算超出且新内容已上传时才驱逐）；如果仍驻留，
`viewport_tile_ids[view_index]` 应该继续指向旧集合直到 `sync.complete` 为真。

## Task 6：修复落地

去掉了 [ViewerApp.cpp](../../src/app/ViewerApp.cpp) 里"选择一变就立即清空
`viewport_tile_ids[view_index]`"的那段逻辑（原 1233-1247 行的
`saved_selection_ready` 判断 + 清空分支），旧 tile 集合现在会一直保持显示，
直到新选择的 `sync.complete` 为真时才整体替换（替换逻辑本来就存在，不用新写）。

验证：
- `ctest --test-dir build` 两个套件仍通过，无回归。
- `GeoScatter3DBenchmark` 用同样的 orbit+zoom 压力场景重跑，帧时间 P50/P95/P99
  （4.18/8.59/9.35ms）和重载延迟（~0.43s）都在噪声范围内与修复前一致，没有
  引入新的崩溃或性能退化。
- **局限**：这是一个无头环境，没法截图验证"画面上确实不再消失再出现"这个视觉
  结论，验证停留在"代码路径追踪 + 行为/性能无回归"这一层。真正的视觉确认需要
  在有屏幕的机器上跑 `GeoScatter3D` 交互对比修复前后的录屏。
- **未处理的边角情况**：当新选择处于多帧分批上传期间，`evict_to_budget` 只
  pin 住新批次里已就位的 tile，不会主动 pin 住正在显示的旧 tile；只要
  `resident_tile_budget_`（现在是 512，见 Task 4）远大于同时存在的新旧 tile
  总数，就不会有问题，但如果未来 tile 数量逼近这个预算上限，仍有极小概率被
  提前驱逐。如果发生，留作后续加固项（给 `sync_from_cached_tiles` 加一个"额外
  pin 集合"参数）。
