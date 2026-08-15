# Phase 5 目录整理:include/ 公私分离 + 私有头下沉

## 背景与诊断

当前 `include/` 与 `src/` 顶层模块虽对称,但分类层级有四个互相纠缠的问题:

1. **`include/` 是"测试可见面",不是公开 API 面**。46 个 `app/` 头里 10 个纯私有(只被 `src/app` 消费),24 个 `ui/` 头里 14 个纯私有——公开与私有混在一个平铺目录。
2. **`include/ui/` 与 `src/ui/` 目录不对齐**。24 个头里 17 个的实现躺在 `src/ui/{panels,viewport,themes,...}` 子目录,头文件却平铺在 `include/ui/` 顶层。
3. **`data`/`render`/`preprocess` 模块也有少量纯私有头**散在 `include/` 里(CsvChunkUtils、Delimiter、Gs3dVerifier、GpuFrameTimer、VulkanDepthBuffer、Gs3dWriter)。
4. **`AppState.hpp`(635 行/21 struct)携带 Vulkan 类型**是 `ui↔app↔gui` 环路枢纽——这是 Phase 6 的硬前置,本次不动。

## 决策(已与用户确认)

- **范围**:仅目录搬迁。不改模块名、不改 CMake 库目标、不移动 `ViewerApp.cpp`/`UiRoot.cpp`/`AppConfig.cpp`(护栏脚本硬编码的 4 个路径不动,护栏零改动)。
- **`include/ui/` 公开头保持扁平**:10 个公开头留在 `include/ui/*.hpp` 顶层,零 include 站点改动。
- **ui 14 个内部共享头**:统一下沉到 `src/ui/` 顶层(扁平),把 `${PROJECT_SOURCE_DIR}/src` 加入 include 路径,所有 `#include "ui/X.hpp"` 站点不变。
- **app 私有头**:下沉 7 个纯私有(无公开消费者),保留 3 个被公开头引用的(`ViewerAppRunState`、`ViewerAppGpuPick`、`MeasurementManager`)在 `include/app/`。

**关键机制**:把 `${PROJECT_SOURCE_DIR}/src` 作为 PUBLIC include 目录加到 5 个库 target,且在列表里排在 `${PROJECT_SOURCE_DIR}/include` 之后。于是 `#include "module/X.hpp"` 先查 `include/module/X.hpp`(留住公开头),找不到再查 `src/module/X.hpp`(下沉的私有头)。**全仓库零 include 站点改动**。用 `git mv`(非复制)保证同名头不会同时存在于两处,消除影子覆盖风险。

## 文件移动表(27 个头,全部 `git mv`)

### Batch 1 — data / render / preprocess(6 个)+ CMake

| 源 | 目标 |
|---|---|
| `include/data/CsvChunkUtils.hpp` | `src/data/CsvChunkUtils.hpp` |
| `include/data/Delimiter.hpp` | `src/data/Delimiter.hpp` |
| `include/data/Gs3dVerifier.hpp` | `src/data/Gs3dVerifier.hpp` |
| `include/render/GpuFrameTimer.hpp` | `src/render/GpuFrameTimer.hpp` |
| `include/render/VulkanDepthBuffer.hpp` | `src/render/VulkanDepthBuffer.hpp` |
| `include/preprocess/Gs3dWriter.hpp` | `src/preprocess/Gs3dWriter.hpp` |

### Batch 2 — ui(14 个)+ architecture.md

| 源 | 目标 |
|---|---|
| `include/ui/AnalysisRailUi.hpp` | `src/ui/AnalysisRailUi.hpp` |
| `include/ui/ApplicationMenu.hpp` | `src/ui/ApplicationMenu.hpp` |
| `include/ui/AuxiliaryPanels.hpp` | `src/ui/AuxiliaryPanels.hpp` |
| `include/ui/ColormapPreview.hpp` | `src/ui/ColormapPreview.hpp` |
| `include/ui/DatasetPanel.hpp` | `src/ui/DatasetPanel.hpp` |
| `include/ui/DockLayoutBuilder.hpp` | `src/ui/DockLayoutBuilder.hpp` |
| `include/ui/FloatingDockUi.hpp` | `src/ui/FloatingDockUi.hpp` |
| `include/ui/MeasurementPanel.hpp` | `src/ui/MeasurementPanel.hpp` |
| `include/ui/NavigationMapPanel.hpp` | `src/ui/NavigationMapPanel.hpp` |
| `include/ui/PreloadGate.hpp` | `src/ui/PreloadGate.hpp` |
| `include/ui/RegionStatsPanel.hpp` | `src/ui/RegionStatsPanel.hpp` |
| `include/ui/RenderSettingsPanel.hpp` | `src/ui/RenderSettingsPanel.hpp` |
| `include/ui/Widgets.hpp` | `src/ui/Widgets.hpp` |
| `include/ui/WorkbenchUi.hpp` | `src/ui/WorkbenchUi.hpp` |

> 保留在 `include/ui/` 的 10 个公开头:`UiRoot`、`ViewportCanvas`、`Theme`、`UiPalette`、`SvgLogoTexture`、`WorkspaceManager`、`WelcomePage`、`ViewportAxisTicks`、`AnalysisRailLayout`、`FloatingDockLayout`。

### Batch 3 — app(7 个)+ README/spec

| 源 | 目标 |
|---|---|
| `include/app/AppConfigTomlValue.hpp` | `src/app/AppConfigTomlValue.hpp` |
| `include/app/AppConfigValidation.hpp` | `src/app/AppConfigValidation.hpp` |
| `include/app/AppConfigViewerToml.hpp` | `src/app/AppConfigViewerToml.hpp` |
| `include/app/ViewerFrameRenderer.hpp` | `src/app/ViewerFrameRenderer.hpp` |
| `include/app/ViewerKeyboardShortcutSystem.hpp` | `src/app/ViewerKeyboardShortcutSystem.hpp` |
| `include/app/ViewerRenderRuntime.hpp` | `src/app/ViewerRenderRuntime.hpp` |
| `include/app/ViewerViewportRenderSystem.hpp` | `src/app/ViewerViewportRenderSystem.hpp` |

> 保留在 `include/app/` 的 3 个(被公开头引用):`ViewerAppRunState`(被 `ViewerApp.hpp` 引)、`ViewerAppGpuPick`(被 `ViewerPickSystem.hpp` 引)、`MeasurementManager`(被 `AppState.hpp` + `PreprocessedBundle.hpp` 引)。
>
> app 私有头放在 `src/app/` **顶层**(扁平),不进 `config/`/`systems/` 子目录——因为 `#include "app/X.hpp"` 经 `${PROJECT_SOURCE_DIR}/src` 解析到 `src/app/X.hpp`(扁平)。进子目录会要求站点改成 `app/<subdir>/X.hpp`,违背"零站点改动"。头文件在模块根、实现 .cpp 在子目录是可接受的组织。

## CMake 改动(`src/CMakeLists.txt`,Batch 1 一次到位)

给 5 个库 target 的 `target_include_directories` 列表**追加** `${PROJECT_SOURCE_DIR}/src`(排在现有 `${PROJECT_SOURCE_DIR}/include` 之后):

- `gs3d_data`、`gs3d_preprocess`、`gs3d_render`、`gs3d_app_support`、`gs3d_app`

`gs3d_viewport_lod`、`gs3d_viewport_camera`、`GeoScatter3D` exe、`GeoScatter3DPreprocess`、tests、tools 经链接传播(PUBLIC)自动获得,无需逐个改。`tests/CMakeLists.txt:156` 的 `${PROJECT_SOURCE_DIR}/src/ui`(为 `themes/Registry.hpp`)保持不动。

## 文档同步

- **`docs/architecture.md` 行 125-126**:`include/ui/DockLayoutBuilder.hpp` → `src/ui/DockLayoutBuilder.hpp`;`include/ui/ApplicationMenu.hpp` → `src/ui/ApplicationMenu.hpp`(这两个头在描述工作台 DockBuilder/菜单栏的句子里被点名)。
- **`README.md` 行 166-178**("目录"段):在树后补一句——`include/` 只放跨模块公开 API 头,模块私有头与实现同处 `src/<module>/`。
- **`docs/spec/geoscatter3d-competition.md` 行 48-51**:同样补注公开/私有分离。
- **护栏与依赖检查器零改动**:`check_engineering_guardrails.py` 硬编码的 4 路径未移动;`check_include_deps.py` 的 `MODULE_ORDER`/`SCAN_ROOTS` 不变(模块名不变,下沉头的 module 仍是首段)。

## 验证(每批跑一次)

```bash
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cmake -B build-verify -S . -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DGS3D_GLSLANG_VALIDATOR=C:/vcpkg/installed/x64-windows/tools/glslang/glslangValidator.exe -DPython3_EXECUTABLE=C:/Users/tianyy/miniconda3/envs/geophysics-forward-plotting/python.exe -DCMAKE_BUILD_TYPE=Release'
cmake --build build-verify --config Release --parallel
ctest --test-dir build-verify -C Release
```

预期 38/38 全绿(含 `GeoScatter3D.IncludeDeps`、`GeoScatter3D.EngineeringGuardrails` 两个 lint)。重新 configure 是必须的(CMake target_include_directories 变了)。

## 提交策略(3 个提交)

1. `refactor(structure): Phase 5.1 - sink data/render/preprocess private headers, add src to include path`
2. `refactor(structure): Phase 5.2 - sink ui private headers to src/ui`
3. `refactor(structure): Phase 5.3 - sink app private headers, document public/private split`

每批:git mv → configure → build → ctest → 提交。

## 风险与缓解

1. **工作树有未提交的 Phase 3 内容改动**,其中 3 个文件(`AnalysisRailUi.hpp`、`ApplicationMenu.hpp`、`FloatingDockUi.hpp`)恰在被移动的 14 个 ui 头里。`git mv` 会把内容改动带到新路径并保持为**未暂存**状态,因此纯重命名提交(只 stage rename)不含 Phase 3 内容改动——Phase 3 改动留给用户另行提交。验证:`git diff --cached` 只见 rename,`git diff` 见 Phase 3 内容编辑。
2. **`src` 上 include 路径是较宽的编译面**:理论上 `#include "X.hpp"`(无前缀)可能误命中 `src/` 文件。缓解:仓库约定无前缀 include 仅限同目录 `*Internal.hpp`(名字独特),且 quote-include 先查同目录、后查 include 路径,误命中概率极低。
3. **影子覆盖**:同名头同时存在 `include/` 与 `src/`。缓解:`git mv`(非复制)+ include 路径顺序保证 `include/` 优先;本次移动的 27 个头都从 `include/` 移走,不留副本。
4. **测试传播**:已核实 38 个测试中无任何测试直接 include 这 27 个被移动的私有头(它们只通过公开头传递引用);PUBLIC 传播覆盖所有间接消费。
