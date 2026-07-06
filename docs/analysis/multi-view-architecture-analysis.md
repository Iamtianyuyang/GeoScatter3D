# Geoscatter3D 多视图架构分析报告

> **状态**: 只读分析 | **日期**: 2026-07-07 | **作者**: Claude Fable 5
>
> 本报告不包含代码修改，仅做架构评估。目标：主窗口内分屏多视图 + 视图拖拽成独立 OS 窗口。

---

## 目录

1. [架构现状](#1-架构现状)
2. [单视图全局状态审计：Per-View vs 全局共享](#2-单视图全局状态审计)
3. [渲染管线分析](#3-渲染管线分析)
4. [独立 OS 窗口额外复杂度](#4-独立-os-窗口额外复杂度)
5. [交互分发](#5-交互分发)
6. [受影响功能逐一分析](#6-受影响功能逐一分析)
7. [分阶段建议与风险评估](#7-分阶段建议与风险评估)

---

## 1. 架构现状

### 1.1 当前多视图能力

**已有部分多视图支持**（比单视图架构更成熟）：

| 组件 | 现状 | 关键文件 |
|---|---|---|
| `ViewportManager` | 管理 N 个 `(Camera, OffscreenFramebuffer)` 对（上限 kMaxViewportCount=4），1-4 视口 | `include/render/ViewportManager.hpp:33` |
| `RenderViewState` | 每视口一份 UI 状态：hover、框选、测量投影、坐标轴、准星、gizmo | `include/app/AppState.hpp:159` |
| `ViewportFrameCmd` | ImGui→App 每视口鼠标事件 | `include/app/UiActions.hpp:9` |
| `CameraHub` | 观察者式相机同步，支持分组联动 | `include/camera/CameraHub.hpp:24` |
| `GpuPickReadback` | per-viewport 数组存储 pick 请求/结果 | `ViewerApp.cpp:2499-2541` |
| `CameraController` | 每视口一个独立实例 | `ViewerApp.cpp:2590-2597` |
| ImGui 视口窗口 | UiRoot::draw_viewport_window 画 N 个 ImGui 窗口，每个显示各自的离屏纹理 | `src/ui/UiRoot.cpp:355` |

**当前限制**：
- 多视口**共享同一个 LOD 级别**和**同一组瓦片选择**——瓦片选择只在一个"流式主视口"(`streaming_viewport_index`)上运行，结果跨所有视口使用
- 渲染设置 (point_size, color_by, height_by, colormap) **全局共享**

### 1.2 一帧的完整处理流程

```
1. window.poll_events()
2. 消费上一帧 GPU pick 结果（异步读回）
3. LOD 自适应反馈（report_frame_time）
4. 为所有视口计算 overlay 数据：
   - 测量线投影 (world→screen per viewport)
   - 世界坐标轴 / 地图轴 overlay
   - gizmo 轴方向
   - hover 点屏幕投影
   - 选中点屏幕投影
5. imgui_layer.new_frame(app_state) → 构建 UI + 返回 viewport_frames[]
6. 相机控制: 每视口 CameraController::update() + CameraHub::propagate()
7. LOD/Tile 选择（仅针对 streaming_viewport_index）
8. GPU pick 请求填充
9. renderer.draw_frame(window, callbacks):
   a. pre_pass: 导航图缩略图重渲 + 每视口 framebuffer.render()
      - LOD 安全网（最粗层）
      - LOD 当前层
      - Tile 叠加
      - GPU pick readback 录制
   b. in_pass: ImGui::Render() → 在 swapchain render pass 中画 UI
   c. post_pass: 截图 readback
10. imgui_layer.render_platform_windows()  // torn-out ImGui 视口窗口
11. 截图 PNG 写入（如待处理）
```

---

## 2. 单视图全局状态审计

### 图例

- ✅ **已是 Per-Viewport** — 无需改动
- 🔶 **Per-Viewport 存储但全局选择** — 需要让每视口独立选择
- ❌ **全局单例，需要 Per-Viewport** — 架构需要调整
- 🟢 **全局共享，应该保持** — 点云数据、GPU buffer 等

---

### 2.1 Camera & 投影

| 状态 | 说明 | 存储位置 |
|---|---|---|
| ✅ Camera (position, target, up, fov, ortho_height, projection_mode) | 已在 ViewportManager::Entry 中每视口一份 | `ViewportManager::entries_[]` |
| ✅ CameraController (orbit_pivot, animation, speed) | 每视口一个 CameraController | `ViewerApp.cpp:2590 controllers[]` |
| ✅ Viewport 尺寸 | Camera::viewport_width_/height_ | Per-Camera |

### 2.2 渲染目标

| 状态 | 说明 | 存储位置 |
|---|---|---|
| ✅ OffscreenFramebuffer (color + pick + depth) | 每视口一个 framebuffer | `ViewportManager::entries_[]::framebuffer` |
| ✅ VkDescriptorSet (ImGui texture) | 每个 framebuffer 自己的 descriptor | `OffscreenFramebuffer::imgui_descriptor_` |

### 2.3 渲染参数 (PointPushConstants)

| 字段 | 状态 | 影响 | 是否需要 Per-View |
|---|---|---|---|
| `mvp` | ✅ 每视口独立填充 | 在 pre_pass 中 per-viewport 设置 | 否 |
| `point_size` | ❌ 全局 `push.point_size` | 所有视口同一点大小 | **可改为 per-view** |
| `color_source` / `color_min` / `color_range` | ❌ 全局 | 所有视口同一着色 | **可改为 per-view** |
| `height_source` / `height_mult` / `height_offset` | ❌ 全局 | 所有视口同一高度映射 | **可改为 per-view** |
| `flags` (colormap, clip) | ❌ 全局 | 所有视口同一设置 | **可改为 per-view** |
| `clip_min[3]` / `clip_max[3]` (value clip) | ❌ 全局 | 所有视口同一数据裁切 | **可改为 per-view** |

> **评估**：当前架构中 `push` 是单个全局结构体。如果让不同视口有不同的 render settings（比如一视口看 fold 着色、另一视口看 elevation 着色），就需要 **Per-Viewport PointPushConstants**。这可以自然地在 pre_pass 的 per-viewport 循环中读取 per-view 的 push 配置。

### 2.4 LOD & Tile 选择

这是**当前架构中最关键的全局瓶颈**：

| 状态 | 说明 | 存储位置 |
|---|---|---|
| ❌ `tile_selection` | 单个 TileSelection 对象，只对 `streaming_viewport_index` 做选择 | 全局局部变量 |
| ❌ `tile_result` | 单个 TileSelectionResult | 全局 |
| ❌ `tile_selection_dirty` | 单个脏标志 | 全局 |
| ❌ `lod_selector` | 单个 LodSelector，追踪"是否交互中" | 全局 |
| ❌ `lod_level_for_frame` | 单个 LOD 级别 | 全局 |
| ❌ `streaming_viewport_index` | 唯一活跃视口——瓦片选择、键盘快捷键（R/F）都针对它 | 全局 |
| ✅ `viewport_tile_ids[kMaxViewportCount]` | 每视口瓦片 ID 存储 | Per-Viewport 数组 |
| ✅ `viewport_tile_query_boxes[kMaxViewportCount]` | 每视口 clip bbox | Per-Viewport 数组 |
| 🔶 `tile_load_future` / `tile_loading_ids` / `debounced_tile_ids` | 异步瓦片加载状态机——当前仅一套 | 全局 |

> **评估**：要让多视口有不同视野区域时各自需要不同的 tile，需要：
> 1. 每视口一个 `TileSelection` 对象
> 2. 每视口独立的 `tile_result`
> 3. 每视口独立的 `tile_selection_dirty` 标志
> 4. 每视口独立的 `LodSelector`
> 5. 异步加载状态机需要支持合并多个视口的 tile 需求（去重后一起加载）

### 2.5 GPU Pick / Hover

| 状态 | 说明 | 存储位置 |
|---|---|---|
| ✅ `gpu_pick_requests[]` | 每视口 | Per-Viewport 数组 |
| ✅ `latest_gpu_hover_points[]` | 每视口 hover 结果 | Per-Viewport 数组 |
| ✅ `hover_timeout[]` | 每视口超时计数 | Per-Viewport 数组 |
| ✅ `consecutive_no_hit[]` | 每视口连续失败计数 | Per-Viewport 数组 |
| ✅ `gpu_pick_readback` | 内部每视口多帧缓冲 | `GpuPickReadback` 构造函数接受 `viewport_count` |

### 2.6 UI 状态 (RenderViewState)

| 字段 | 状态 | 说明 |
|---|---|---|
| `viewport_index` | ✅ Per-Viewport | |
| `visible` / `detached` / `render_requested` | ✅ Per-Viewport | |
| `camera_linked` | ✅ Per-Viewport | CameraHub 联动标志 |
| `box_select_*` / `stats_select_*` | ✅ Per-Viewport | 框选拖拽状态 |
| `hover_tooltip_*` / `hover_screen_*` | ✅ Per-Viewport | |
| `selected_point_*` | ✅ Per-Viewport | |
| `show_world_axis` / `show_map_axis` / `show_crosshair` | ✅ Per-Viewport | |
| `measurement_overlays[]` / `pending_point_*` | ✅ Per-Viewport | 世界坐标→屏幕投影 |
| `gizmo_*` | ✅ Per-Viewport | |
| `canvas_rect_*` | ✅ Per-Viewport | 截图用 |
| `axis_lines` / `axis_tick_labels` | ✅ Per-Viewport | |
| `map_axis_*` / `map_axis_origin_*` | ✅ Per-Viewport | |
| `descriptor` | ✅ Per-Viewport | ImGui 纹理句柄 |

### 2.7 测量 (Measurement)

| 状态 | 说明 |
|---|---|
| ❌ `MeasurementManager` | 全局单例，在 AppState 中（`app_state.measurement`） |

`MeasurementManager` 存储：
- `lines_` — 所有测量线的世界坐标点（viewport-independent，因为存的是 3D 坐标）
- `pending_first_point_` — 只有一个待定点
- `measure_mode_active_` — 全局模式

> **评估**：测量线的**存储**（世界坐标 3D 点）天然是 viewport-independent 的。每条线的投影是 per-viewport 计算的。但如果多视口允许**同时**在不同视口独立测量（不同视口各自有 pending point），那 `pending_first_point_` 和 `measure_mode_active_` 就需要 per-viewport。

### 2.8 点云数据 & GPU Buffers（应该全局共享）

| 状态 | 说明 |
|---|---|
| 🟢 `full_gpu_cloud` | 全量点云 GPU buffer — 所有视口都用同一个 |
| 🟢 `lod_gpu_cloud` | LOD 多层 GPU buffer — 共享（但每视口选不同层） |
| 🟢 `tile_gpu_cloud` | Tile GPU buffer — 共享（不同视口可见不同 tile 子集） |
| 🟢 `dataset` | 数据集元数据 — 共享 |
| 🟢 `bounds` | 场景包围盒 — 共享 |
| 🟢 `tile_reader` / `tile_index_view` | Tile 索引 — 共享 |
| 🟢 `tile_point_cache` | CPU 瓦片缓存 — 共享 |
| 🟢 `runtime_points_by_id` / `runtime_points_valid_by_id` | 运行时点查询表 — 共享 |
| 🟢 `point_pipeline` | Vulkan pipeline 对象 — 共享（编译一次，所有兼容的 render pass 都用） |
| 🟢 `attr_list` | 属性描述符 — 共享 |

### 2.9 导航图 (Navigation Map)

| 状态 | 说明 |
|---|---|
| ❌ `NavigationMapState` | 全局单例，一个缩略图 |

> **评估**：导航图显示整个数据集的顶视图 + 一个视野框。多视口时，可能需要显示每个视口各自的视野框。或者保持一个导航图、多个视野框。改动量不大。

### 2.10 区域统计 (Region Stats)

| 状态 | 说明 |
|---|---|
| ❌ `region_stats` | 全局单例结果 |
| ❌ `region_stats_future_` | 全局异步计算 |

> **评估**：区域统计来自框选操作。多视口下每个视口的框选都是独立的（`stats_select_*` 已在 per-viewport RenderViewState 中）。但统计结果面板只有一个。可以保留单一面板、显示最后触发的统计结果，或者在面板内按视口分 tab。

### 2.11 截图

| 状态 | 说明 |
|---|---|
| ❌ `screenshot_*` (staging, extent, pending) | 全局，一次一张 |

> **评估**：截图当前只支持单视口。多视口后需要支持"截取当前活跃视口"或"每个视口独立截图"。改动量为中等——需要在 post_pass 中选择正确的视口区域。

### 2.12 键盘快捷键

| 快捷键 | 当前行为 | 多视口下应改为 |
|---|---|---|
| R (复位) | 复位 `streaming_viewport_index` | 复位 hovered/focused 视口 |
| F (聚焦) | 聚焦 `streaming_viewport_index` 的选中点 | 聚焦 hovered/focused 视口 |
| Tab / Shift+Tab | 全局切换颜色/高度属性 | 可能改为切换 hovered 视口的属性（如果 per-viewport render settings） |
| +/- | 全局调整点大小 | 可能改为调整 hovered 视口 |
| Esc | 关闭窗口 | 保持不变 |

---

## 3. 渲染管线分析

### 3.1 当前流程

```
                    ┌────────────────────────────┐
                    │    VulkanRenderer            │
                    │    (swapchain render pass)   │
                    │                              │
 pre_pass (VkCmdBuf) │  in_pass (VkCmdBuf)        │ post_pass (VkCmdBuf)
 ┌─────────────────┐ │  ┌──────────────────┐      │ ┌──────────────────┐
 │ Nav Thumbnail   │ │  │ ImGui::Render()  │      │ │ Screenshot copy  │
 │ (if dirty)      │ │  │                  │      │ │ swapchain→buffer │
 │                 │ │  │ ImGui::Image()   │      │ │                  │
 │ Viewport[0]     │ │  │  ┌────────────┐  │      │ └──────────────────┘
 │  framebuffer    │ │  │  │ viewport 0  │  │      │
 │  .render()      │ │  │  │ offscreen   │  │      │
 │  - LOD safety   │ │  │  │ texture     │  │      │
 │  - LOD level    │ │  │  └────────────┘  │      │
 │  - Tiles        │ │  │  ┌────────────┐  │      │
 │  - GPU pick rec.│ │  │  │ viewport N  │  │      │
 │                 │ │  │  │ offscreen   │  │      │
 │ Viewport[N]     │ │  │  │ texture     │  │      │
 │  framebuffer    │ │  │  └────────────┘  │      │
 │  .render()      │ │  └──────────────────┘      │
 └─────────────────┘ │                              │
                    └────────────────────────────┘
```

### 3.2 多视图的渲染已经基本正确

`pre_pass` 中对每个视口的 `framebuffer.render()` 已经是独立的 render pass —— 不嵌套在 swapchain render pass 内。每个 framebuffer 有独立的 color/pick/depth 图像。

在 `in_pass` 中，ImGui 利用 `AddImage()` 把每个视口的离屏纹理显示在各自的 ImGui 窗口里。

**唯一需要改的渲染管线部分**：每个视口渲染时使用 per-viewport 的 `PointPushConstants`（而非当前的全局 `push`），以支持不同视口有不同着色。

### 3.3 不需要改的

- VulkanRenderer 的 swapchain 管理：只有一个主窗口 swapchain
- PointPipeline：一个 pipeline 对象，所有视口因为共享 VkFormat 而兼容
- render pass 结构：已经正确隔离

---

## 4. 独立 OS 窗口额外复杂度

### 4.1 主窗口内分屏（Phase 1）

**当前状态**：ImGui docking 已支持将视口面板拖出主窗口成为独立 ImGui 视口（`imgui_layer.render_platform_windows()` 已调用）。

**额外工作量**：
- 主要是**前面分析的 per-viewport 状态拆分**：每个视口独立 LOD/Tile 选择、独立 render settings
- ImGui 层面基本不需要改动——`draw_viewport_window` 自然支持多窗口

### 4.2 独立 OS 窗口（Phase 2）

**真正独立 OS 窗口**（每个视口创建独立 GLFW window + Vulkan swapchain）比主窗口内分屏复杂得多：

| 复杂度 | 说明 |
|---|---|
| **ImGui ViewportsEnable** | 启用 `ImGuiConfigFlags_ViewportsEnable` 后，ImGui 可以创建"平台窗口"——拖出到独立 OS 窗口。当前代码已调用 `render_platform_windows()`，但需要确认 `ViewportsEnable` 是否开启。如果开启，拖出的视口面板已经是独立 OS 窗口（但共享同一个 Vulkan device）。 |
| **独立 Swapchain** | 每个独立 OS 窗口需要自己的 swapchain。ImGui 的 ViewportsEnable 后端会管理这个，但会增加大量复杂度：present 多个 swapchain、处理多窗口的 resize/minimize。 |
| **Vulkan 多窗口** | 所有窗口共享同一个 VkDevice 和 VkPhysicalDevice，但每个窗口需要独立的 VkSurfaceKHR、VkSwapchainKHR、depth buffer、framebuffer。ImGui Vulkan 后端已经支持 ViewportsEnable。 |
| **事件分发** | 多 GLFW window 的 poll_events、鼠标回调需要区分窗口。如果使用 ImGui ViewportsEnable，ImGui 后端处理这个。但如果每个视口是独立的**点云渲染窗口**（不仅仅是 ImGui 窗口），就需要独立的 OffscreenFramebuffer → 独立 Swapchain 的显示管线。 |

### 4.3 技术路线选择

**方案 A：纯 ImGui ViewportsEnable 多窗口**
- 拖出的视口仍是 ImGui 管理的"平台窗口"
- 点云仍然在主窗口的 pre_pass 中离屏渲染，通过 ImGui::Image 纹理在子窗口中显示
- 优点：工作量小（现有框架几乎已支持）
- 缺点：子窗口的绘制受限于 ImGui 的帧循环；所有离屏渲染在主 pre_pass 完成

**方案 B：真正独立渲染窗口**
- 每个视口创建自己的 GLFWwindow + swapchain + VulkanRenderer
- 每视口独立 render loop（或在同一帧循环中依次渲染到各 swapchain）
- 优点：完全灵活
- 缺点：工作量巨大，接近重写窗口管理

> **建议**：Phase 2 用方案 A（ImGui ViewportsEnable）。当前架构已为此做好准备（`render_platform_windows()` 已调用）。

---

## 5. 交互分发

### 5.1 当前机制

`UiRoot::draw_viewport_window` 中：
- `ImGui::InvisibleButton("##ViewportCanvas", ...)` 占据视口可用区域
- ImGui 自动将 `IsItemHovered()`/`IsItemActive()` 分发给正确的前端窗口
- 鼠标事件被包装为 `ViewportFrameCmd`，通过 `UiActions::viewport_frames` 返回
- ViewerApp 遍历 `viewport_frames`，为每个视口独立更新 `CameraController`

### 5.2 多视口下已经是正确的

因为：
1. ImGui 的 hit-test 保证 `hovered`/`active` 只属于最顶层的可见视口（或鼠标实际所在的视口窗口）
2. `CameraController` 已经是 per-viewport 的
3. 相机联动通过 `CameraHub::propagate()` 实现——被操作的相机为主，其他跟随

### 5.3 相机联动

当前实现（`CameraHub`）：
- `camera_linked` toggle 在 UI 中 per-viewport
- `kIndependent = -1` 表示不跟随
- 同一 group（默认 group 0）内的相机通过 `propagate()` 同步
- 同步复制 position/target/up/perspective，但保留各自的 viewport 尺寸

> **多视口扩展**：当前设计（group 0 联动 / kIndependent 独立）已经足够。如果需要多组联动（例如视口 A、B 联动组 1，视口 C、D 联动组 2），当前 CameraHub 架构已支持（`set_group(view_index, group_id)`），只需要 UI 暴露 group 选择器。

### 5.4 Keyboard 快捷键分发给哪个视口

当前所有快捷键（R、F、Tab、+/-）都针对 `streaming_viewport_index`。
多视口后需要改为：
- 优先 `hovered` 视口（鼠标在哪个视口上）
- 或者引入"活跃视口"概念（类似 Blender 的 active camera 黄色边框）

---

## 6. 受影响功能逐一分析

### 6.1 GPU Pick (Hover)

**当前状态**：✅ 已 Per-Viewport
- 每个视口独立发出 `GpuPickRequest`
- 每个 framebuffer 有独立的 pick attachment
- `gpu_pick_readback` 内部 per-viewport 管理结果

**多视口改动**：无需改动。

### 6.2 测量 (Measurement)

**当前状态**：❌ 全局单例

**关键问题**：`pending_first_point_` 是单例——一次只能有一个待定点。

**改动方案**：
- **最小改动**：保持全局测量模式，测量线用世界坐标存储（viewport-independent），投影 per-viewport 计算（已有）。
- **完整方案**：每视口独立的 `pending_first_point_`，允许同时在不同视口做测量。`lines_` 可以继续全局（所有视口看到同样的线）。

**工作量**：小（最小改动）到中（完整方案）。

### 6.3 十字准线 (Crosshair)

**当前状态**：✅ 已 Per-Viewport（`RenderViewState::show_crosshair`）

在 `UiRoot.cpp` 中每视口独立绘制。无改动。

### 6.4 框选 (Box Select) & 区域统计

**当前状态**：✅ 框选 UI 状态 Per-Viewport（`box_select_*`, `stats_select_*` 在 RenderViewState 中）

框选的屏幕→世界坐标转换使用该视口的 Camera，已经正确。

**区域统计**：❌ 全局单例 `region_stats` 和单一 `region_stats_future_`。

**多视口改动**：统计结果面板可保持单一（显示最新完成的统计），或分视口显示。工作量小。

### 6.5 导航图 (Navigation Map)

**当前状态**：❌ 全局单例缩略图 + 单一视野框

**多视口改动**：
- 视野框从单一矩形改为多个矩形（每个 linked 视口一个），或者只显示 `streaming_viewport_index` 的视野
- 如果视口有独立 LOD/tile 且有独立着色，导航图缩略图可能需要对每视口重渲（不同 colormap）
- 工作量：小到中

### 6.6 截图

**当前状态**：❌ 全局单次截图

**多视口改动**：
- 需要支持"截取哪个视口"——建议对 hovered/focused 视口截图
- 或者每个视口独立截图
- UI 需要增加视口选择器，或在其他视口 UI 上增加截图按钮
- 工作量：小

### 6.7 DPI / 缩放

**当前状态**：✅ 全局 ui_scale（影响 ImGui 字体大小和各视口的 UI 元素）

**多视口改动**：不同 OS 窗口可能在不同 DPI 的显示器上。ImGui ViewportsEnable 天然支持 per-window DPI。工作量在启用 ViewportsEnable 时自动解决。

### 6.8 坐标轴 (World Axis / Map Axis / Gizmo)

**当前状态**：✅ 已 Per-Viewport
- `compute_axis_overlay()` → per-viewport `axis_lines`/`axis_tick_labels`
- `compute_map_axis_overlay()` → per-viewport `map_axis_*`
- `compute_gizmo_axes()` → per-viewport `gizmo_*`

无改动。

### 6.9 Tile 预加载 (Tile Preload)

**当前状态**：✅ 全局一次性操作

无改动——预加载仍然是全局的一次性全量加载。

---

## 7. 分阶段建议与风险评估

### Phase 1: 主窗口内独立多视图（4-6 周）

**目标**：每个视口可以有独立的 LOD 级别、独立的 tile 可见集合、独立的渲染设置

#### 需要改动（按优先级）：

| # | 改动项 | 工作量 | 风险 | 说明 |
|---|---|---|---|---|
| 1 | Per-Viewport `TileSelection` | 🟡 中 | 🔴 高 | 每视口独立 frustum cull 和 tile 选择。需要重构 `tile_selection`→`tile_selections[]`，并对多视口的 tile 需求做 union（避免重复加载同一 tile） |
| 2 | Per-Viewport `LodSelector` | 🟡 中 | 🟡 中 | 每视口独立 LOD 选择（不同视口缩放不同，应显示不同 LOD 层）。需要 `lod_selectors[]` |
| 3 | 异步 tile 加载合并 | 🟡 中 | 🔴 高 | 多视口可能需要不同 tile 集合。加载状态机需要去重后合并加载（union of all desired tiles），然后各自显示各自可见的子集 |
| 4 | Per-Viewport `PointPushConstants` | 🟢 小 | 🟡 中 | 在 pre_pass per-viewport 循环中使用各自的 push 配置。如果让不同视口有不同着色时需要 |
| 5 | 键盘快捷键分发到 hovered 视口 | 🟢 小 | 🟢 低 | R/F/Tab/+/- 改为操作 hovered 视口而非 streaming_viewport_index |
| 6 | 导航图多视野框 | 🟢 小 | 🟢 低 | 在缩略图上画多个视野矩形 |
| 7 | Measurement per-viewport pending | 🟡 中 | 🟢 低 | 如果允许同时测量 |

**Phase 1 总评估**：
- 核心复杂度在于 tile/LOD 选择从"一个全局"拆成"每视口独立 + 需求合并"
- 渲染管线基本不需要动（pre_pass per-viewport 循环已正确）
- 点云数据 / GPU buffer 共享不变

### Phase 1 替代方案（最小可行性）

如果不想做完全的独立 LOD/tile per viewport：

**共享 LOD/tile，独立相机**（当前多视图已有基础）：
- 所有视口共享同一个 LOD 级别和同一个 tile 集合
- 但各自有独立相机/投影
- 限制：所有视口看同一区域时才合理（联动相机场景）
- 改动量：极小（基本已经是现在的能力，`camera_linked = true` 时已如此）

### Phase 2: 独立 OS 窗口（3-5 周）

#### 方案 A：ImGui ViewportsEnable（推荐）

| 项 | 说明 |
|---|---|
| 开启 `ImGuiConfigFlags_ViewportsEnable` | 需要在 ImGuiLayer::init() 中设置 |
| 当前 `render_platform_windows()` | 已调用，但只在 ViewportsEnable 时生效 |
| 多 swapchain 管理 | ImGui 后端管理平台窗口的 swapchain；主渲染仍在主窗口 |
| 工作量 | 🟡 中（主要是配置 + 测试 + 修复边缘情况） |
| 风险 | 🟡 中（ImGui ViewportsEnable + Vulkan 的兼容性，需要测试各种 GPU 驱动） |

#### 方案 B：独立 GLFW 窗口 + 独立渲染

| 项 | 说明 |
|---|---|
| 每视口独立 GLFWwindow | 需要窗口管理器 |
| 每视口独立 Vulkan swapchain | 需要 swapchain 管理器 |
| 独立 render loop | 可能是同一线程顺序渲染多个 swapchain，或独立线程 |
| 工作量 | 🔴 大（3-5 周） |
| 风险 | 🔴 高（多 swapchain 同步、跨窗口 ImGui 上下文共享、驱动兼容性） |

> **强烈建议 Phase 2 采用方案 A**。

### 总体路线图

```
Phase 0 (当前): 多视口相机 + 共享 LOD/tile + ImGui 内分屏
         |
         v
Phase 1 (4-6 周): 独立 LOD + Tile 选择 per viewport
         |         独立渲染设置 (optional)
         |         独立测量 (optional)
         |
         v
Phase 2 (3-5 周): ImGui ViewportsEnable
         |         独立 OS 窗口
         |
         v
Phase 3 (future): Per-view 着色设置
                  多组相机联动
                  视口同步/复制
```

### 最大风险项

1. **Tile 加载状态机重构**（Phase 1 核心）——当前异步加载是单例状态机，改为多视口需求合并后容易引入竞态或死锁
2. **ImGui ViewportsEnable + Vulkan**（Phase 2）——不同 GPU 驱动对此的支持程度不一，需要广泛测试
3. **深度 attachments**——当前每 OffscreenFramebuffer 有自己的 depth buffer。多视口 resize 时可能遇到 Vulkan 内存碎片化
4. **性能**——N 个视口 = N 次 render pass + N 次 pick readback + N 次 LOD 绘制。4 视口意味着 GPU 负载增加，需要测试帧率影响

---

## 附录 A：关键文件索引

| 文件 | 关键内容 |
|---|---|
| `src/app/ViewerApp.cpp` | 主渲染循环、所有全局状态、tile/LOD 选择、pick、测量、交互 |
| `include/app/AppState.hpp` | AppState、RenderViewState、MeasurementManager、NavigationMapState |
| `include/app/UiActions.hpp` | ViewportFrameCmd 结构 |
| `include/render/ViewportManager.hpp` | ViewportManager、Entry (Camera+Framebuffer 对) |
| `include/camera/CameraHub.hpp` | 相机联动 |
| `include/camera/CameraController.hpp` | 相机交互控制 |
| `include/camera/Camera.hpp` | Camera 类 |
| `include/render/OffscreenFramebuffer.hpp` | 离屏帧缓冲（color+pick+depth） |
| `include/render/VulkanRenderer.hpp` | 主 swapchain 渲染器，FrameDrawCallbacks |
| `include/render/TileSelection.hpp` | 瓦片选择 |
| `include/render/LodSelector.hpp` | LOD 选择 |
| `include/render/PointCloudTileGpu.hpp` | Tile GPU buffer |
| `include/gui/ImGuiLayer.hpp` | ImGui 层管理 |
| `src/ui/UiRoot.cpp` | 视口窗口绘制、鼠标事件映射、overlay 绘制 |

## 附录 B：Per-Viewport 数组索引约定

当前代码中 per-viewport 存储使用 `static_cast<std::size_t>(viewport_index)` 索引：
- `controllers[i]`
- `gpu_pick_requests[i]`
- `latest_gpu_hover_points[i]`
- `hover_timeout[i]`
- `viewport_tile_ids[i]`
- `viewport_tile_query_boxes[i]`
- `prev_rotate[i]`
- `mouse_down_x[i]` / `mouse_down_y[i]`
- `rotation_activated[i]`
- `selected_focus_points[i]`

所有数组大小为 `kMaxViewportCount = 4`。
