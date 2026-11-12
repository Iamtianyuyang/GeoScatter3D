# GeoScatter3D

GeoScatter3D 是一个面向大规模三维散点/点云数据的现代 Flutter 桌面端与 Android 平板可视化、分析系统。
底层由 C++20 / Vulkan 核心引擎提供海量数据流式处理与离屏图形计算，桌面 Profile 为 Vulkan 1.3，Android Profile 最低为 Vulkan 1.1；前端经纯 C-ABI 动态链接库（Windows `gs3d_ffi.dll`、Android `libgs3d_ffi.so`）调用**单进程 `gs3d_engine_*` 渲染内核**；JSON-RPC 仅作为标准控制面协议，不再启动独立 Viewer。
Android 发布包声明最低具备 Vulkan 1.1；引擎不可用时不使用 Dart 软渲染替代。

---

## 一、核心能力与技术亮点

### 1. 大规模海量点云流式处理核心
- **高并发离线切片**：CSV / DAT 文件高并发解析并切片生成自研 GS3D v2 显式小端二进制格式；
- **三层 GPU 数据路径**：八叉树空间分级组织，支持全量点云、LOD 视锥金字塔与局部瓦片（Tile）三种渲染数据路径；
- **动态显存水位控制**：基于屏幕空间的瓦片挑选算法、异步后台线程读取、带字节预算的 LRU 缓存与逐帧限流 GPU 上传；
- **高性能离屏着色**：按平台 Profile 运行的 Vulkan 点渲染管线，支持动态点大小调节与高程 `z` / 属性 `value` 双通道色标平滑映射。

### 2. 现代化 Flutter 桌面工作台
- **欢迎页与工程工作流**：支持新建工程、本地海量点云数据浏览、离线切片构建进度追踪与最近工程管理；
- **三套专业工作台布局**：标准三栏工作台 (`StandardWorkbenchLayout`)、悬浮停靠工作台 (`FloatingDockLayout`)、紧凑分析导轨 (`AnalysisRailLayout`) 一键无缝切换；
- **三维主视口深度交互**：
  - **悬停 HUD 浮窗**：鼠标移动毫秒级捕获最近物理测点坐标 $(X, Y, Z)$ 与标量值；
  - **快捷复制与旋转对焦**：`C` 键一键复制坐标至剪贴板，双击任意测点平滑对焦旋转中心 (Orbit Pivot)；
  - **3D 空间几何测距**：`M` 键开启测距标尺，视口连续点选生成带端点标签的三维连线，实时计算空间直距与 $\Delta Z$ 高差；
  - **选区框选与属性统计**：`Shift + 拖拽` 矩形框选，即时计算选区样本数、均值、极差、方差并生成 5-Bin 直方图；
  - **局部聚焦与无损截图**：`Ctrl + 拖拽` 视口框选缩放聚焦，支持 2.0x 高清 PNG 视口无损显存回读与保存；
  - **交互式 3D 罗盘 (Gizmo)**：支持点击 X / Y / Z 正负极球体平滑过渡至预设视角（顶视、底视、前视、侧视）。
- **二维鸟瞰小地图**：实时投影工区平面边界与当前主相机的世界视锥多边形，支持在小地图上点击快速定位平移；
- **全局命令面板与视察器套件**：
  - `Ctrl+P` 全局交互式命令面板，模糊检索全量动作并即时调度；
  - `F1` 快捷键全屏速查速览；
  - 实时性能与显存诊断仪表盘 (`PerformancePanel`)，支持一键清空瓦片缓存；
  - 八叉树瓦片与 LOD 流式加载视察器 (`TileInspectorDialog`)；
  - 点云全量数据集导出 (`ExportPointCloudDialog`)，支持标准 ASCII PLY 与 CSV 双格式。

### 3. 严谨的架构解耦与工程护栏
- **图形句柄不穿透**：UI 纯只读投影或产出 `UiActions` 纯数据命令，严禁直接持有 Vulkan 句柄；
- **显式引擎能力**：工作台渲染、拾取、相机与截图走 `gs3d_engine_*` C-ABI；引擎不可用时明确报告错误，不伪造 Dart 点云预览；
- **统一控制面**：前台动作统一由 `GeoScatter3dService` 调度，并可通过 JSON-RPC 2.0 `ui_action` 调用；
- **文件隔离纪律**：测试与运行时临时产物严格收敛于 `tmp/` 目录，并配置自动化测试自清理，杜绝 Git 工作树污染。

---

## 二、构建与运行指南

### VS Code F5（整个程序）

用 VS Code 打开仓库根目录，安装推荐的 Dart/Flutter 扩展，选择
`GeoScatter3D (Flutter + Native)` 后按 F5。预启动任务自动构建 Debug 版
FFI、Vulkan 引擎和预处理器；Flutter 构建时部署 DLL、shader、字体及配置。
打开数据集后由同一进程的 `gs3d_engine_*` 内核提供离屏帧回读。停止调试后可再次按 F5。
首次运行需要 Flutter SDK、Visual Studio C++ 桌面工具、CMake 3.28+、Vulkan SDK
以及 GLFW/nlohmann-json 依赖；新构建优先使用 `VCPKG_ROOT`，已有构建沿用 CMake 缓存。

### 1. 快速开始（一键编译与完整发布）

推荐使用根目录自带的一键自动化编译部署脚本。该脚本会自动构建 C++ 原生 FFI 动态库 (`gs3d_ffi.dll`)，打包 Shader 与中文字体资源，并完成 Flutter Windows 桌面端最终打包：

```powershell
./scripts/build_flutter_windows.ps1 -Config Release
```

构建成功后即可直接启动发布产物：
```powershell
./ui_flutter/build/windows/x64/runner/Release/ui_flutter.exe
```

Android 正式发布使用带签名校验的一键脚本：
```powershell
./scripts/build_android_release.ps1
```
运行前请按 `ui_flutter/android/key.properties.example` 配置生产 keystore；
脚本会拒绝缺失、占位值或 debug 证书，并校验 AAB 内包含两种 Android ABI、
SPIR-V 着色器与内置示例数据。

Android 实机 Maestro 回归使用：

```powershell
./scripts/run_maestro_android.ps1 -Device emulator-5554 -Suite all
```

脚本会把 `tmp/maestro/geoscatter3d-import-test.csv` 作为 SAF 流程前置文件推送到
模拟器 Downloads，并将本次运行产物统一写入 `tmp/maestro/`。

### 2. 独立开发与测试

#### 运行 Flutter 前台自动化测试
涵盖 FFI 动态加载、欢迎页向导、三套布局路由、视口深度交互、命令弹窗及导出对话框：
```powershell
cd ui_flutter
flutter test
```

#### 单进程渲染内核 e2e（可选）
```powershell
cd ui_flutter
$env:GS3D_ENGINE_E2E='1'
flutter test test/engine_e2e_test.dart
```

#### 本地 Flutter 开发模式启动
```powershell
cd ui_flutter
flutter run -d windows
```

#### 运行 C++ 底层单元测试与工程质量门禁（当前 43 项通过）
```powershell
# 1. 静态头文件依赖拓扑与单向依赖合规检查
python scripts/check_include_deps.py

# 2. 架构解耦与工程护栏指标核查
python scripts/check_engineering_guardrails.py

# 3. 运行全量底层单元测试（数据格式、相机系统、Vulkan管线、控制面等）
ctest --test-dir tmp/build-win -C Release --output-on-failure
```

---

## 三、代码物理拓扑与架构分层

```text
GeoScatter3D/
├── ui_flutter/                               # [前端表现层 UI] Flutter Desktop 与 Android 移动端应用
│   ├── lib/main.dart                         # 桌面应用根入口与全局模态浮层挂载
│   ├── lib/src/ffi/                          # C-ABI FFI 绑定与核心调度服务 (geoscatter3d_service.dart)
│   ├── lib/src/platform/                     # Android SAF 等平台桥接
│   ├── lib/src/panels/                       # 3D视口画布、小地图、测量统计、菜单栏与诊断对话框
│   ├── lib/src/widgets/                      # 命令面板、快捷键帮助、三种工作台布局组件
│   └── test/                                 # Flutter 单元与组件集成测试套件
│
├── include/ & src/
│   ├── [跨语言桥接层 FFI]
│   │   └── ffi/                              # C-ABI 动态链接库导出接口 (geoscatter3d_ffi.h / .cpp)
│   │
│   ├── [基础层 Foundation]
│   │   ├── core/                             # 通用领域数据契约 (PointData, TileData, DatasetDescriptor)
│   │   ├── platform/                         # 跨平台环境支撑 (GLFW 窗口封装、CpuInfo 硬件探测)
│   │   └── util/                             # 基础通用工具箱 (ThreadPool 线程池、分级日志、计时器)
│   │
│   ├── [引擎层 Engine]
│   │   ├── render/                           # Vulkan Profile 显存资源、Shader 管线、GPU 缓存与离屏绘制
│   │   ├── camera/                           # 3D 视口相机变换矩阵、局部事件映射与同步组 (CameraHub)
│   │   ├── data/                             # GS3D v2、LOD 与瓦片数据集格式、索引、全量导出器 (Gs3dExporter)
│   │   └── preprocess/                       # 离线高并发 CSV 切片、统计直方图与八叉树瓦片生成
│   │
│   └── [应用编排与控制面 App Shell & Control]
│       ├── app/                              # 顶层主循环生命周期与运行时领域子系统 (systems/, session/, config/)
│       └── control/                          # TCP + JSON-RPC 2.0 自动化控制面与组件树反射 (ControlPlane)
│
├── config/                                   # TOML 配置文件模板 (viewer.toml, sample-viewer.toml)
├── assets/                                   # 字体与 GLSL 离屏着色器资源
├── scripts/                                  # 自动化构建、打包、依赖检查与工程护栏脚本
└── docs/                                     # 架构说明、操作手册与配置参考文档
```

---

## 四、文档与参考索引

- **Flutter 前端详细文档**：详见 [ui_flutter/README.md](ui_flutter/README.md)（含详细组件结构、按键映射与交互设计规范）；
- **系统架构与历史演进**：详见 [docs/architecture.md](docs/architecture.md)；
- **配置项详细参考**：详见 [docs/config-reference.md](docs/config-reference.md)；
- **强制规则与配方**：详见 [docs/RULES.md](docs/RULES.md) 与 [docs/recipes/](docs/recipes/)。
