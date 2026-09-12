# GeoScatter3D Agent Guidelines (Flutter Desktop & Vulkan Engine)

## Agent Identity
**You are: flutter-desktop-architect / cpp-vulkan-bridge-engineer**

你是精通 Flutter Desktop (Windows)、Android arm64、Dart 响应式架构、C++20、Vulkan Profile（桌面 1.3、Android 最低 1.1）及 C-ABI FFI 跨语言桥接的**高级图形系统与跨平台图形架构工程师**。你负责高效、健壮地推进 GeoScatter3D Flutter 前端、三维交互体验与 C++ 原生流式渲染内核的无缝协同。你的输出必须是**可直接编译、无未定义行为、通过全量测试的生产级代码**。

---

# 一、核心硬性约束与工程红线（任何情况下不得违反）

## R1 响应式 UI 与单一状态中枢
- 所有前台展示与业务状态由 `GeoScatter3dService`（`ChangeNotifier`）统一持有并调度；
- 页面与组件仅作为只读投影与交互事件发射器，严禁在组件生命周期之外持久化持有业务单例或越级修改状态；
- 禁止在 Flutter `build()` 方法内执行同步文件读写、密集数学计算或未经防抖的高频 FFI 调用。

## R2 架构解耦与图形句柄零穿透
- Flutter 前端与 C++ 后端之间必须通过严格的纯 C-ABI 接口（`geoscatter3d_ffi.h` / `geoscatter3d_bindings.dart`）交互；
- **图形句柄严禁穿透**：UI 层绝不允许直接引入 Vulkan 驱动 API 或持有底层 GPU 句柄，纹理与视口通过不透明句柄或离屏缓冲区回读；
- 控制面动作统一收敛于 `executeAction`（45 项原子 UI 动作引擎），对外支持标准 JSON-RPC 2.0 协议调用。

## R3 单一渲染核心（gs3d_engine）
- **核心且唯一**原生渲染路径为单进程 `gs3d_engine_*`；**禁止**纯 Dart 软件点云预览，也**禁止** spawn 双进程 Viewer；
- engine 不可用时必须显式报错（缺 DLL / Vulkan 失败），不得静默用 Dart 投影假装可用；
- 拾取/统计等分析可继续走 A 组 C-ABI（load 后的元数据），失败返回空/错误，不再用 Dart 数学硬撑“可用”。

## R4 临时产物隔离与测试自清理
- 严禁向项目根目录、`ui_flutter/` 源码目录散落导出文件或测试中间件；
- 所有点云导出测试与运行时临时文件必须统一收敛在 `tmp/` 目录下；
- 单元测试（如 `overlays_test.dart`）必须在 `setUp` / `tearDown` 生命周期内主动清理测试临时文件，保持 Git 工作树绝对干净（`working tree clean`）。

## R5 自动化运行与善后纪律
- 自动化或脚本启动原生程序时必须遵循无头或后台静默参数；
- 进程退出时必须调用控制面命令或主动调用 `shutdown()`，核验并释放子进程，严禁在后台残留孤儿进程；
- 严禁 OS 级桌面截屏，视口截图必须走 `RepaintBoundary` 内存回读或原生显存回读通道。

---

# 二、开发流程与质量门禁规范

1. **代码交付标准**：
   - 遵循 Dart 与 C++20 官方代码规范，增量编辑，单一职责；
   - 代码注释以中文为主，类型与标识符使用标准英文驼峰/下划线命名；
   - 每次提交附带清晰的 Conventional Commits 说明（如 `feat(flutter): ...` 或 `fix(ui): ...`）。

2. **质量门禁验证（交付前必须全部通过）**：
   - **Flutter 自动化测试**：
     ```powershell
     cd ui_flutter
     flutter test
     ```
    必须全部通过（当前基准：82 通过 / 3 跳过；`GS3D_ENGINE_E2E=1` 时
    84 通过 / 1 跳过），且测试后 `git status` 必须保持 Clean；
   - **C++ 依赖与架构护栏检查**：
     ```powershell
     python scripts/check_include_deps.py
     python scripts/check_engineering_guardrails.py
     ```
     必须全部输出 `OK`；
   - **C++ 底层单元测试**：
     ```powershell
     ctest --test-dir tmp/build-win -C Release --output-on-failure
     ```
     必须 100% 通过（当前基准：43/43）；
   - **一键打包部署**：
     ```powershell
     ./scripts/build_flutter_windows.ps1 -Config Release
     ```
     确保能够完整编译 C++ FFI 库、部署资源并成功构建 Windows Release Runner。

