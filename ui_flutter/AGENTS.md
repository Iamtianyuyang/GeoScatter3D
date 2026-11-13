# GeoScatter3D Flutter 前端治理规范

## 角色

**flutter-desktop-engineer / cross-platform-ui-architect**：维护 `ui_flutter/` 的界面、
状态中枢、交互视口与 C ABI 调用。输出必须能过工程护栏，代码高内聚低耦合。

## 红线

1. **状态单向数据流**：场景状态由 `GeoScatter3dService`（`lib/src/ffi/geoscatter3d_service.dart`）
   管理，界面用 `ListenableBuilder` 消费；预处理任务由 `PipelineService`
   （`lib/src/ffi/pipeline_service.dart`）管理。不许在组件里直接改引擎或服务内部字段。
2. **纯 C ABI 边界**：只经 `lib/src/ffi/gs3d_bindings.dart`（引擎 ABI）、`app_abi.dart`
   （显卡与最近项目）、`pipeline_bindings.dart`（无 GPU 管线）访问原生代码。
   不 import Vulkan 头，不在 Dart 侧持有图形句柄。
3. **只用 `gs3d_engine_*` 出帧**：视口只显示引擎回读的 RGBA 帧（Android 走 surface
   呈现）。engine 失败时用 `vulkan_engine_error` 明确报错，不做 Dart 软件渲染兜底。
4. **测试自清理**：测试产生的文件写进临时目录（`app_state.dart` 的平台目录或
   `Directory.systemTemp`），并在 `tearDown` 删除，不污染源码与工作区。
5. **动作入口统一**：视口与面板的用户操作收敛到 `service.executeAction(name, params)`，
   目前实现 `camera.input`、`camera.reset`、`pick.point`；新增动作在这里加 case，
   不要在 widget 里直接拼 ABI 调用。

## 结构与样式

- 结构：`layouts/main_shell.dart`（顶栏 + 52px 工具轨 + 挤压式面板 + 视口 + 状态条），
  `ShellSection` 决定面板，`_panelFor` 分派到 `widgets/*_panel.dart`。
- 样式：只用 `theme/app_tokens.dart` 的令牌（颜色、字阶、间距、圆角、动效）与
  `ui/ui_kit.dart` 的组件。不写裸 `Color(0xFF…)` 与非令牌字号；不加可选主题。
- 视口浮层只放视图轴、缩放、色标图例、测量读数；不做 HUD 卡片堆叠。

## 验证

```bash
cd ui_flutter
flutter analyze --no-pub            # 必须 0 error / 0 warning
flutter test --no-pub               # 60 项：54 通过、6 跳过
GS3D_ENGINE_E2E=1 \
GS3D_PIPELINE_LIBRARY=$PWD/../tmp/build-linux/src/ffi/libgs3d_pipeline_ffi.so \
LD_LIBRARY_PATH=../tmp/build-linux/src/ffi ../tmp/build-linux/src \
  flutter test --no-pub             # 60 项全跑（真实 GPU 与 JNI 管线）
```

## 测试约定

- 界面的数字必须来自引擎；widget 测试用 `test/support/fake_engine.dart`，不碰真引擎。
- `testWidgets` 里不要 `await` 真实文件 IO（FakeAsync 会挂住），也不要对带定时器的
  服务用 `pumpAndSettle`，用 `pump(Duration(...))`。
- 新增结构体镜像要同时在 `test/engine_state_layout_test.dart` 断言尺寸与偏移。
