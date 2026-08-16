# GeoScatter3D

GeoScatter3D 是一个面向大规模三维散点/点云数据的 C++20 桌面查看器。项目使用
Vulkan 渲染、GLFW 管理窗口、Dear ImGui 提供 docking UI，并使用自定义 GS3D、
LOD 和 tile 文件支持分级与局部加载。

## 当前能力

- CSV 并行解析并转换为 GS3D 二进制数据。
- GS3D v2 使用固定小端字段编码；读取端兼容既有 v1 文件，并校验头部、长度和点数据。
- 全量点云、LOD 点云和局部 tile 三种 GPU 数据路径。
- 基于屏幕空间的 tile 选择、后台读取、CPU 缓存和主线程 GPU 上传。
- Vulkan 点渲染、点大小调整、`value`/`z` 属性着色切换。
- 轨道旋转、平移、缩放、相机重置和可选的相机联动。
- 简体中文、Adobe 风格的紧凑 ImGui 工作台和最多 4 个三维视图。
- 默认只打开一个视图；可按需添加，并以标签页停靠或拖成独立系统窗口。
- 每个视图拥有独立相机和输入状态，拖出主工作台后仍可旋转、平移、缩放和操作控件。
- LOD sidecar 存在时只加载 GS3D 元数据，避免完整点数组常驻内存。
- 有字节预算的 LRU tile CPU 缓存、逐帧限量 GPU 上传和拖动结束后的批量视口 resize。
- 交互时使用低 LOD，隐藏标签页停止离屏渲染，减少旋转、移动和缩放卡顿。
- TOML 配置和 Vulkan validation layer 开关。

- 欢迎页 + 可折叠侧边栏工作台（docking）布局, 主题可在 视图→主题 切换（5 套：
  碳蓝·浅色 / 碳蓝 2.0·深色 / 石墨·深色 / 仪器琥珀·深色 / 高对比·浅色）。
  代码里仍保留 workbench / floating-dock / analysis-rail 三套布局的设计与休眠
  实现，但 TIA-111 后实际只渲染 workbench 一套（详见
  [docs/operations.md](docs/operations.md)）。

当前功能状态与未实现项清单见
[架构与现状](docs/architecture.md#当前功能状态), 全部配置键见
[viewer.toml 配置参考](docs/config-reference.md), 构建/运行/控制面驱动/
进程善后手册见 [操作手册](docs/operations.md)。

## 构建

依赖：

- CMake 3.28+
- 支持 C++20 的编译器
- Vulkan SDK/开发包
- GLFW 3
- 线程库（Linux 上为 pthreads，Windows 使用系统原生线程）
- Git submodule 中的 Dear ImGui 与 Catch2
- Python 3（配置期硬性依赖：`find_package(Python3 REQUIRED)`，用于 include
  依赖检查与工程护栏测试脚本）
- `glslangValidator`（Vulkan SDK 或 glslang tools；CMake 会自动从 GLSL 生成 SPIR-V）

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`ctest` 包含一个从仓库内样例 CSV 生成 bundle 的无窗口 smoke test，因此上述
命令会在没有 GPU 或图形会话的 CI 环境中验证最小数据流程。CTest 中的 C++ 单元测试使用
Catch2，可按标签或原有测试函数名单独运行，例如
`./build/GeoScatter3DGs3dV2Tests "[gs3d]"` 或
`./build/GeoScatter3DRuntimeLogicTests test_resize_debounce`。

Windows 可用 vcpkg 安装 `glfw3`、`vulkan-headers`、`vulkan-loader` 和
`glslang[tools]`，配置时传入 vcpkg toolchain 即可。`glslangValidator` 会通过
toolchain 的工具目录被自动发现，无需手动指定：

```powershell
vcpkg install glfw3:x64-windows vulkan-headers:x64-windows vulkan-loader:x64-windows glslang[tools]:x64-windows
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

该流程已在 Windows 11 + Visual Studio 2022 上本地验证，全部 CTest 测试通过。
可执行文件生成于 `build/Release/`，vcpkg 会自动将 `vulkan-1.dll` 与
`glfw3.dll` 部署到输出目录。若自动发现失败（例如未使用 vcpkg toolchain），
可用 `-DGS3D_GLSLANG_VALIDATOR=<glslangValidator 路径>` 显式指定。

注意：复用旧的构建目录时，缓存中的 `BUILD_TESTING=OFF` 会跳过所有测试目标的
生成；如发现 ctest 只列出少量测试，删除构建目录重新配置，或显式传
`-DBUILD_TESTING=ON`。

详细的 Windows 状态和仍需手工验证的桌面路径见
[Windows portability report](docs/windows-portability-report.md)。

## 分发

一键脚本会依次完成依赖检查、配置、编译、测试、安装 `dist/` 和打包：

```powershell
./scripts/package-windows.ps1
```

```bash
./scripts/package-linux.sh
```

两者均支持跳过测试（`-SkipTests` / `--skip-tests`）。以下为各步骤的
手动等价命令。构建后可安装出一个自包含的分发目录（可执行文件在根目录，
`config/`、`assets/` 在旁边，`data/` 由运行时生成）：

```powershell
cmake --install build --config Release --prefix dist
```

`dist/` 中包含 `GeoScatter3D.exe`、`GeoScatter3DPreprocess.exe`、
`gs3d_groundtruth.exe`、运行所需 DLL（vcpkg 部署的 `vulkan-1.dll`/`glfw3.dll`
以及 MSVC 动态 CRT 的 `msvcp140*.dll`/`vcruntime140*.dll`，目标机器无需另装
VC++ Redistributable）、字体与 SPIR-V shader、配置模板和样例 CSV，整个目录可
直接拷贝到目标机器运行：

```powershell
cd dist
./GeoScatter3D.exe --config config/sample-viewer.toml
```

CPack 可将同样内容打成带版本号的压缩包（Windows 为 ZIP，其余平台为 TGZ），
生成于构建目录下：

```powershell
cd build
cpack -C Release
```

## 从干净克隆打开样例

仓库提供了 26 行的[样例 CSV](examples/sample-points.csv)（1 行表头 +
25 行数据，对应 25 个 golden 点）和可直接运行的
[样例配置](config/sample-viewer.toml)。以下命令从零开始生成数据并打开窗口：

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/GeoScatter3D --config config/sample-viewer.toml
```

样例生成的 bundle 位于被 Git 忽略的 `data/sample-points.gs3d.bundle/`。若只需
验证转换而不启动窗口，可运行：

```bash
./build/GeoScatter3DPreprocess --config config/sample-viewer.toml
```

默认配置入口是 [config/viewer.toml](config/viewer.toml)。四种输入模式：

- `input.mode = "csv"`：启动时重新生成 GS3D 和启用的 LOD/tile；
- `input.mode = "dat"`：同 csv，解析分隔符自动检测（逗号/制表/空白）的 `.dat` 文件；
- `input.mode = "gs3d"`：直接打开现有 `.gs3d` 文件；
- `input.mode = "bundle"`：打开目录式 `.gs3d.bundle` 项目
  （`bundle_dir` 指向包含 `manifest.toml` 的目录，默认配置即此模式）。

注意：默认 `config/viewer.toml` 的 `bundle_dir` 指向 `data/` 下的文件，
而 `data/` 整体被 .gitignore 忽略，干净克隆下默认配置不可直接使用；
请用 `config/sample-viewer.toml` 或先运行预处理生成自己的 bundle。
全部配置键见 [docs/config-reference.md](docs/config-reference.md)。

## 用户偏好

项目配置是可提交的模板。欢迎页选择的 GPU UUID 不再修改
`config/viewer.toml`，而是保存至 Linux 的 `$XDG_CONFIG_HOME/geoscatter3d/`
（默认 `~/.config/geoscatter3d/`）或 Windows 的
`%APPDATA%\\geoscatter3d\\preferences.toml`。这样在新机器上运行不会弄脏仓库。

## 日志

应用、预处理和 Vulkan 诊断统一按级别写日志。`GS3D_LOG_LEVEL` 可设为
`trace`、`debug`、`info`（默认）、`warning`、`error` 或 `off`；性能报告走独立
benchmark 通道，可用 `GS3D_LOG_BENCHMARK=0` 静默。例如：

```bash
GS3D_LOG_LEVEL=warning GS3D_LOG_BENCHMARK=0 \
  ./build/GeoScatter3D --config config/sample-viewer.toml
```

## 多窗口使用

- `viewport.count` 设置启动时显示的视图数量，范围为 1-4，默认是 1。
- 点击顶部的 `+ 视图`、使用“视图”菜单或按 `Ctrl+N` 可以添加视图。
- 新视图默认成为中央工作区的标签页；拖动“视图 N”标签可将其变成独立系统窗口。
- 独立窗口可移动到其他显示器，其画布和“适配”等控件不依赖主窗口输入。
- 相机默认互不影响；在“视图”卡片中启用“联动相机”可加入同步组。
- 未激活的停靠标签页不执行点云离屏渲染，独立可见窗口仍会持续更新。
- 截图按钮会打开系统“另存为”面板，可选择目录和文件名；确认后在后台编码 PNG，并在界面提示保存结果。
- `tile.cpu_cache_max_bytes` 控制 CPU tile 缓存上限，超出后按 LRU 淘汰。
- `tile.gpu_upload_budget_bytes` 控制每帧 tile GPU 上传预算，避免一次上传造成长时间停顿。

## 目录

```text
include/ + src/
  app/         配置、应用状态和主循环编排
  camera/      相机、输入控制和多视口同步
  core/        领域数据边界：PointData、TileData、DatasetDescriptor
  data/        CSV/GS3D/LOD/tile 格式与读取
  preprocess/  CSV 转换、统计、LOD 与 tile 写入
  render/      Vulkan 资源、点管线、LOD/tile GPU 数据和离屏视口
  scene/       SceneState（渲染/场景状态）
  gui/ + ui/   ImGui 生命周期与界面绘制
  platform/    GLFW 窗口
  util/        线程池、计时与统一日志
```

更完整的启动流程、逐帧流程、资源所有权和主要缺陷见
[docs/architecture.md](docs/architecture.md)。
