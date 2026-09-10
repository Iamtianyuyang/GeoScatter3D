---
feature: ffi-backend-bridge
status: delivered
updated: 2026-09-10
branch: flutter
commits: 07c02c2..working-tree
---

# 后端 C 全量 FFI 化（Flutter 可调用）

## Report

**What was built** — 在双进程控制面架构下，将后端能力收敛为稳定 C-ABI：A 组本地引擎 API + B 组 ViewerBridge/运行时控制。Flutter 经 `dart:ffi` 调用；`viewer_control_client` 不再持有 TCP socket。服务层类型化 FFI 优先、返回码非 0 回退 RPC；导出改为异步 execute+轮询，避免 UI isolate 阻塞。Viewer 连接超时会 `TerminateProcess`，避免孤儿进程。

**Verification** —
- `flutter test`（含 `GS3D_FFI_E2E=1`）：59 通过
- `ctest --test-dir tmp/build-win -C Release`：43/43
- `check_include_deps.py` / `check_engineering_guardrails.py`：OK
- B 组符号探测 31/31；结束后无 GeoScatter3D 残留进程

**Journey log** —
1. ViewerBridge 健康检查曾因 `connected_` 未置位导致 ping 永远失败 → 临时置位后 rpc。
2. 连接超时只 CloseHandle 会留下孤儿 Viewer → 补 TerminateProcess。
3. `export_via_viewer` 同步 sleep 最长约 2 分钟，不可在 UI isolate 调用 → Dart 改为异步轮询。
4. 类型化 FFI 忽略返回码会静默成功 → 仅 rc==0 视为已处理，否则 RPC 回退。
5. e2e/ctest 并发会锁住 sample bundle 文件 → 门禁前清理残留进程。

## [S1] Problem

Flutter Desktop 前端与 C++20/Vulkan 引擎协同，但「后端能力」分散在三处：

1. **本地引擎状态**（`gs3d_ffi.dll` 内 `EngineState`）：数据集元数据、GPU、最近工程、文件对话框、预览点。
2. **运行时渲染/交互**（独立进程 `GeoScatter3D.exe` 控制面 JSON-RPC）：相机、拾取、测量、瓦片、截图、导出。
3. **UI 动作层**（Dart `executeAction`）：部分仅改 Flutter 本地状态，部分转发原生。

结果：

- Flutter 不应直接持有 TCP socket / 子进程句柄（R2）；当前已有 `ViewerBridge`，但类型化契约与绑定/服务路由尚未完全收敛。
- 旧 DLL / 无 Viewer / 无 DLL 三条路径行为不一致，错误语义不清晰。
- 缺少一份**权威 FFI 契约**：哪些符号必须存在、返回码含义、字符串所有权、线程与生命周期。

目标：在**双进程控制面架构**下，把后端能力全部收敛为稳定 C-ABI，Flutter **只调用 FFI**（类型化优先，`viewer_rpc` 作通用逃生舱），并保证双轨回退与测试门禁。

## [S2] Design

### 2.1 架构（选定：双进程控制面 FFI 化）

```text
Flutter UI / GeoScatter3dService
        │  Dart FFI (dart:ffi + package:ffi)
        ▼
gs3d_ffi.dll  ── 纯 C-ABI（geoscatter3d_ffi.h）
   ├── 本地引擎：dataset / GPU / recent / dialogs / preview analysis
   └── ViewerBridge（进程生命周期 + TCP JSON-RPC 封装）
              │
              ▼
       GeoScatter3D.exe --headless --control-plane
              └── ComponentRegistry + Vulkan 渲染
```

**不嵌入 Vulkan 进 Flutter 进程**（非本阶段）。句柄不穿透（R2）保持不变：Flutter 只见 PNG/JSON/float buffer。

### 2.2 C-ABI 契约规则

| 规则 | 约定 |
|---|---|
| 链接 | `extern "C"` + `GS3D_FFI_API`；仅 `stdint`/`stdbool` |
| 整型返回 | `0` 成功；负数失败。统一码见 §2.3 |
| 字符串返回 | `const char*`：thread_local 缓冲或 `gs3d_ffi_free_string` 可释放指针；Dart 读取后立即拷贝，不得缓存裸指针 |
| 字符串入参 | UTF-8，调用期间有效；实现内必须拷贝 |
| JSON 入参/出参 | 单对象 UTF-8 JSON；未知字段忽略；错误用 `{"error":...}` 或负返回码 |
| 可选符号 | Viewer 桥与 §19 运行时符号在旧 DLL 上可缺失；Dart 侧 `optLookup`，缺失时回退 |
| 线程 | 本地引擎函数内部加锁；Viewer 桥 RPC 同步阻塞（≤5s）；禁止在 Flutter UI isolate 做长阻塞（导出轮询须 async） |
| 生命周期 | `init` → 业务 → `viewer_start`/… → `viewer_stop`/`quit_viewer` → `shutdown`；重复 start 先 stop |

### 2.3 统一返回码（int32_t）

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| -1 | 空指针/非法参数 |
| -2 | 目标不存在 / Viewer 未连接（按函数文档区分） |
| -3 | 内部执行失败 / JSON-RPC error |
| -4 | 状态冲突（如已占用、同时 rotate+pan） |
| -5 | 超时 / 启动失败（CreateProcess 等） |
| -6 | 子进程提前退出 |
| -7 | 连接超时 |
| -8 | 句柄无效 |

### 2.4 符号分层（必须齐全）

**A. 本地引擎（无 Viewer 也可用）**

- 生命周期：`init` / `shutdown` / `get_version`
- 数据集：`load_dataset`、`is_dataset_loaded`、`get_dataset_name`、`get_point_count`、`get_file_size_string`、`get_bounding_box`、`get_value_range`
- LOD/属性：`get_lod_*`、`get_attribute_*`
- 最近工程：count/path/timestamp/remember/remove/clear
- GPU：count/name/type/active/set_preferred
- 预览点流：`get_points`
- 本地渲染镜像：`set/get_point_size`、`set/get_colormap`、`set/get_scalar_range`
- 对话框/拖放：`pick_file`、`pick_folder`、`init_drag_drop`、`poll_dropped_file`、`set_dropped_file`
- 本地分析：`pick_point`、`calculate_region_stats`、`get_nav_map_thumbnail`、`export_dataset`、`clear_cache`、`get_performance_metrics`（无 Viewer 时可为 stub，必须文档化）
- 本地 JSON 通道：`execute_command`（ping / get_dataset_summary / …）

**B. Viewer 桥（连接后可用）**

- 进程：`viewer_start` / `viewer_stop` / `viewer_is_connected` / `viewer_last_error` / `viewer_startup_log` / `quit_viewer`
- 通用：`viewer_rpc` / `viewer_execute` / `viewer_get_state` / `viewer_screenshot_json` / `list_components_json`
- 运行时类型化：`set_theme`/`cycle_theme`、`set_camera_linked`、`request_new_view`、`request_new_workspace`、`restore_workspace`、`set_panel_visible`、`open_native_file_dialog`、`open_native_bundle_dialog`、`request_native_screenshot`
- 状态 JSON：`get_status_bar_json`、`get_interaction_snapshot_json`、`get_runtime_diagnostics_json`、`get_render_settings_json`
- 控制：`set_render_settings_json`、`camera_input`、`reset_camera`、`gizmo_axis`、`pick_input`、`measurement_op`、`clear_tile_cache`、`export_via_viewer`

### 2.5 Dart 绑定契约（`geoscatter3d_bindings.dart`）

1. A 组符号：**强制 lookup**，缺失即打开失败（与当前一致）。
2. B 组符号：`optLookup`，整包 `hasViewerBridge => viewer_start_opt != null`。
3. 所有 `Pointer<Utf8>` 返回值用后即拷；入参 `toNativeUtf8` + `calloc.free`（try/finally）。
4. 禁止在 bindings 层写业务状态；只做符号与类型映射。

### 2.6 服务层路由契约（`GeoScatter3dService`）

统一入口 `_sendNativeCommand`：

```text
若 Viewer 未连接 → 丢弃（本地双轨）
若存在类型化 FFI 且参数可映射 → 调用类型化符号
否则 → viewer_execute / viewer_rpc
成功后 → refreshNativeViewport（快照优先 get_interaction_snapshot_json）
```

要求：

- `executeAction` 的全部 workbench 动作在「已连接 Viewer」时最终落到 FFI（类型化或 viewer_*）。
- 快照、诊断、导出、清缓存、测量操作**禁止**再新增裸 TCP 路径。
- 无 DLL / 无 Viewer：行为与 R3 一致，不抛未捕获异常。

### 2.7 ViewerBridge 约束

- 子进程 `CreateProcess`，工作目录 = exe 父目录；参数：`--no-welcome --headless --control-plane=<port> [--no-tiles --no-lod] --input|--bundle <path>`。
- 端口：本地 bind(0) 预留后关闭再启动，避免竞态。
- 健康检查：socket 连通后必须能 `ping`→`pong`（实现须允许 rpc 在健康检查期间发送）。
- 失败：`last_error_` 必须含可操作信息（退出码 / pong 全文 / CreateProcess GetLastError）。
- `stop`：优先 `quit` RPC，超时 `TerminateProcess`；无孤儿进程（R5）。

### 2.8 测试边界

| 层 | 手段 |
|---|---|
| 无原生 | `flutter test` 全量；动作分发、双轨、绑定 optional 不崩 |
| 符号 | `hasViewerBridge`、ctypes/dumpbin 导出抽查 |
| 真实 Viewer | `GS3D_FFI_E2E=1` + `GS3D_RUNTIME_DIR`；启动→渲染设置→主题→快照→PNG→导出→list_components |
| C++ | ctest 43+；include deps / guardrails |
| 运行时暂存 | Release 目录必须含 `config/`、`assets/shaders/`、exe、dll |

### 2.9 错误与可观测性

- FFI 失败不得 silent-success：类型化返回非 0 时服务层记录 `_nativeViewerError` 或回退 RPC 并保留错误。
- `viewer_last_error` / `startup_log` 必须在 Flutter 启动失败 UI 可读。

## [S3] Out of Scope

- 将 Vulkan 渲染循环嵌入 Flutter 进程 / 同进程纹理共享（后续里程碑）。
- 数据集热切换、保存另存、首选项完整产品化（后端仅有 flag 时不做 UI 造假）。
- 空间裁剪与 EDL/光照模型完整 UI（管线 flag 未接线）。
- 非 Windows 平台 Viewer 桥（当前 Windows-only）。
- 重写 ImGui（已移除）或更改 GS3D 磁盘格式。

## Tasks

- [x] T1: 冻结并文档化 `geoscatter3d_ffi.h` 契约（返回码、字符串所有权、A/B 组、线程）— acceptance: 头文件分组注释与 §2.3/§2.4 一致，无未文档化导出 (covers: S2.2, S2.3, S2.4)
- [x] T2: 核对 ViewerBridge 启动/健康检查/stop/错误路径 — acceptance: ping 前可 rpc；失败 last_error 含退出码或 pong；无残留进程 (covers: S2.7)
- [x] T3: 确保 B 组符号全部导出并被 Dart optLookup — acceptance: 对新构建 DLL 探测 viewer_start/set_theme/camera_input/pick_input/export_via_viewer/list_components 均存在 (covers: S2.4, S2.5)
- [x] T4: 收敛 Service 路由：所有 workbench 原生命令走 `_tryTypedFfiCommand` 或 viewer_* — acceptance: 源码中无新增直接 socket；快照/诊断/导出/清缓存/测量均经 FFI (covers: S2.6)
- [x] T5: 补齐/修正类型化实现内存与错误码 — acceptance: UTF-8 入参 free、非 0 码语义符合表；无泄漏/UB (covers: S2.2, S2.3)
- [x] T6: 运行时暂存与 e2e — acceptance: `GS3D_FFI_E2E=1` 全链路通过；Release 含 config/shaders (covers: S2.8)
- [x] T7: 全量门禁 — acceptance: `flutter test` 全绿；ctest 100%；include deps & guardrails OK；tmp 无残留孤儿 (covers: S2.8, S2.9)
- [x] T8: 更新集成文档（architecture / flutter-integration-audit 片段）— acceptance: 双进程 FFI 路径与启动失败排查步骤可照做 (covers: S2.1, S2.9)
