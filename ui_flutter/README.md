# GeoScatter3D Flutter Desktop 与 Android 移动端前端

GeoScatter3D 的 Windows、Linux 桌面端与 Android 平板/移动端应用前端采用 Flutter 构建，通过标准 C ABI 动态链接库调用 C++20 / Vulkan 引擎。引擎在应用进程内运行；桌面使用 RGBA 帧回读，Android 支持原生纹理呈现。引擎不可用时显式报错。

本项目遵循严格的架构解耦与工程护栏：**UI 纯只读投影或产生纯数据命令（UiActions）、图形句柄不穿透、临时中间产物收敛至 `tmp/`**。

---

欢迎页已通过真实预处理、FFI 和 Vulkan 首帧测试。以下工作台模块清单并不等于全部端到端验收通过，最新状态见 [接入记录](../docs/flutter-integration-audit.md)。

## 一、核心特性与模块架构


本项目遵循严格的架构解耦与工程护栏：**UI 纯只读投影或产生纯数据命令（UiActions）、图形句柄不穿透、临时中间产物收敛至 `tmp/`**。

---

欢迎页已通过真实预处理、FFI 和 Vulkan 首帧测试。以下工作台模块清单并不等于全部端到端验收通过，最新状态见 [接入记录](../docs/flutter-integration-audit.md)。

## 一、核心特性与模块架构

### 1. 欢迎页与工区管理 (Welcome & Workspaces)
- **新建工程向导**：支持 CSV/DAT 文件浏览、线程数、XY/XYZ 体素模式和最细 LOD 层目标点数，调用真实预处理器构建。当前使用 X/Y/elevation/field_statics 标准列名。
- **工区快速载入**：支持打开 `.gs3d.bundle` 工区包文件夹及独立 `.gs3d` 点云文件。
- **最近工程列表**：自动记录并持久化最近打开的工区，支持一键清空与快速进入。
- **防重提交与流式反馈**：后台任务执行期间提供进度条、取消机制与防止重复触发护栏。

#### 2. 极简全屏视口工作台 (Minimalist Fullscreen Viewport Workbench)
- **极简全屏视口布局 (`StandardWorkbenchLayout`)**：彻底移除左右停靠抽屉与侧栏，中央 Vulkan 3D 视口 (`CenterViewport`) 最大化充满窗口，跨桌面端、平板与手机保持纯粹视野一致性。
- **悬浮渲染外观卡片 (`RenderSettingsPopover`)**：点大小、散点形状、高度缩放、色标映射、背景底色及标量数值区间重置等核心渲染控制统一封装在轻量半透明悬浮卡片中，支持顶栏与视口工具条快速唤起。

### 3. 三维主视口深度交互 (Center Viewport)
- **悬停拾取 HUD**：鼠标在点云上移动时，毫秒级捕获最近物理测点坐标 $(X, Y, Z)$ 及标量属性值，浮动展示半透明属性卡片。
- **快捷键复制 (`C` 键)**：一键将当前悬停测点坐标复制至系统剪贴板并弹出 Toast 反馈。
- **双击旋转对焦 (Orbit Pivot)**：双击点云中任意目标点，视口旋转中心与相机观察焦点平滑定位至该点，并显示对焦准星光环。
- **交互式 3D 罗盘 (Gizmo)**：支持点击 X / Y / Z 正负极球体一键平滑过渡至顶视、底视、前视、侧视。
- **高清截图导出**：基于 `RepaintBoundary` 实现 2.0x 无损 PNG 显存回读与本地保存。

### 4. 辅助视察与状态遥测 (Telemetry & Diagnostics)
- **动态底部状态栏**：实时绑定原生 Vulkan 引擎 FPS、单帧耗时、可见点数、GPU 显卡信息与相机位姿遥测。
- **移动端安全区与自适应工具栏**：Android Edge-to-Edge 下由 `SafeArea` 避让状态栏和底部手势区；手机档顶部工具条紧凑图标排列，平板档提供拇指操作条。

### 5. 全局视察器与命令套件 (Overlays & Inspectors)
- **命令面板 (`CommandPalette` - `Ctrl+P`)**：居中模糊搜索框，快速检索全量动作、视角预设、色标方案并回车即时分发。
- **快捷键速查指南 (`ShortcutOverlay` - `F1`)**：分类展现漫游、交互与全局快捷键图层。
- **性能与显存诊断面板 (`PerformancePanel`)**：大字号仪表盘展示渲染帧率、GPU 显存占用、CPU 瓦片缓存水位及一键清空显存缓存。
- **八叉树瓦片与 LOD 视察器 (`TileInspectorDialog`)**：深入检查空间八叉树分级结构与后台瓦片流式加载管线。
- **点云数据集导出 (`ExportPointCloudDialog`)**：支持将工区点云全量导出为标准 ASCII PLY 或逗号分隔 CSV 格式。

### 6. 统一动作引擎与单进程控制 (Action Engine)

- 注册了 **62 个统一高层 UI 动作**（15 个欢迎页动作 + 47 个工作台动作），覆盖欢迎页、工作台、渲染外观配置、视口控制与数据导出。
- **单进程引擎路径**：
  - `hasEngineApi` 时经 `gs3d_engine_*` 直连 `HeadlessRenderKernel`（渲染 + 拾取 + Gizmo + 导出 + PNG），RGBA 帧呈现，不 spawn 子进程；
  - 引擎不可用时显式报错；拾取/统计可走 A 组 C-ABI 或返回空；无纯 Dart 软件点云预览。

---

## 二、代码物理拓扑

```text
ui_flutter/
├── lib/
│   ├── main.dart                             # 桌面/Android 应用入口，全局模态浮层与快捷键监听挂载
│   └── src/
│       ├── ffi/
│       │   ├── geoscatter3d_bindings.dart    # 纯 C-ABI 动态链接库原生函数签名绑定
│       │   └── geoscatter3d_service.dart     # 核心状态中枢、62项动作调度器与指标轮询引擎
│       ├── layouts/
│       │   └── standard_workbench_layout.dart# 极简全屏视口工作台布局
│       ├── panels/
│       │   ├── center_viewport.dart          # 3D 视口画布、射线拾取、罗盘与悬停 HUD
│       │   ├── render_settings_popover.dart  # 悬浮渲染外观卡片（点大小/色标/高度缩放/背景）
│       │   ├── top_menu_bar.dart             # 顶部原生风格菜单栏与核心操作入口
│       │   ├── bottom_status_bar.dart        # 底部指标状态栏（FPS、耗时、可见点数、显存）
│       │   ├── performance_panel.dart        # 性能与流式加载显存诊断面板
│       │   ├── tile_inspector_dialog.dart    # 八叉树金字塔与 LOD 瓦片调试器
│       │   └── export_dialog.dart            # PLY / CSV 点云数据导出对话框
│       └── widgets/
│           ├── command_palette.dart          # Ctrl+P 全局交互式命令面板
│           ├── shortcut_overlay.dart         # F1 快捷操作速查窗口
│           └── welcome_view.dart             # 欢迎页组件
│       └── models/
│           └── measurement_line_model.dart   # 三维空间点与标量模型
├── test/
│   ├── ffi_test.dart                         # FFI 动态库加载与离线数据降级测试
│   ├── welcome_test.dart                     # 欢迎页组件与交互动作测试
│   ├── workbench_test.dart                   # 工作台视口与悬浮渲染卡片操作测试
│   ├── widget_test.dart                      # 欢迎页到工作台路由与极简全屏测试
│   └── overlays_test.dart                    # 命令面板、快捷键帮助、性能面板、导出对话框测试
└── scripts/
    └── build_flutter_windows.ps1             # Windows 桌面全自动化编译与 FFI/Native 二进制部署脚本
```

---

## 三、开发与验证指南

### 1. 运行自动化测试套件
项目包含 Flutter 前台单元与组件集成测试，包含引擎不可用时的显式错误状态与临时文件自清理机制：

```powershell
cd ui_flutter
flutter test
```
*当前基线：普通测试 90 项通过、3 项跳过；设置 `GS3D_ENGINE_E2E=1` 后为 92 项通过、1 项跳过。*

### 2. 本地开发调试运行
推荐用 VS Code 打开仓库根目录，选择 `GeoScatter3D (Flutter + Native)` 后按 F5。
在本目录使用命令行时，先准备完整 Debug 原生运行时：

```powershell
../scripts/build_flutter_windows.ps1 -Config Debug -NativeOnly
flutter run -d windows
```

实际功能接线及 ImGui 残留结论见 [集成检查报告](../docs/flutter-integration-audit.md)。

### 3. 一键编译与发布构建 (Release Build)
推荐通过工程根目录的一键自动化部署脚本进行桌面发布构建。该脚本会自动编译 C++ 原生动态库 `gs3d_ffi.dll`，打包 Shader 与中文字体资源，并完整部署至 Flutter Windows Runner：

```powershell
./scripts/build_flutter_windows.ps1 -Config Release
```

构建产物生成于：
`ui_flutter/build/windows/x64/runner/Release/ui_flutter.exe`

Linux 桌面发布构建（从项目根目录执行）：

```bash
scripts/build_flutter_linux.sh
```

产物为 `ui_flutter/build/linux/x64/release/bundle/`，包含原生引擎与编译后的着色器；需要 Vulkan、GTK 3 和 Ninja。

Android Debug APK（Maestro/本地联调）：

```powershell
cd ui_flutter
flutter build apk --debug
```

应用商店发布使用 AAB：

```powershell
../scripts/build_android_release.ps1
```

该脚本要求先配置 `android/key.properties` 和生产 keystore，并会校验 AAB
不能使用 Android Debug 证书，同时确认 arm64/x86_64 原生库、SPIR-V 着色器和
示例数据均已打包。Release 构建不会在缺少 keystore 时回退到 Debug
签名；未配置密钥时请使用上面的 Debug APK 做本地 smoke test。

移动端工程写入应用持久沙盒，Android 不申请外部存储权限；由于渲染内核无软件兜底，
Android Manifest 声明设备必须支持 Vulkan 1.1。原生
`libgs3d_ffi.so` 与 SPIR-V 着色器随 APK 部署。正式发布 Android 包时，复制
`android/key.properties.example` 为 `android/key.properties`，将生产 keystore 放在
`android/` 目录（或填写绝对路径），再构建 AAB；`key.properties`、`.jks` 和
`.keystore` 已加入忽略规则。只有经过该脚本校验的生产签名 AAB 才能上传应用商店。

---

## 四、工程约束与规范

1. **中间产物与临时文件隔离**：
   - 任何由测试或运行时导出的数据、日志必须放置在 `tmp/` 目录下；
   - 单元测试执行完毕后必须在 `tearDown` 中主动清理临时文件，杜绝污染 Git 工作树。
2. **状态纯净化与单向通信**：
   - UI 控件不持有业务核心状态，状态统一收敛于 `GeoScatter3dService`；
   - 严禁在 UI 层引入图形驱动 API 或底层句柄，统一通过纯数据命令（`UiActions`）驱动。

## 欢迎页后端验收

从仓库根目录执行 `./scripts/test_welcome_backend.ps1 -Config Debug`。测试实际启动隐藏原生进程，覆盖构建、载入、错误、取消和首帧；产物及测试偏好在 `tmp` 下自动清理。
