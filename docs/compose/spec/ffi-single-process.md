---
feature: ffi-single-process
status: in-progress
updated: 2026-09-10
branch: flutter
commits: 
---

# 单进程渲染内核 FFI（多端可移植）

## Report

（交付后填写）

## [S1] Problem

当前「Flutter → gs3d_ffi.dll → spawn GeoScatter3D.exe → TCP JSON-RPC」的双进程架构在 Windows 桌面已可跑通，但无法支撑后续多端（Android / iOS）：

1. **移动端禁止**任意 spawn 独立可执行进程做渲染内核；应用包内只能加载一个动态库。
2. Flutter 已持 `dart:ffi`，但渲染状态机仍在外部进程，生命周期、端口、桌面会话依赖过重。
3. C-ABI 现有 B 组以 ViewerBridge/TCP 为中心，符号语义绑定「远程控制面」，不能直接下沉为同进程内核 API。
4. 双进程 PNG 回读 + JSON-RPC 往返对手机延迟与电量不友好。

目标：把 **Viewer 渲染循环与数据流式内核嵌入 `gs3d_ffi` 共享库**，Flutter 只加载一个库；Windows 先落地可交互视口，**公开 ABI 不含平台窗口/进程/句柄**，保证后续 Android/iOS 可复用同一契约。

保留原则（AGENTS）：

- **R2** 图形句柄不穿透：Flutter 只见像素缓冲 / 元数据 / 不透明 engine 句柄，不见 `VkDevice` 等。
- **R3** 双轨：无 Vulkan / 初始化失败时，Dart 预览数学与本地分析仍可用。
- **R5** 善后：`shutdown` 后无渲染线程、无 GPU 泄漏、无孤儿进程。

## [S2] Design

### 2.1 目标架构

```text
Flutter UI / GeoScatter3dService
        │  dart:ffi
        ▼
gs3d_ffi (单动态库)
   ├── Engine 实例（不透明 gs3d_engine*）
   │     ├── 数据会话 Gs3dDataset / LOD / Tile
   │     ├── Vulkan 渲染内核（离屏，无 GLFW 窗口）
   │     ├── GPU pick / 测量 / 区域统计
   │     ├── 导出 PLY/CSV
   │     └── 渲染线程 + 命令队列
   └── 本地辅助：GPU 枚举、最近工程、文件对话框（桌面可选）
```

**废弃路径（本阶段）**：Flutter 不再 `viewer_start` spawn `GeoScatter3D.exe`；ViewerBridge 可保留为**可选兼容出口**（`GS3D_FFI_LEGACY_VIEWER=1`），默认关闭，非多端依赖。

**不嵌入 UI**：无 ImGui；Flutter 仍负责全部界面。

### 2.2 生命周期与线程模型

| 阶段 | API | 线程 |
|---|---|---|
| 创建 | `gs3d_engine_create(config_json)` → `gs3d_engine*` | 调用线程 |
| 加载 | `gs3d_engine_load_dataset(engine, path)` | 可阻塞调用线程；大数据集内部线程池 |
| 交互 | `gs3d_engine_camera_input` / `pick_input` / `set_render_settings_json` | 任意 Dart 线程；入队即可 |
| 帧 | 内部渲染线程按需/定时离屏绘制 | 渲染线程独占 Vulkan |
| 读帧 | `gs3d_engine_read_frame_rgba` / `read_frame_png` | 调用线程；拷贝到调用方缓冲 |
| 状态 | `gs3d_engine_get_snapshot_json` / diagnostics | 调用线程；读原子/双缓冲快照 |
| 销毁 | `gs3d_engine_destroy` | 停渲染线程 → 释放 Vulkan → free |

约束：

1. **渲染线程独占**全部 `Vk*` 对象；其它线程只投递命令、读双缓冲结果。
2. 命令队列有界（默认 256）；满则丢弃最旧交互增量，保留最新相机/拾取（防手机端积压）。
3. `read_frame_*` 拷贝最近**已完成**帧；无帧时返回 `-2`（未就绪），不阻塞 UI isolate 超过 1 帧预算。
4. 禁止在公开头文件出现 `HWND` / `ANativeWindow` / `Vk*`；窗口接入属后续 `gs3d_engine_set_android_surface` 等扩展，不在本阶段。

### 2.3 公开 C-ABI（新增，前缀 `gs3d_engine_`）

与现有 A 组本地引擎 API（`gs3d_ffi_*`）共存；A 组保留数据/GPU/最近工程等无渲染能力。

```c
typedef struct gs3d_engine gs3d_engine;  // 不透明

typedef struct gs3d_engine_config {
  const char* preferred_gpu_uuid; // 可空
  uint32_t viewport_width;        // 离屏逻辑宽
  uint32_t viewport_height;
  const char* shader_dir;         // point.vert.spv / point.frag.spv
  int32_t enable_validation;      // 0/1
} gs3d_engine_config;

GS3D_FFI_API gs3d_engine* gs3d_engine_create(const gs3d_engine_config* cfg);
GS3D_FFI_API void gs3d_engine_destroy(gs3d_engine* e);

GS3D_FFI_API int32_t gs3d_engine_load_dataset(gs3d_engine* e, const char* path);
GS3D_FFI_API int32_t gs3d_engine_unload_dataset(gs3d_engine* e);

GS3D_FFI_API int32_t gs3d_engine_resize(gs3d_engine* e, uint32_t w, uint32_t h);
GS3D_FFI_API int32_t gs3d_engine_request_frame(gs3d_engine* e); // 触发一帧
GS3D_FFI_API int32_t gs3d_engine_set_continuous(gs3d_engine* e, int32_t on);

// 帧：RGBA8 紧密排列；stride_bytes 可为 w*4
GS3D_FFI_API int32_t gs3d_engine_read_frame_rgba(
    gs3d_engine* e, uint8_t* out, uint32_t max_bytes,
    uint32_t* out_w, uint32_t* out_h, uint32_t* out_stride);
GS3D_FFI_API int32_t gs3d_engine_read_frame_png(
    gs3d_engine* e, uint8_t* out, uint32_t max_bytes, uint32_t* out_len);

// 交互与设置：JSON 字符串入参，语义对齐现有 runtime.* 组件
GS3D_FFI_API int32_t gs3d_engine_camera_input(gs3d_engine* e, const char* params_json);
GS3D_FFI_API int32_t gs3d_engine_set_render_settings_json(gs3d_engine* e, const char* json);
GS3D_FFI_API int32_t gs3d_engine_pick_input(gs3d_engine* e, const char* kind, const char* params_json);
GS3D_FFI_API int32_t gs3d_engine_measurement_op(gs3d_engine* e, const char* op, const char* params_json);
GS3D_FFI_API int32_t gs3d_engine_reset_camera(gs3d_engine* e);
GS3D_FFI_API int32_t gs3d_engine_gizmo_axis(gs3d_engine* e, int32_t axis);

GS3D_FFI_API int32_t gs3d_engine_export(gs3d_engine* e, const char* path, const char* format); // ply|csv
GS3D_FFI_API int32_t gs3d_engine_clear_tile_cache(gs3d_engine* e);

GS3D_FFI_API const char* gs3d_engine_get_snapshot_json(gs3d_engine* e);   // interaction_snapshot
GS3D_FFI_API const char* gs3d_engine_get_diagnostics_json(gs3d_engine* e);
GS3D_FFI_API const char* gs3d_engine_get_dataset_json(gs3d_engine* e);
GS3D_FFI_API const char* gs3d_engine_last_error(gs3d_engine* e);
```

字符串规则沿用既有契约：返回 `const char*` 为 thread_local/引擎内缓冲，Dart 读完即拷；入参 UTF-8，实现内拷贝。

返回码沿用 `[S2.3]` 既有表（见 `docs/compose/spec/ffi-backend-bridge.md`）。

### 2.4 实现分层（C++）

| 模块 | 职责 |
|---|---|
| `include/ffi/Gs3dEngine.hpp` + `src/ffi/Gs3dEngine.cpp` | 不透明引擎：线程、队列、双缓冲帧/快照 |
| `src/ffi/OffscreenViewerKernel.cpp`（或 `src/app/OffscreenViewerSession.cpp`） | 从 `ViewerApp` 抽出「无窗口」会话：VulkanContext → 无 swapchain 渲染 → OffscreenFramebuffer → PointPipeline → GPU pick |
| 既有 `ViewerApp` | 保留给 `GeoScatter3D.exe` GUI/控制面路径；**禁止**双向依赖 |
| `geoscatter3d_ffi.cpp` | 转发到 `Gs3dEngine`；旧 `viewer_*` 符号保留但文档标注 legacy |

关键抽取点：

1. **去掉 GLFW/swapchain 依赖**：离屏 pass 使用与 `PointPipeline` 兼容的 render pass；首版固定 `B8G8R8A8_UNORM` 或 `R8G8B8A8_UNORM`，与现 screenshot 路径一致。
2. **复用** `ViewerDatasetSession` / tile 流式 / `ViewerPickSystem` / `Gs3dExporter` / `MeasurementManager`，不重写算法。
3. 控制面 JSON-RPC **不必**在单进程路径启动 TCP；内部直接调用与 `runtime.*` 等价的 C++ 入口（避免再嵌一层 socket）。

### 2.5 帧与呈现契约

- 默认 **on-demand**：`request_frame` 或交互后渲染一帧；`set_continuous(1)` 用于动画/持续 LOD。
- Flutter `CenterViewport`：优先 `read_frame_rgba` → `Image.memory`/`RawImage`；失败回退 PNG；再失败 Dart 软件投影（R3）。
- pick / measure / region 在渲染线程执行，结果写入 snapshot 双缓冲；Flutter 10Hz 轮询或每帧后拉取。

### 2.6 数据与配置

- 数据集路径：`.gs3d.bundle` / `.gs3d`；加载逻辑与 FFI `load_dataset` / Viewer bundle 解析一致。
- Shader：`shader_dir` 指向含 SPIR-V 的目录；默认相对可执行文件或 `GS3D_SHADER_DIR`。
- GPU 选择：复用 `load_preferred_gpu_preference` / UUID；`create` 失败必须给出可读 `last_error`（无设备/校验层缺失/驱动过旧）。

### 2.7 Flutter 侧改造

`GeoScatter3dService`：

1. `startNativeRenderer()` 改为：
   - `hasEngineApi` → `engine_create` + `load_dataset`（新路径，默认）
   - 否则若 `hasViewerBridge` → 旧双进程（桌面兼容）
   - 否则纯 Dart 预览
2. 相机/拾取/设置：引擎路径调用 `gs3d_engine_*`；双进程路径保留现状。
3. 帧定时器：engine 路径 `request_frame` + `read_frame_rgba`；双进程路径仍 PNG RPC。
4. 导出/清缓存/快照：全部改走 engine API。

`NativeViewerControlClient`：仅 legacy 双进程使用，不再作为新功能入口。

### 2.8 构建与打包

- `gs3d_ffi` 链接 `gs3d_app`（或精简后的 kernel 目标）+ `Vulkan::Vulkan` + data/platform/util；**不**链接 glfw 于 engine 路径。
- 一键脚本部署：`gs3d_ffi.dll` + `assets/shaders/*.spv` +（可选）`GeoScatter3D.exe`。
- 未来 Android：同一 `gs3d_ffi` 源用 NDK 编 `.so`；Vulkan loader 系统提供；本阶段只要求 **Windows 编译与 e2e**，但不得在公开 API 写死 `_WIN32` 类型。

### 2.9 测试边界

| 层 | 内容 |
|---|---|
| 单元 | 命令队列有界丢弃、双缓冲快照、返回码、UTF-8 参数拷贝 |
| 符号 | `gs3d_engine_*` 全导出；`hasEngineApi` |
| e2e | `GS3D_ENGINE_E2E=1`：create → load sample bundle → set settings → camera → request_frame → RGBA 非空且宽高匹配 → pick/measure → export → destroy；结束后无线程/句柄泄漏迹象 |
| 回归 | 原 `GS3D_FFI_E2E` 双进程用例可跳过或标记 legacy |
| 门禁 | `flutter test`、ctest、include deps、guardrails |

### 2.10 错误与降级

- `create` 失败：返回 `NULL`；`gs3d_ffi_last_error`（全局）或后续 `engine_last_error` 不可用，错误写入进程级 thread_local 字符串 `gs3d_engine_last_global_error()`。
- `load_dataset` 失败：引擎可保留，允许重试；不得半初始化 Vulkan。
- 读帧未就绪 `-2`：Flutter 静默等待，不弹错误。
- 与 R3：引擎不可用时服务层状态 `isNativeRendererActive=false`，Dart 软件视口可用。

## [S3] Out of Scope

- Android/iOS 实机构建、ANativeWindow/CAMetalLayer 共享纹理呈现（仅保证 ABI 可移植）。
- 同进程纹理零拷贝进 Flutter（`TextureRegistry` / `SurfaceTexture`）— 后续优化。
- 数据集热切换 UI、保存另存、空间裁剪/EDL 产品化。
- 移除 `GeoScatter3D.exe` 或删除 ViewerBridge 符号（保留兼容）。
- macOS/Linux 官方打包验证（ABI 不写死 Windows 即可）。
- 更改 GS3D 磁盘格式或 Vulkan 着色语言。

## Tasks

- [x] T1: 抽取无窗口 HeadlessRenderKernel（无 GLFW/surface/swapchain）— acceptance: sample bundle 出 RGBA 帧，Python/F e2e 非全黑 (covers: S2.4, S2.5)
- [x] T2a: `gs3d_engine_*` C-ABI 最小集（create_simple/load/render/destroy/snapshot）— acceptance: 符号导出且 e2e 通过 (covers: S2.2, S2.3)
- [ ] T2b: 渲染线程 + 命令队列 + 双缓冲（当前 API 同步阻塞调用线程）— acceptance: UI isolate 不阻塞；队列有界丢弃旧交互 (covers: S2.2)
- [ ] T3: 导出/清缓存/完整快照字段接入 engine — acceptance: export PLY、pick/measure 与 runtime.pick 对齐 (covers: S2.4, S2.6)
- [x] T4: CMake 导出 engine 符号 + 部署 glfw3 同目录 — acceptance: openLibrary 可加载 (covers: S2.8)
- [ ] T5: Service 默认单进程路径 — acceptance: hasEngineApi 时不再 spawn Viewer (covers: S2.7)
- [ ] T6: Flutter 视口 RGBA 呈现与交互 — acceptance: 拖动后帧变化；pick 走 engine (covers: S2.5, S2.7)
- [x] T7a: `GS3D_ENGINE_E2E=1` create→load→render 通过；flutter test 58 全绿；ctest 43/43 (covers: S2.9)
- [ ] T7b: export/destroy/无线程泄漏完整门禁 (covers: S2.10)
- [ ] T8: architecture / 集成审计更新 (covers: S2.1, S2.8)

依赖：T1 → T2a → T5/T6；T2b 可紧随 T2a；T7 完整依赖 T5+T6。
