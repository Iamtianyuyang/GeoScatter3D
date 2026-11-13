# ui_flutter

GeoScatter3D 的前端。Flutter 只画界面和发命令，数据、渲染、任务状态都在 C++。

## 结构

```text
lib/
  main.dart                    MaterialApp + 主题装配
  src/theme/app_tokens.dart    颜色 / 字阶 / 间距 / 圆角 / 动效令牌
  src/ui/ui_kit.dart           PanelHeader、KeyValueRow、Segmented、SliderField…
  src/ui/format.dart           千分位、字节、相对时间
  src/layouts/main_shell.dart  顶栏 + 工具轨 + 面板 + 视口 + 状态条
  src/widgets/
    viewport_canvas.dart       纹理显示与手势转发
    project_panel.dart         数据集摘要、最近项目
    pipeline_panel.dart        CSV/DAT → 工程包，阶段与取消
    render_panel.dart          配色、点大小、点形状、背景、高程与着色、值域、预算
    camera_panel.dart          六个正交视角与缩放
    measurement_panel.dart     两次拾取的距离与高差
    result_panel.dart          导出 PLY/CSV、保存当前帧
    diagnostics_panel.dart     显卡、LOD、缓存、诊断 JSON
    status_strip.dart          底部唯一状态条
    camera_gizmo.dart          视口内视图轴与缩放按钮
    colormap_legend.dart       色标图例
  src/ffi/
    gs3d_bindings.dart         引擎 C ABI 的类型与查找表
    gs3d_engine.dart           引擎封装（安全指针、返回值转换）
    geoscatter3d_service.dart  场景中枢：数据集、设置、拾取、导出、诊断
    pipeline_bindings.dart     无 GPU 管线 ABI
    pipeline_service.dart      预处理任务（桌面 C ABI，Android 走前台服务）
    app_abi.dart               显卡枚举、最近项目
    app_state.dart             应用状态目录与原子写
    render_settings.dart       显示设置的模型与持久化
    pipeline_history.dart      最近一次预处理的源/包路径
    document_picker.dart       文件与目录选择（Android 方法通道、Windows FFI、Linux GTK）
```

布局规则：左侧 52px 工具轨，面板展开时把视口挤窄（IDE 式），不覆盖点云；再次
点击当前轨项收起面板。宽度小于 520 时面板宽度收在 `200..320`。底部状态条是唯一的
全局状态显示：就绪/载入/错误、数据集、`源 → 绘制`、LOD、FPS、帧时间、渲染时间、GPU。

视口里只有视图轴、缩放按钮、色标图例和测量读数。拖动旋转，双指拖动平移，
滚轮缩放，单击拾取测量点。

## 运行与构建

```bash
# Linux（引擎、shader 与 Flutter 包一起构建）
../scripts/build_flutter_linux.sh
./build/linux/x64/release/bundle/ui_flutter
```

```powershell
# Windows
../scripts/build_flutter_windows.ps1 -Config Release
# Android（需先编译原生库）
../scripts/build_android_arm64.ps1
flutter build apk --release
```

调试运行时引擎按当前目录找 `assets/shaders/point.vert.spv`，必须在 `ui_flutter/` 下
启动二进制，或用 `Gs3dEngine.create(shaderDir: ...)` 显式给路径。

## Android

- `libgs3d_ffi.so` 与 `libgs3d_pipeline_ffi.so` 随仓库跟踪在
  `android/app/src/main/jniLibs/{arm64-v8a,x86_64}/`；改过引擎代码后要重新交叉编译覆盖。
- 选择文件走 SAF：`MainActivity` 把选中的内容复制到应用持久目录，引擎只认识本地路径。
- 预处理由 `PipelineJobService`（`dataSync` 前台服务）持有，退出界面后继续跑，再次
  进入时通过方法通道重连；系统回收进程后按「最近项目」手动续跑。
- 需要 `POST_NOTIFICATIONS`、`FOREGROUND_SERVICE`、`FOREGROUND_SERVICE_DATA_SYNC` 与
  唤醒锁；Android 15 对 `dataSync` 有 24 小时累计 6 小时上限，超时前服务主动停止任务。

## 测试

```bash
flutter analyze --no-pub          # 0 error / 0 warning（既有 material 弃用 info 不计）
flutter test --no-pub             # 60 项：54 通过、6 跳过
```

跳过的 6 项：3 个真实 GPU 用例（`GS3D_ENGINE_E2E=1` 才跑，见 `test/engine_test.dart`）、
3 个管线库用例（`GS3D_PIPELINE_LIBRARY` 指向 `libgs3d_pipeline_ffi.so` 才跑）。

- 界面测试用 `test/support/fake_engine.dart`，断言发出的命令而不是渲染结果。
- 结构体镜像的尺寸与偏移在 `test/engine_state_layout_test.dart` 断言。
- `testWidgets` 里不要 `await` 真实文件 IO（FakeAsync 会挂住），也不要对带定时器的
  服务用 `pumpAndSettle`，用 `pump(Duration(...))`。

## 限制

- Android 只有构建与 JVM 级冒烟，没有设备级验证。
- 导出是同步调用，超过 200 万点直接拒绝；50M 数据集拾取一次要数秒。
- 显示设置存在平台应用状态目录的 `render-settings.json`，含本机路径（数据集路径），
  分享该文件会泄露路径。
