---
feature: fullstack-engine-parity
status: delivered
updated: 2026-09-11
branch: flutter
commits: a8232f1..working-tree
---

# 单进程全栈功能收口（前后端完全实现）

## Report

**What was built** — 在 R3「gs3d_engine_* 唯一路径」下收口前后端：内核补齐屏幕坐标拾取（VP 矩阵 + 容差圆判定）、Gizmo 预设（顶/底视 up 向量防奇异）、清缓存软复位、PNG 编码；快照扩展 camera/render/viewport。删除全部双进程 Viewer 符号（C 头/cpp + Dart bindings）。Service 将 hover 拾取节流接 engine、测量/主题/面板/布局纯 Dart、对话框走 A 组 pick_file/folder、截图走 RepaintBoundary/engine PNG、Gizmo/清缓存走 engine API。

**Verification** —
- `flutter analyze`：0 error（9 处既有 warning）
- `flutter test`：55 通过 / 3 跳过
- `GS3D_ENGINE_E2E=1`：pick 未命中角点 rc=0、中心命中、gizmo 后 snapshot 相机变化、clear、PNG magic、export、destroy→重建 通过；`service_restart_flow` 通过
- `ctest -C Release`：43/43
- include deps / guardrails：OK

**Journey log** —
1. Review 发现 pick 首个候选无条件接受、无容差判定 → 改为 `dsq <= tol_sq` 圆内最近；e2e 补角点未命中与 gizmo 前后 snapshot 不等。
2. STB 写实现仅在 HeadlessRenderKernel.cpp（gs3d_ffi），ViewerAppScreenshot 在 gs3d_app，无 ODR 冲突。
3. 顶/底视 gizmo 用 up=Y + Y-epsilon 避免与视线共线。
4. hover 高频路径接 `queueNativePickThrottled`（33ms 合并），点击/测距仍同步 `pickPointAt`。

## [S1] Problem

双进程 Viewer 已从 C++ / Service 主路径删除，单进程 `gs3d_engine_*` 成为唯一渲染内核。但「前后端功能完全实现」尚未收口：

1. **Engine 交互能力缺口**：`gs3d_engine_*` 覆盖 create/load/相机增量/渲染设置/RGBA/export/snapshot，但缺少屏幕坐标拾取、测量辅助、Gizmo 预设、瓦片/数据缓存清理等接口；hover HUD 仍依赖 A 组简化正交投影 `gs3d_ffi_pick_point` + Dart 侧 az/el/zoom 参数，与 engine 透视相机在 `camera_input` 后会漂移。
2. **双进程残留**：`geoscatter3d_ffi.h` §18–19 仍残留 `export_via_viewer` / `list_components_json` / `quit_viewer` / 无句柄 `clear_tile_cache` / `get|set_render_settings_json` 等符号；Dart bindings 仍 optLookup 全套 `viewer_*` / `set_theme` / `pick_input` 等已删符号；Service `_tryTypedFfiCommand` 仍按双进程组件分发。
3. **动作链路断裂**：`queueNativePickThrottled` / `_synchronizeNativeMeasurementMode` / `_queueNativeCommand` 为空实现；`workbench.file.open_native`、`workbench.screenshot.native`、`workbench.view.new` 等动作仍声称「原生对话框/截图」，实际已无后端。
4. **快照字段过薄**：`gs3d_engine_get_snapshot_json` 仅有 `ready/point_count`，无法支撑状态栏/诊断面板/测量模式等 UI 投影。

目标：在 **R3 单进程唯一路径** 下，把工作台全部能力收敛为「engine 交互 API + A 组本地元数据 + Dart 纯 UI 状态」，删除一切双进程符号与死代码，使 45 项 `executeAction` 在 engine 激活时全部有可观察后端结果。

## [S2] Design

### 2.1 架构（冻结）

```text
Flutter UI / GeoScatter3dService
        │ dart:ffi
        ▼
gs3d_ffi.dll
  ├── A 组 gs3d_ffi_*     数据集元数据 / GPU / 最近工程 / 对话框 / pick_point 简化 / nav_map / export_dataset
  └── §20 gs3d_engine_*   唯一渲染与交互内核（同进程离屏 RGBA）
        无 viewer_*；无 spawn；无 Dart 软件点云预览
```

原则：

- **图形句柄不穿透（R2）**：仍只暴露 RGBA / JSON / 标量。
- **Dart 与 engine 相机双源**：交互增量走 `engine_camera_input`；hover/测距拾取在 engine 激活时改为 **engine 屏幕坐标拾取**（用引擎真实 VP 矩阵），不再用 A 组简化投影；A 组 `pick_point` 仅在未加载 engine 时作为无引擎分析兜底（允许返回空）。
- **主题 / 面板可见性 / 布局 / 测量线绘制 / 小地图点击**：ImGui 已移除，全部为 **Dart 本地状态**，不再请求原生。
- **文件对话框**：保留 A 组 `gs3d_ffi_pick_file` / `pick_folder`。
- **截图**：engine 激活时对当前 RGBA 帧做 PNG 编码并由 Dart 写盘；删除「原生菜单截图」语义。

### 2.2 新增 / 补齐 C-ABI（`gs3d_engine_*`）

在 §20 追加（统一返回码；字符串 thread_local / retain 缓冲）：

```c
/// 屏幕坐标拾取：使用 engine 当前相机 VP；命中填 [x,y,z,value] 返回 1，未命中 0，错误负。
GS3D_FFI_API int32_t gs3d_engine_pick_point(
    gs3d_engine* e, float screen_x, float screen_y,
    float tolerance_px, float* out_point_xyzv);

/// Gizmo 预设轴 0..5（-X/+X/-Y/+Y/-Z/+Z），与 workbench.gizmo.axis 对齐。
GS3D_FFI_API int32_t gs3d_engine_gizmo_axis(gs3d_engine* e, int32_t axis);

/// 清空点缓存 / GPU 上传水位（单进程无瓦片 LRU 时等价软复位）。
GS3D_FFI_API int32_t gs3d_engine_clear_tile_cache(gs3d_engine* e);

/// 当前帧编码 PNG；未渲染过返回 -2。
GS3D_FFI_API int32_t gs3d_engine_read_frame_png(
    gs3d_engine* e, uint8_t* out, uint32_t max_bytes, uint32_t* out_len);
```

**删除（§18–19 残留）**：`gs3d_ffi_export_via_viewer`、`gs3d_ffi_list_components_json`、`gs3d_ffi_quit_viewer`、无句柄 `gs3d_ffi_clear_tile_cache`、无句柄 `gs3d_ffi_get_render_settings_json`、`gs3d_ffi_set_render_settings_json`。渲染设置仅经 engine setter。

**快照扩展**（`gs3d_engine_get_snapshot_json`）：

```json
{
  "id": "engine.snapshot",
  "ready": true,
  "point_count": 0,
  "camera": {"azimuth": 0, "elevation": 0, "zoom": 1, "pan": [0, 0]},
  "render": {"point_size": 1, "colormap_index": 0, "point_shape": 0,
             "height_source": 0, "color_source": 0, "height_mult": 1,
             "value_clip": {"enabled": false, "min": 0, "max": 0}},
  "viewport": {"width": 0, "height": 0}
}
```

诊断 JSON 增加 `last_error`、`frame_ready`、`fps`（由连续帧间隔估计，无帧则 0）。

### 2.3 HeadlessRenderKernel 实现要点

| 能力 | 实现 |
|---|---|
| pick_point | 读 camera view/proj，世界点→NDC→像素，半径 `tolerance_px` 内取最近深度点；CPU 遍历 CPU 缓存点集（与现有 export/load 一致），无需 GPU pick pass |
| gizmo_axis | 映射 0..5 → look_at 预设（相对 bbox 中心与跨度），复用 `set_camera_look_at` |
| clear_tile_cache | 清空 CPU 点缓存副本的「可重建」标记 + 记录 diagnostics；不 destroy Vulkan（保持交互） |
| read_frame_png | 在已有 `last_rgba_` 上 stb_image_write 编码（补齐 `encode_last_frame_png` 未实现分支） |
| snapshot | 暴露 camera 状态（az/el/zoom/pan 或 pos/target）、render push 常量、viewport |

Camera 状态：`CameraController` 增加只读查询（azimuth/elevation/distance/pan），供 snapshot 与 pick 复用。

### 2.4 Dart bindings

- **删除**全部 `viewer_*`、`set_theme`/`cycle_theme`、`set_camera_linked`、`request_new_*`/`restore_workspace`、`set_panel_visible`、`open_native_*_dialog`、`request_native_screenshot`、`get_status_bar_json`、`get_interaction_snapshot_json`、`get_runtime_diagnostics_json`、无 engine 的 `camera_input`/`reset_camera`/`gizmo_axis`/`pick_input`/`measurement_op`/`clear_tile_cache`/`export_via_viewer`/`list_components_json`/`quit_viewer` 与对应 lookup。
- **新增** `engine_pick_point_opt`、`engine_gizmo_axis_opt`、`engine_clear_tile_cache_opt`、`engine_read_frame_png_opt`。
- `hasEngineApi` 仍要求 create/load/render/camera 齐全；pick/gizmo 为 optLookup。

### 2.5 Service 路由契约

| 动作类 | engine 激活时 | 无 engine |
|---|---|---|
| 相机增量 / 预设 / gizmo | `engine_camera_input` / `engine_set_camera_look_at` / `engine_gizmo_axis` | Dart 状态（允许） |
| hover / 点选 / 测距取点 | `engine_pick_point` → 更新 `_hoveredPoint` / measurement | A 组 `pick_point` 或空 |
| 测量线增删显隐 | **纯 Dart** `_measurementLines` | 同左 |
| 渲染设置 | `_applyEngineRenderSettings` | A 组 set_point_size 等镜像 |
| 导出 | `engine_export` | A 组 `export_dataset` |
| 截图 | `engine_read_frame_png` + Dart 写 `tmp/` 或用户路径 | 报错「无渲染帧」 |
| 清缓存 | `engine_clear_tile_cache` + A 组 `clear_cache` | A 组 `clear_cache` |
| 主题/面板/布局/叠加层 | **纯 Dart** | 同左 |
| 打开文件/包 | A 组 `pick_file`/`pick_folder` | 同左 |
| 诊断 | `engine_get_diagnostics_json` + A 组 performance | A 组 performance |

`_tryTypedFfiCommand` **删除**或缩窄为仅 engine 相关；`_queueNativeCommand` / `queueNativePickThrottled` 改为真正调用 engine pick（节流保留）。

`workbench.file.open_native` / `open_bundle_native` → 调 `pick_file`/`pick_folder` 并走现有加载流程，不再伪装「原生菜单」。  
`workbench.screenshot.native` → 与 `workbench.screenshot` 同路径（RGBA→PNG→保存）。  
`workbench.view.new` / `workspace.*` → 纯 Dart 重置相机/布局，返回真实消息。

### 2.6 相机与拾取一致性

- 手势：继续即时 `engine_camera_input`（已有）。
- hover：`pickPointAt` 在 `isEngineNative` 时改调 `engine_pick_point`，屏幕坐标 = 视口逻辑像素（与 resize 后离屏一致）。
- 预设视角：`setCameraView` 后对 engine 调 `engine_set_camera_look_at` 或 `engine_gizmo_axis`（二选一，实现取 gizmo 内部映射以避免双份矩阵逻辑）。

### 2.7 测试边界

| 层 | 内容 |
|---|---|
| 符号 | 新 DLL 无 `gs3d_ffi_viewer_*` / `export_via_viewer`；有 `gs3d_engine_pick_point` 等 |
| 单元 | bindings 无死 lookup；service 在 mock/无 DLL 下动作不抛 |
| e2e `GS3D_ENGINE_E2E=1` | create→load→camera→pick 命中→gizmo→clear→export→read_frame_png→destroy→重建 |
| 门禁 | `flutter test`；ctest；include deps；guardrails；工作树 clean |

### 2.8 错误与可观测性

- engine 失败：`_nativeViewerError` / `backendStatus` 可读；视口报错而非静默 Dart 预览（R3）。
- pick 未命中：返回 null / success=false，不记错误。
- PNG 未就绪 `-2`：截图动作返回失败文案。

## [S3] Out of Scope

- Android/iOS 实机、零拷贝 TextureRegistry。
- 瓦片 LOD 流式与真实 LRU（单进程全量点路径）；`clear_tile_cache` 仅语义软复位。
- 重新引入双进程或 ImGui。
- 主题产品化多皮肤（Dart 本地 index 即可）。
- GPU pick pass（CPU VP 拾取已满足 hover/测距精度需求）。

## Tasks

- [x] T1: C++ HeadlessRenderKernel + Gs3dEngineApi：pick_point / gizmo_axis / clear_tile_cache / read_frame_png / 快照扩展 — acceptance: e2e 中 pick 命中 sample 点、gizmo 后相机变化、png 非空、snapshot 含 camera/render (covers: S2.2, S2.3, S2.6)
- [x] T2: 清理 `geoscatter3d_ffi.h` / `geoscatter3d_ffi.cpp` 双进程残留符号与注释 — acceptance: 头文件无 viewer/export_via_viewer/无句柄 render_settings；guardrails OK (covers: S2.2)
- [x] T3: Dart bindings 删除双进程 lookup，新增 engine_pick/gizmo/clear/png — acceptance: 无 viewer_start 等字段；hasEngineApi 行为不变 (covers: S2.4)
- [x] T4: Service：pick 节流接 engine、测量纯 Dart、截图/清缓存/打开对话框/主题面板/gizmo 路由收口 — acceptance: 45 动作在 engine 路径无静默空操作；无双进程分支 (covers: S2.5, S2.6)
- [x] T5: engine_e2e 扩展 + 无 DLL 单元回归 + 符号探测 — acceptance: GS3D_ENGINE_E2E 全链路；flutter test 全绿 (covers: S2.7)
- [x] T6: 文档（architecture / flutter-integration-audit / 本 Spec Report）— acceptance: 描述与实现一致 (covers: S1, S2.1)

依赖：T1 → T2/T3 可并行 → T4 → T5 → T6。
