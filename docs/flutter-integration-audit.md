# Flutter 后端接入记录

更新：2026-09-12。当前权威路径为**单进程渲染内核 FFI**；双进程 Viewer 桥已删除。工作台交互（拾取/Gizmo/清缓存/PNG）经 `gs3d_engine_*` 收口；主题/面板/测量线为 Dart 本地状态。

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
- 能力：create/load/unload/resize、渲染设置、相机输入、Gizmo 预设、屏幕坐标拾取、`render_rgba`、`read_frame_png`、export ply/csv、clear_tile_cache、snapshot（camera/render/viewport）/诊断。
- 视口：RGBA 帧回读 → `ui.Image`；交互由手势直接 `camera_input` + 异步渲染。
- 拾取：engine 激活时走 `gs3d_engine_pick_point`（引擎真实 VP）；未激活时 A 组 `gs3d_ffi_pick_point` 兜底。
- 测量 / 主题 / 面板 / 布局：纯 Dart（ImGui 已移除）。
- 文件对话框：A 组 `pick_file` / `pick_folder`。
- 截图：engine 激活时 `engine_read_frame_png` 写 `tmp/`，否则 `RepaintBoundary` PNG。
- 门禁：`GS3D_ENGINE_E2E=1` 覆盖 pick / gizmo / clear / png / export / destroy 后重建。
- 契约：`docs/compose/spec/fullstack-engine-parity.md`（主）、`ffi-single-process.md`（历史）。

## 控制面收敛（2026-09-10）

- **Dart 重算消除**：`calculateStats()` 改为原生优先（`calculate_region_stats`，
  以数据集包围盒为选区），仅在引擎不可用时回退到 Dart 采样估算，
  并在返回值中标记 `source`（`native_region_stats` / `dart_sample`）。
- **未绑定 ABI 补齐**：`gs3d_engine_create`（完整 config：视口尺寸、首选 GPU
  UUID、着色器目录、validation 开关）与 `gs3d_engine_unload_dataset` 已绑定；
  `hasEngineApi` 收紧为四项必备符号齐备才为真。
- **空实现消灭**：删除 `_queueNativeCommand` / `_queueNativeRenderSettings` /
  `_synchronizeNativeMeasurementMode` 三个空壳转发；渲染设置统一直接走
  `_applyEngineRenderSettings()`。
- **动作目录端到端**：`test/action_coverage_test.dart` 遍历 `getAvailableActions()`
  全量动作，断言无「未知的动作 ID」、无 null 返回、均带 `success` 字段，
  并对数据相关动作（stats.get / stats.box / measure.calculate / export）做真机断言。
- **真实缺陷修复**：`_openWithLocalDeps` 中 `SetDllDirectoryA` 会改写相对路径
  基准，导致含 `..` 的候选路径加载失败（Windows error 126）；改为先绝对化再
  `DynamicLibrary.open`。`gs3d_engine_create` 在 `shader_dir` 为空时未回退到
  DLL 同目录资产路径，使 PointPipeline 退回相对 `assets/shaders`；已统一回退。

## 前端设计体系（2026-09-10）

- **单一令牌源**：`lib/src/theme/gs3d_tokens.dart` 是唯一视觉常量来源，
  分层为 `Gs3dPalette`（原始色板）→ `Gs3dTokens`（语义令牌）→
  `Gs3dType`（光学字号阶梯）→ `Gs3dMaterial`（玻璃/阴影）→
  `Gs3dMotion`（弹簧物理/动量投射）→ `Gs3dA11y`（无障碍上下文）。
  `Gs3dDark` 承载暗色分析舱色阶。
- **已删除兼容适配层**：`app_theme.dart` / `macos_tokens.dart` 移除，
  改由 `gs3d_theme.dart` 从令牌装配 `ThemeData`（含深色主题）。
- **流体交互组件库**：`lib/src/widgets/fluid.dart` 提供可中断弹簧数值
  （`Gs3dSpringValue`）、悬停/按压反馈（`Gs3dPressable`）、毛玻璃容器
  （`Gs3dGlass`）、令牌化卡片/菜单项/胶囊/状态点/指标磁贴，以及弹簧模态路由。
- **无障碍**：`Gs3dA11yScope` 经 `MaterialApp.builder` 注入，全局可读；
  减弱动效时停用装饰粒子流并归零过渡，降低透明度时毛玻璃退化为实心表面，
  高对比时描边增强。
- **材质取舍**：毛玻璃仅用于真正浮于视口之上的形态（悬浮 Dock / 分析舱 /
  模态 / 命令面板）；铺满整高的固定 chrome（顶栏 / 状态栏 / 左右停靠面板）
  使用实心表面 + 发丝描边，避免无视觉收益的合成开销。
- **覆盖范围**：欢迎页（Hero、三联操作卡片、最近工程、抽屉侧栏、顶栏、
  页脚、五个弹窗）与三种工作台布局的全部组件均已令牌化；除三维视口 HUD
  与科学色表阶梯（领域专用色）外，源码中不再有裸色值字面量。
- **Android 系统栏**：入口启用透明 Edge-to-Edge，外层使用工具栏表面填充
  系统栏区域，`SafeArea` 为顶部状态栏和底部手势区让位；平板安全区回归测试
  覆盖 24dp 顶部 / 34dp 底部 inset，避免底部状态栏再次与系统白条重叠。
- **紧凑手机布局**：针对 328×525 logical px 增加回归用例；顶部工具条收敛为
  图标入口，工程名弹性省略，左右抽屉按可用宽度收缩，视口标签与浮动工具条不再
  产生 `RenderFlex` 溢出。

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

## 验证结果（2026-09-12）

- Flutter：普通 `flutter test` 90 项通过、3 项 e2e 按环境变量跳过；设置
  `GS3D_ENGINE_E2E=1` 后 92 项通过、1 项跳过。
- 单进程 e2e：`GS3D_ENGINE_E2E=1` create→load→render→pick 命中→gizmo→clear→png→export ply/csv→destroy→重建通过；日志中的 GPU 名称仅代表当前测试主机，不作为移动端硬件文案。
- 重启流：`service_restart_flow_test` open→start→stop→re-open 通过。
- CTest：43 项全部通过。
- 两项工程护栏均为 OK。
- Android Maestro：Pixel_10_Pro_Fold 模拟器最新 APK 的 smoke/system 5/5 和
  data/SAF 6/6 flows 均通过，合计 11/11；覆盖欢迎页、平板手势、系统栏、SAF
  文件/目录导入、PLY/CSV 导出和 PNG 保存；测试 fixture 运行后已从设备 Downloads
  清理。
- Android AAB：使用临时 QA keystore 验证 release 编译、资源打包和签名校验链路
  通过；当前工作区未配置生产 `key.properties`/keystore，正式商店包仍必须注入生产
  keystore 后再执行 release 构建。

## 清理与剩余边界

src、include、cmake 和根构建文件已无 ImGui/ImPlot 引用；无 ViewerBridge / viewer_* 符号。
历史规划/分析文档可能仍提旧路径；双进程 FFI 规格已删除，以本文与
`fullstack-engine-parity.md` 为准。
单进程路径以 RGBA 回读呈现；超大数据 hover 拾取为 CPU 遍历（O(n)），大数据集性能可再优化。
主题多皮肤与零拷贝纹理传输不在本阶段。
