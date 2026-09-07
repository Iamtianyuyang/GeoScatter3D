# GeoScatter3D Flutter Desktop 前端

GeoScatter3D 的现代 Windows 桌面端应用前端，采用 Flutter Desktop 框架构建。彻底取代了原有的 C++ ImGui 立即模式界面，通过标准 C-ABI 动态链接库 (`gs3d_ffi.dll`) 与原生高性能 C++20 / Vulkan 1.3 核心引擎及 JSON-RPC 2.0 控制面无缝通信。

本项目遵循严格的架构解耦与工程护栏：**UI 纯只读投影或产生纯数据命令（UiActions）、图形句柄不穿透、临时中间产物收敛至 `tmp/`**。

---

## 一、核心特性与模块架构

### 1. 欢迎页与工区管理 (Welcome & Workspaces)
- **新建工程向导**：支持本地海量 CSV/DAT 文件浏览、指定坐标/标量列索引与离线流式切片构建。
- **工区快速载入**：支持打开 `.gs3d.bundle` 工区包文件夹及独立 `.gs3d` 点云文件。
- **最近工程列表**：自动记录并持久化最近打开的工区，支持一键清空与快速进入。
- **防重提交与流式反馈**：后台任务执行期间提供进度条、取消机制与防止重复触发护栏。

### 2. 多工作台布局体系 (Workbench Layouts)
- **标准三栏工作台 (`StandardWorkbenchLayout`)**：左侧工具/图层/测量/统计停靠抽屉、中间 3D 主视口、右侧渲染与属性配置面板。
- **浮动停靠布局 (`FloatingDockLayout`)**：工具卡片化悬浮，最大化主视口视野。
- **紧凑分析导轨 (`AnalysisRailLayout`)**：左侧垂直轻量化功能 Rail，专为高频分析优化。
- 支持在顶部菜单栏一键自由切换与状态保存。

### 3. 三维主视口深度交互 (Center Viewport)
- **悬停拾取 HUD**：鼠标在点云上移动时，毫秒级捕获最近物理测点坐标 $(X, Y, Z)$ 及标量属性值，浮动展示半透明属性卡片。
- **快捷键复制 (`C` 键)**：一键将当前悬停测点坐标复制至系统剪贴板并弹出 Toast 反馈。
- **双击旋转对焦 (Orbit Pivot)**：双击点云中任意目标点，视口旋转中心与相机观察焦点平滑定位至该点，并显示对焦准星光环。
- **3D 几何测距标尺 (`M` 键 / 工具栏)**：连续拾取空间点生成三维几何连线，实时显示空间直线距离、$\Delta Z$ 轴向落差与坐标端点标注。
- **选区框选与属性统计**：
  - `Shift + 拖拽`：矩形框选区域，即时计算选区样本数、均值、极差、方差并生成 5-Bin 直方图。
  - `Ctrl + 拖拽`：矩形框选视口缩放聚焦。
- **交互式 3D 罗盘 (Gizmo)**：支持点击 X / Y / Z 正负极球体一键平滑过渡至顶视、底视、前视、侧视。
- **高清截图导出**：基于 `RepaintBoundary` 实现 2.0x 无损 PNG 显存回读与本地保存。

### 4. 停靠面板与分析套件 (Dock Panels & Tools)
- **2D 鸟瞰小地图**：实时投影工区平面边界与当前主相机的世界视锥多边形，支持在小地图上点击快速平移漫游。
- **测量线段管理**：记录多组测量结果，支持图钉固定、单项删除、清空全部与 2D/3D 显示切换。
- **局部统计卡片**：动态展示选区统计指标，并支持一键复制统计数据文本。
- **动态底部状态栏**：实时绑定引擎 FPS、单帧耗时、可见点数、显存占用与瓦片流式加载水位；支持点击快捷唤出诊断面板。

### 5. 全局视察器与命令套件 (Overlays & Inspectors)
- **命令面板 (`CommandPalette` - `Ctrl+P`)**：居中模糊搜索框，快速检索全量动作、视角预设、色标方案并回车即时分发。
- **快捷键速查指南 (`ShortcutOverlay` - `F1`)**：分类展现漫游、交互、测量与全局快捷键图层。
- **性能与显存诊断面板 (`PerformancePanel`)**：大字号仪表盘展示渲染帧率、GPU 显存占用、CPU 瓦片缓存水位及一键清空显存缓存。
- **八叉树瓦片与 LOD 视察器 (`TileInspectorDialog`)**：深入检查空间八叉树分级结构与后台瓦片流式加载管线。
- **点云数据集导出 (`ExportPointCloudDialog`)**：支持将工区点云全量导出为标准 ASCII PLY 或逗号分隔 CSV 格式。

### 6. 双轨动作与原生控制面引擎 (Action Engine & Dual-Path Control)
- 注册了 **45 个统一高层 UI 动作**，覆盖欢迎页、工作台、视口控制、测量、诊断及数据导出。
- **原生引擎联动与离线回退双轨制**：
  - 连接原生引擎时，通过 `ViewerControlClient` 将相机矩阵、渲染设置、高频事件分发给本地 C++ 渲染管线；
  - 无论动态链接库是否存在或处于无头测试环境，均内建纯 Dart 高精度投影算法与离线数据回退，确保全平台环境 100% 健壮运行。

---

## 二、代码物理拓扑

```text
ui_flutter/
├── lib/
│   ├── main.dart                             # 桌面应用入口，全局模态浮层与快捷键监听挂载
│   └── src/
│       ├── ffi/
│       │   ├── geoscatter3d_bindings.dart    # 纯 C-ABI 动态链接库原生函数签名绑定
│       │   └── geoscatter3d_service.dart     # 核心状态中枢、45项动作调度器与指标轮询引擎
│       ├── native/
│       │   └── viewer_control_client.dart    # 本地原生查看器 TCP + JSON-RPC 双向控制面客户端
│       ├── panels/
│       │   ├── center_viewport.dart          # 3D 视口画布、射线拾取、测距连线、框选与罗盘
│       │   ├── left_dock_panel.dart          # 左侧抽屉：2D小地图、空间测量线列表、选区统计
│       │   ├── right_dock_panel.dart         # 右侧抽屉：点云属性渲染、色标映射、光照与视图预设
│       │   ├── top_menu_bar.dart             # 顶部原生风格菜单栏与布局切换入口
│       │   ├── bottom_status_bar.dart        # 底部指标状态栏（FPS、耗时、可见点数、显存）
│       │   ├── performance_panel.dart        # 性能与流式加载显存诊断面板
│       │   ├── tile_inspector_dialog.dart    # 八叉树金字塔与 LOD 瓦片调试器
│       │   └── export_dialog.dart            # PLY / CSV 点云数据导出对话框
│       ├── widgets/
│       │   ├── command_palette.dart          # Ctrl+P 全局交互式命令面板
│       │   ├── shortcut_overlay.dart         # F1 快捷操作速查窗口
│       │   ├── welcome_view.dart             # 欢迎页组件
│       │   ├── standard_workbench_layout.dart# 标准三栏工作台布局
│       │   ├── floating_dock_layout.dart     # 悬浮停靠卡片布局
│       │   └── analysis_rail_layout.dart     # 分析导轨布局
│       └── models/
│           └── measurement_line_model.dart   # 三维测量线几何数据模型
├── test/
│   ├── ffi_test.dart                         # FFI 动态库加载与离线数据降级测试
│   ├── welcome_test.dart                     # 欢迎页组件与交互动作测试
│   ├── workbench_test.dart                   # 工作台三栏布局与视口操作测试
│   ├── widget_test.dart                      # 欢迎页到工作台路由与布局模式切换测试
│   └── overlays_test.dart                    # 命令面板、快捷键帮助、性能面板、导出对话框测试
└── scripts/
    └── build_flutter_windows.ps1             # Windows 桌面全自动化编译与 FFI/Native 二进制部署脚本
```

---

## 三、开发与验证指南

### 1. 运行自动化测试套件
项目包含 50 项前台单元与组件集成测试，包含完整的离线回退与临时文件自清理机制：

```powershell
cd ui_flutter
flutter test
```
*预期输出：`All tests passed! (50/50 通过)`*

### 2. 本地开发调试运行
直接在 Windows 桌面以开发模式启动前端：

```powershell
flutter run -d windows
```

### 3. 一键编译与发布构建 (Release Build)
推荐通过工程根目录的一键自动化部署脚本进行发布构建。该脚本会自动编译 C++ 原生动态库 `gs3d_ffi.dll` 与 `GeoScatter3D.exe`，打包 Shader 与中文字体资源，并完整部署至 Flutter 输出目录：

```powershell
./scripts/build_flutter_windows.ps1 -Config Release
```

构建产物生成于：
`ui_flutter/build/windows/x64/runner/Release/ui_flutter.exe`

---

## 四、工程约束与规范

1. **中间产物与临时文件隔离**：
   - 任何由测试或运行时导出的数据、日志必须放置在 `tmp/` 目录下；
   - 单元测试执行完毕后必须在 `tearDown` 中主动清理临时文件，杜绝污染 Git 工作树。
2. **状态纯净化与单向通信**：
   - UI 控件不持有业务核心状态，状态统一收敛于 `GeoScatter3dService`；
   - 严禁在 UI 层引入图形驱动 API 或底层句柄，统一通过纯数据命令（`UiActions`）驱动。
