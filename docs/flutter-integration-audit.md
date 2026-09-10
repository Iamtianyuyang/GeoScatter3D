# Flutter 后端接入记录

更新：2026-09-11。当前权威路径为**单进程渲染内核 FFI**；双进程 Viewer 桥已删除。工作台交互（拾取/Gizmo/清缓存/PNG）经 `gs3d_engine_*` 收口；主题/面板/测量线为 Dart 本地状态。

## 连接路径（现行）

```text
Flutter UI / GeoScatter3dService
        │ dart:ffi
        ▼
  gs3d_engine_*  —— 唯一渲染与交互内核（同进程离屏 RGBA）
        │ 失败
  明确报错（无 Dart 软件预览、无双进程 Viewer）
```

路由入口：`GeoScatter3dService.startNativeRenderer()`（`hasEngineApi` 必需）。

## 单进程 FFI（2026-09-11）

- 公开 ABI：`gs3d_engine_*`（`include/ffi/geoscatter3d_ffi.h` §20），不透明 `gs3d_engine*`，无 HWND/Vk 句柄穿透。
- 能力：create/load/resize、渲染设置、相机输入、Gizmo 预设、屏幕坐标拾取、`render_rgba`、`read_frame_png`、export ply/csv、clear_tile_cache、snapshot（camera/render/viewport）/诊断。
- 视口：RGBA 帧回读 → `ui.Image`；交互由手势直接 `camera_input` + 异步渲染。
- 拾取：engine 激活时走 `gs3d_engine_pick_point`（引擎真实 VP）；未激活时 A 组 `gs3d_ffi_pick_point` 兜底。
- 测量 / 主题 / 面板 / 布局：纯 Dart（ImGui 已移除）。
- 文件对话框：A 组 `pick_file` / `pick_folder`。
- 截图：`RepaintBoundary` PNG 或 `engine_read_frame_png`。
- 门禁：`GS3D_ENGINE_E2E=1` 覆盖 pick / gizmo / clear / png / export / destroy 后重建。
- 契约：`docs/compose/spec/fullstack-engine-parity.md`（主）、`ffi-single-process.md`（历史）。

## 欢迎页已验证流程

- 新建：CSV/DAT → 真实 Preprocess → 工程包 → C-ABI 元数据和有界预览 → 原生渲染（单进程）→ 工作台。
- 参数：线程数用于转换和分块；XY/XYZ 与最细层目标点数用于 LOD。小型数据跳过空 LOD 文件，清单按实际产物记录。
- 打开：支持 bundle 和独立 GS3D；独立文件禁用外部分块/LOD 依赖。失败显示错误，只有原生首帧就绪才跳转。
- 示例：部署包含原始 CSV，首次点击自动构建，后续直接打开生成的工程。
- 最近工程：真实记录、打开、移除和清空，重开复用普通加载流程。
- GPU：Vulkan 查询真实设备，按 UUID 保存偏好，下一次打开工程生效；页脚显示实际引擎连接状态。
- 取消与防重：单个工程操作串行执行；取消等待子进程退出；专属 tmp 目录自动清理；同名工程拒绝覆盖。
- 中文与空格：FFI 和 TOML 路径使用 UTF-8；中文工程名与中文 DAT 文件通过真实测试。

耗时操作使用 `executeActionAsync`；JSON-RPC 使用 `executeJsonRpcAsync` 的 `ui_action` 方法。
欢迎页按钮、拖放、命令面板已接到异步入口。
开发时工程默认写入仓库 data；安装运行时写入用户 GeoScatter3D/projects。
`GS3D_PROJECTS_DIR` 可指定工程目录，`GS3D_RUNTIME_DIR` 可指定完整原生运行目录。

## F5 与构建

根目录 F5 运行 Flutter Windows Debug；预构建部署 FFI、Preprocess、依赖 DLL、配置、着色器和示例。
检测到 Visual Studio 生成器变化时，将旧 Flutter 生成目录移入 tmp 保留，再重新配置。
正常退出等待任务和原生进程；Runner 的 Windows Job Object 用于调试器停止时回收子进程。

## 验证结果（2026-09-11）

- Flutter：`flutter test` 55 项通过，3 项 e2e 按环境变量跳过。
- 单进程 e2e：`GS3D_ENGINE_E2E=1` create→load→render→pick 命中→gizmo→clear→png→export ply/csv→destroy→重建 通过（RTX 4070）。
- 重启流：`service_restart_flow_test` open→start→stop→re-open 通过。
- CTest：43 项全部通过。
- 两项工程护栏均为 OK。

## 清理与剩余边界

src、include、cmake 和根构建文件已无 ImGui/ImPlot 引用；无 ViewerBridge / viewer_* 符号。
历史规划/分析文档可能仍提旧路径；双进程 FFI 规格已删除，以本文与
`fullstack-engine-parity.md` 为准。
单进程路径以 RGBA 回读呈现；超大数据 hover 拾取为 CPU 遍历（O(n)），大数据集性能可再优化。
主题多皮肤与零拷贝纹理传输不在本阶段。
