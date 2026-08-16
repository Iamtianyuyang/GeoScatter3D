# GeoScatter3D 操作手册（能力清单 / 构建运行 / 程序化驱动 / 进程善后 / 红线）

> 本手册面向新加入的智能体与人类开发者：不读历史对话，只看本文档 +
> 下方索引即可建立全局认知并正确构建、运行、驱动、截图、退出。
>
> 核对基准: 2026-08-16, main@8162b1a（TIA-111 之后）。**源码是最终事实**：
> 文档与代码冲突时以代码为准，本手册已按代码逐条核验（含真实运行验证）。
>
> 文档地图：
> - 架构与数据流 / 逐帧流程 / 资源所有权 / 重大风险: [docs/architecture.md](architecture.md)
> - 全部配置键 / 用户偏好: [docs/config-reference.md](config-reference.md)
> - 控制面协议细节（JSON-RPC 2.0、线程模型、错误码）: [docs/control-plane.md](control-plane.md)
> - 控制面组件注册契约: [docs/component-registry.md](component-registry.md)
> - 二进制格式规范: [docs/spec/gs3d-format.md](spec/gs3d-format.md)
> - 构建 / 测试 / 分发: [README.md](../README.md)

## 1. 能力清单（项目「本来有什么」）

### 1.1 布局模式：代码里 3 套，实际渲染 1 套（以代码为准）

顶层布局枚举 `UiLayoutMode`（`include/app/AppState.hpp:25-30`），配置键
`[window] layout`（`config/viewer.toml:39`），解析函数
`ui_layout_from_string()`（`AppState.hpp`）：

| 枚举值 | 配置字符串 | 设计意图（源码注释） | 入口文件 | 当前完成度 |
|---|---|---|---|---|
| `kWorkbench` (0) | `workbench` | 方案 A：菜单栏 + 左右停靠面板 + 状态栏 | `src/ui/UiRoot.cpp`（docking 编排）、`src/ui/AppChrome.cpp`（顶栏/状态栏/菜单） | **唯一实际渲染的布局** |
| `kFloatingDock` (1) | `floating-dock` | 方案 B：视口全沉浸 + 底部悬浮胶囊 Dock + 弹出卡片（Telegram 风），所有面板功能收进 Dock 弹出卡片 | `include/ui/FloatingDockUi.hpp` + `src/ui/FloatingDockUi.cpp`（`draw_floating_dock_layout`） | **休眠**：无调用点，运行期不可达 |
| `kAnalysisRail` (2) | `analysis-rail` / `rail` | 方案 C：暗色图标轨 + 互斥抽屉 + 右侧分析卡片 | `include/ui/AnalysisRailUi.hpp` + `src/ui/AnalysisRailUi.cpp`（`draw_analysis_rail_layout`） | **休眠**：无调用点，运行期不可达 |

**完成度结论（TIA-151 实测）**：TIA-111（commit 4a6fa80）已移除
`UiRoot::draw()` 的三布局分发，当前代码里 `ui_layout_mode` 只有两处读取：
`ViewerAppStateInitialization.cpp:32`（从配置写入状态）与
`AppChrome.cpp:357`（持久化到用户偏好）。`floating-dock` / `analysis-rail`
的绘制函数与切换按钮仍保留在源码中，但没有任何调用点。**实测（TIA-151
第二轮，隐藏窗口 + 控制面 swapchain 回读，2054x1242）**：分别以
`layout = "workbench" / "floating-dock" / "analysis-rail"` 启动，排除实时
文本带（工具栏 LOD/点数、状态栏 FPS/相机坐标——同一次运行相隔 6 s 的两张
截图也只在这些位置变化）后**零差异**，证明配置键在 HEAD 上无视觉效果。
（首轮桌面截屏的 1721x957 图与此结论一致，但取图方式不可复现，故以
本段隐藏窗口实测为准。）

- 「运行时可通过菜单 / Dock 设置卡片双向切换」是 `AppState.hpp` 里的**过期注释**；
  切换按钮（方案 A/B/C）只存在于休眠布局内部的设置卡片/抽屉中，当前 UI 无入口。
- 布局持久化本身存在且有效：`[window] ui_layout_ini_path`
  （`config/viewer.toml:28`）负责 ImGui docking 布局跨重启保留（TIA-90，
  逻辑在 `src/ui/UiRoot.cpp` 的 keep-current-dock 路径）。
- 若未来要恢复三布局：把 `UiRoot::draw` 中按 `ui_layout_mode` 分发
  `draw_floating_dock_layout` / `draw_analysis_rail_layout` 的调用补回
  （TIA-111 删除的代码在 `git show 4a6fa80^:src/ui/UiRoot.cpp`），并同步
  修正 `AppState.hpp` 注释。

### 1.2 主题：5 套（代码默认深色，配置默认浅色覆盖）

`ThemeId` 枚举（`include/ui/Theme.hpp:14-23`），token 定义 `src/ui/Theme.cpp`：

| ThemeId | 配置 id | 菜单名（中文） | 明暗 (`dark`) | 默认性 |
|---|---|---|---|---|
| `kCarbonBlue` (0) | `carbon-blue` | 碳蓝 · 浅色测绘 | 浅 | 配置文件默认（`config/viewer.toml:36`） |
| `kCarbonBlueDark` (1) | `carbon-blue-dark` | 碳蓝 2.0 · 深色测绘 | 深 | **代码默认**（`Theme.cpp:294` `g_active_theme`） |
| `kDeepGraphite` (2) | `deep-graphite` | 石墨 · 深色测绘 | 深 | — |
| `kInstrumentAmber` (3) | `instrument-amber` | 仪器 · 琥珀测绘 | 深 | — |
| `kHighContrastLight` (4) | `high-contrast` | 高对比 · 浅色测绘 | 浅 | — |

**覆盖链（优先级从低到高）**：代码默认 `kCarbonBlueDark`（深色）→
`config/viewer.toml [window] theme = "carbon-blue"`（浅色，仓库默认配置
实际生效值）→ 用户偏好 `preferences.toml [ui] theme`（运行期改动，最高优先）。
因此**「默认主题」要分两层说**：代码默认深色，默认配置生效浅色。

- 切换入口：菜单 视图→主题（`AppChrome.cpp` `draw_theme_menu`，5 套全列）；
  运行期切换写入 `%APPDATA%\geoscatter3d\preferences.toml` / Linux
  `~/.config/geoscatter3d/preferences.toml` 的 `[ui]` 段，不回写 `viewer.toml`。
- 控制面 `menu.view.theme` 的 `set_value` 按 `value % 4` 归一
  （`src/control/MenuComponents.cpp`），**只能到达主题 0-3，高对比 (4) 不可达**
  —— 已知边界，驱动时注意。

### 1.3 面板清单：8 张（注册表是唯一事实来源）

`include/ui/PanelRegistry.hpp`（`kPanelRegistry`，菜单「窗口」/ Ctrl+P 命令面板 /
默认布局构建共用），控制面 `panel.*` 组件与 UI 绘制读取同一份
`AppState::panels`（构造上保证一致，`tests/ComponentRegistryTests.cpp` 强制
1:1 覆盖）：

| PanelId | 控制面 id | 名称 | 默认可见 | 实现文件 |
|---|---|---|---|---|
| `kDataset` | `panel.dataset` | 项目 | 开 | `src/ui/DatasetPanel.cpp` |
| `kRenderSettings` | `panel.render_settings` | 属性 | 开 | `src/ui/RenderSettingsPanel.cpp` |
| `kNavigationMap` | `panel.navigation_map` | 导航图 | 开 | `src/ui/NavigationMapPanel.cpp` |
| `kMeasurement` | `panel.measurement` | 测量 | 开 | `src/ui/MeasurementPanel.cpp` |
| `kRegionStats` | `panel.region_stats` | 区域统计 | 开 | `src/ui/RegionStatsPanel.cpp` |
| `kPerformance` | `panel.performance` | 性能（帧率/流式诊断） | 关 | `src/ui/AuxiliaryPanels.cpp` |
| `kTileInspector` | `panel.tile_inspector` | 瓦片（调试） | 关 | `src/ui/AuxiliaryPanels.cpp` |
| `kLodView` | `panel.lod_view` | 细节层级（调试） | 关 | `src/ui/AuxiliaryPanels.cpp` |

默认可见性定义：`AppState.hpp` `PanelVisibilityState`（业务面板开、性能/调试面板关，
TIA-92 决策）。日志面板已下线，不在注册表中。

### 1.4 多视图 / 多工作区窗口

- 视图：`[viewport] count` 启动 1-4 个；运行期 `Ctrl+N` / 视图菜单 / 顶栏
  `+ 视图` / 控制面 `toolbar.add_view` 新增；视图可拖出为原生 OS 窗口
  （`[window] multi_viewports`，Wayland 等平台 ImGui 可能自动禁用）。
- 工作窗口：`窗口 → 新建工作窗口`（`src/ui/WorkspaceManager.cpp`，
  `create_workspace_window`）把隐藏视图装入独立
  `WorkspaceWindowState`（`AppState.hpp`，含自己的 viewport 集合与
  dataset/render_settings/navigation_map/measurement 组件状态）；
  「恢复默认工作区」走 `restore_default_workspace`。
- 每个视图独立相机与输入；「联动相机」布尔组（`RenderViewState.camera_linked`）
  同步组传播；隐藏标签页/独立窗口之外不渲染（性能优化之一）。
- 控制面组件 `viewport.main` 只读暴露活动视口状态（fps/点数/尺寸/相机位置等）。

## 2. 模块架构

目录职责（`include/` 与 `src/` 同构，头文件即契约）：

| 模块 | 职责 | 关键入口 |
|---|---|---|
| `src/app/` | 配置解析、AppState、主循环编排、数据集会话、tile 流、截图、控制面会话装配 | `AppConfig.cpp`（CLI/TOML）、`ViewerApp.cpp`（`run()` 主循环）、`ViewerAppControlPlane.cpp`（组件注册） |
| `src/render/` | Vulkan 资源所有权与绘制 | `VulkanContext` / `VulkanSwapchain` / `VulkanRenderer`、`PointCloud{Gpu,LodGpu,TileGpu}`、`PointPipeline`、`OffscreenFramebuffer[N]`、`ViewportManager` |
| `src/gui/` | ImGui 生命周期与字体 | `ImGuiLayer.cpp`（`new_frame`）、`UiFonts.hpp`（ui_scale，见红线 1） |
| `src/ui/` | 全部界面绘制（读 AppState，产出 UiActions） | `UiRoot.cpp`（docking 编排/`draw`）、`AppChrome.cpp`（顶栏/状态栏/主题菜单/命令面板）、`WorkbenchUi.cpp`、`FloatingDockUi.cpp`（休眠）、`AnalysisRailUi.cpp`（休眠）、`WorkspaceManager.cpp`、`Theme.cpp`（5 套主题 + palette 全局色） |
| `src/control/` | 控制面：TCP + JSON-RPC 2.0 服务器线程、组件注册表 | `ControlPlane.cpp`（`poll()` 帧边界分发）、`ComponentRegistry.cpp`、`Panel/Menu/Toolbar/Status/Overlay/Gizmo/CanvasComponents.cpp` |
| `src/camera/` | 相机数学与多视口同步 | `ViewerCameraFrameSystem.cpp` 等（app 侧封装） |

**依赖方向**：`platform`(GLFW) → `gui`(ImGui 生命周期) → `ui`(绘制) →
`app`(状态/编排) → `render`(Vulkan) 与 `control`(协议)。UI 与控制面组件都只
读写 `AppState`，不直接触碰渲染资源；渲染状态由 `ViewerFrameStateSynchronizer`
每帧镜像进 `AppState`。

**一次帧循环里 UI 与渲染的交接**（`ViewerApp::run()`，逐行核验）：

1. `frame_state_synchronizer.synchronize()`：真实渲染状态（fps/可见点数/瓦片/相机）
   写入 `AppState`；
2. `control_session.poll()`：控制面请求在帧边界取入——必须在 ImGui
   `new_frame` 之前，命令效果才会出现在本帧 UI 与截图里（帧同步保证）；
3. `imgui_layer.new_frame(app_state)`：`UiRoot::draw` 从 `AppState` 读状态、
   绘制 UI、产出 `UiActions` 命令队列；
4. `apply_control_actions()`：控制面动作并入 `UiActions`（主题切换直接
   `apply_theme`）；
5. 相机/渲染设置/快捷键/拾取等命令按 `UiActions` 执行；
6. `frame_renderer.render()`：Vulkan 离屏渲染各视口 → `ImGui::Image` 呈现；
   控制面 `screenshot` 在该帧 post_pass 读回并编码 PNG。

**约束**：UI 只产出命令（`UiActions`），状态单一来源是 `AppState`，主循环消费
——控制面与 UI 因此天然共享同一份真相。

## 3. 构建与运行手册（以下命令全部实测通过）

### 3.1 完整命令

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # Windows 需加 -DCMAKE_TOOLCHAIN_FILE=<vcpkg toolchain>
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Windows 实测结果（2026-08-16，VS2026 + vcpkg）：构建成功，
`ctest` **42/42 通过**（约 2.1s，含 FreshClonePreprocess / FreshCloneBundle
无窗口 smoke test）。可执行文件在 `build/Release/GeoScatter3D.exe`（测试程序
同名前缀 `GeoScatter3D*Tests.exe`）。

注意：`data/` 整体被 `.gitignore` 忽略，干净克隆下默认 `config/viewer.toml`
的 `bundle_dir` 不存在；先跑 `config/sample-viewer.toml` 或预处理生成数据
（见 3.3）。

### 3.2 命令行参数表

| 参数 | 含义（实现位置：`src/app/AppConfig.cpp` / `src/main.cpp`） |
|---|---|
| `--config <path>` / `-c` | 指定 TOML 配置（默认 `config/viewer.toml`） |
| `--bundle <dir>` | 打开目录式 `.gs3d.bundle` 项目（覆盖 input mode） |
| `--input <file>` | 打开现有 `.gs3d` 文件（gs3d 模式） |
| `--csv <file>` | CSV 输入，启动时转换生成 GS3D + LOD/tile |
| `--dat <file>` | 同 csv，分隔符自动检测（逗号/制表/空白） |
| `--validation` | 启用 Vulkan validation layers（调试用） |
| `--control-plane` | 启用控制面，默认端口 **12735**（只绑 127.0.0.1） |
| `--control-plane=PORT` / `--control-plane PORT` | 指定端口（下个参数为纯数字时视为端口） |
| `--headless` | 隐藏窗口运行（渲染/截图路径与 GUI 一致；需要可用 GPU 与桌面会话）。**同时忽略持久化布局** `ui_layout_ini_path`（TIA-151：该 ini 可能记录其他视口上的窗口，ImGui 多视口会为它们创建可见原生窗口——既闯入桌面又让主窗口截图缺内容） |
| `--no-welcome` | 跳过欢迎窗口直接进入查看器（**自动化流程必须带**，见第 5 节） |

端口被占用时控制面启动失败但应用继续以 GUI 方式运行，不崩溃（实测日志行为）。

### 3.3 数据集 / bundle 位置

- 仓库样例源：`examples/sample-points.csv`（26 行，25 个 golden 点）。
- 一键生成 + 打开：`./build/GeoScatter3D --config config/sample-viewer.toml`
  （生成到被忽略的 `data/sample-points.gs3d.bundle/`）；只转换不开窗口用
  `./build/GeoScatter3DPreprocess --config config/sample-viewer.toml`。
- 默认配置 `config/viewer.toml` 指向 `data/field_and_ele_source_rec_50m.gs3d.bundle`
  （约 403 MiB，3302 万点，本机实测 `--bundle` 启动 + 控制面截图正常；
  `viewport.main` 报可见点数约 387 万、GPU 显存约 77 MiB）。
- **相对路径陷阱（实测教训）**：`--bundle` 等路径按 `ResourcePath` 搜索根解析
  （`src/app/ResourcePath.cpp`），相对路径可能落到可执行目录
  （如 `build/Release/data/...`）→ 报 `bundle directory not found`。
  自动化脚本一律传**绝对路径**。

## 4. 程序化驱动指南（控制面）

协议细节（传输、线程模型、错误码、`execute`/`toggle`/`set_value`/`click`
语义、通知）见 [docs/control-plane.md](control-plane.md)，本节给出可复制的
完整会话。控制面在查看器阶段生效；**欢迎窗口阶段不监听**。

### 4.1 启动

```bash
./build/Release/GeoScatter3D.exe \
  --bundle "D:/code/GeoScatter3D/data/sample-points.gs3d.bundle" \
  --no-welcome --control-plane=12735
```

日志出现 `[CONTROL] listening on 127.0.0.1:12735` 即可连接
（实测：亦可省略 `=12735` 使用默认端口）。

### 4.2 完整会话（Python，逐条实测；换行分隔 JSON，一行请求一行响应）

```python
import json, socket, base64

s = socket.create_connection(("127.0.0.1", 12735), timeout=5)

def req(method, params=None, rid=1):
    msg = {"jsonrpc": "2.0", "id": rid, "method": method}
    if params is not None:
        msg["params"] = params
    s.sendall((json.dumps(msg) + "\n").encode("utf-8"))
    buf = b""
    while b"\n" not in buf:
        buf += s.recv(1 << 20)
    return json.loads(buf.split(b"\n", 1)[0])

print(req("ping", rid=1))                       # 健康检查 → "pong"
print(req("list_components", rid=2))            # 自我发现，29 个组件（见 component-registry.md）
print(req("get_state", {"component": "panel.performance"}, rid=3))   # 查询面板可见性
print(req("toggle", {"component": "panel.performance"}, rid=4))      # 切换性能面板
print(req("get_state", {"component": "viewport.main"}, rid=5))       # 视口状态（fps/点数/尺寸）

r = req("screenshot", rid=6)                    # 帧同步截图：必含处理帧效果
open("shot.png", "wb").write(base64.b64decode(r["result"]["data"]))  # PNG, 画布区域

print(req("set_value", {"component": "menu.view.theme", "value": 2}, rid=7))  # 切到深色主题
print(req("quit", rid=8))                       # 干净退出 → {"ok": true}，进程下一帧结束
```

实测请求/响应片段（2026-08-16）：

```text
→ {"jsonrpc":"2.0","id":1,"method":"ping"}
← {"id":1,"jsonrpc":"2.0","result":"pong"}
→ {"jsonrpc":"2.0","id":3,"method":"get_state","params":{"component":"panel.performance"}}
← {"id":3,"jsonrpc":"2.0","result":{"id":"panel.performance","name":"性能","type":"panel","debug":false,"visible":false}}
→ {"jsonrpc":"2.0","id":4,"method":"toggle","params":{"component":"panel.performance"}}
← {"id":4,"jsonrpc":"2.0","result":{"id":"panel.performance","visible":true}}
→ {"jsonrpc":"2.0","id":2,"method":"get_state","params":{"component":"viewport.main"}}
← {"id":2,"jsonrpc":"2.0","result":{"active_viewport_index":0,"camera_position":"0.0, 0.0, 143.4","fps":872.5,"gpu_memory_bytes":1000,"height":929,"loaded_tiles":1,"pending_tiles":0,"viewport_count":4,"visible_points":25,"width":1328}}
→ {"jsonrpc":"2.0","id":9,"method":"quit"}
← {"id":9,"jsonrpc":"2.0","result":{"ok":true}}
   （应用日志收尾: [PASS] ViewerApp finished.）
```

错误路径（实测）：

```text
→ method "bogus_method"        ← error -32601 "method not found: bogus_method"
→ get_state panel.nonexistent  ← error -32602 "unknown component: panel.nonexistent"
```

### 4.3 驱动要点

- `screenshot` 返回**整个窗口完整客户区**（swapchain 全幅回读，`offset={0,0}`、
  extent=swapchain extent，见 `ViewerAppScreenshot.cpp:request_control_capture`），
  PNG Base64；宽高=实际渲染分辨率（如 1280x720 逻辑窗口 @1.25 DPI → 2054x1242，
  含全部面板/Dock）。同一时刻只允许一张在途，超过 2 s 回 `-32000` 超时。
  先 `toggle` 再 `screenshot`，截图里就能看到效果（帧同步，不会截到上一帧）。
  （UI 保存面板路径的截图才裁剪到活动主视口画布区域；控制面截图不裁剪。）
- `toggle`/`set_value`/`click` 在帧边界应用，返回时已生效（结果含新状态）。
- `--headless` 下控制面与截图路径完全一致（实测 2054x1242 PNG 正常回读；
  隐藏窗口时 swapchain acquire/present 正常，validation layers 无 VUID 报错）。
- 组件清单以 `list_components` 实时返回为准；29 个组件 = 8 面板 + viewport.main
  + 9 菜单 + 6 工具栏 + 状态栏 + 2 overlay + 导航球 + 视口画布。

### 4.4 自动化截图规范（硬性约定：禁止桌面截屏）

> 背景：TIA-151 评估期间截图走桌面/窗口截屏，窗口抢占了人类决策者的前台，
> 被遮挡/未完整露出时截图残缺；且窗口本身干扰办公。以下为**硬性要求**。

1. **禁止任何 OS 级桌面/窗口截屏**：BitBlt / PrintWindow / 截图工具
   （PowerToys、微信、PIL ImageGrab 等）一律不得用于取证截图；也不得让
   窗口获得前台焦点。
2. **取图一律走控制面 `screenshot`（swapchain 回读）**，输出=完整客户区、
   与实际渲染分辨率一致、不受遮挡影响。
3. **全程隐藏窗口**：自动化启动必须 `--headless`（窗口创建即隐藏，
   `GLFW_VISIBLE=false`）+ `--no-welcome`；`--headless` 已保证不加载
   持久化布局、不创建可见原生窗口（TIA-151 实测：运行期间枚举顶层窗口
   全部 `IsWindowVisible=False`、`MainWindowHandle=0`）。
4. 验证窗口未出现（运行期间可执行）：

   ```powershell
   Get-Process -Name GeoScatter3D | Select-Object MainWindowHandle  # 期望 0
   ```

5. 取证截图的分辨率以控制面返回的 `width`/`height` 为准，汇报时注明。

## 5. 进程善后规范（硬性约定）

> 背景：TIA-150 评估期间，多个自动化会话反复启动程序且未退出，堆积了 7 个
> 卡在「欢迎」窗口的进程。以下约定为**硬性要求**，任何自动化流程必须遵守。

1. **必须用 `--no-welcome` 启动**——欢迎窗口阶段不监听控制面、无法被
   `quit` 驱动，是残留进程的第一来源。
2. **必须用控制面 `quit` 退出**（`{"method":"quit"}` → `{"ok":true}`），
   不要直接杀进程；退出后应用日志会打印 `[PASS] ViewerApp finished.`。
3. **善后检查（收尾必跑，期望输出为空）**：

   ```powershell
   Get-Process -Name GeoScatter3D -ErrorAction SilentlyContinue | Select-Object Id
   ```

4. 发现残留时的清理（先试控制面 quit，失败再强杀）：

   ```powershell
   # 端口上还有监听时先 quit
   # 强杀（仅用于卡死/无法驱动的进程）：
   Get-Process -Name GeoScatter3D -ErrorAction SilentlyContinue | Stop-Process -Force
   ```

5. 同一端口只能有一个控制面监听；多个实例抢端口时后启动者继续 GUI 运行，
   容易制造「看起来活着但驱动不了」的进程——**一次只跑一个实例**，跑完确认
   清空再跑下一个。

TIA-151 全程验证：每次运行均以 `quit` 退出，结束时
`Get-Process -Name GeoScatter3D` 为空（截图见交付附件）。

## 6. 既有红线集中登记（散在代码注释里的约束，收拢于此）

| # | 红线 | 出处 |
|---|---|---|
| 1 | `ui_scale` 只能用于 ImGui 外观（字体/样式度量），**禁止流入 Vulkan 渲染尺寸链**：swapchain extent、offscreen framebuffer、viewport/scissor、鼠标拾取映射、`io.DisplayFramebufferScale` | `include/gui/UiFonts.hpp:17-21` |
| 2 | GPU pick 写出的 `point_id` 只保证**单次运行内稳定**，不是跨运行/跨文件的持久 ID，不得存盘或长期引用 | `ViewerPickSystem`（见 architecture.md「GPU Pick Contract」） |
| 3 | LOD v1 sidecar 已被读取端**显式拒绝**（提示重新生成 v2）；tile 读取端按 stride 强校验，损坏/手工拼装文件显式拒绝，不得静默置零解码 | `docs/spec/gs3d-format.md`、architecture.md 风险 2 |
| 4 | `tile.max_visible_tiles` 已废弃：任何值都不截断可见 tile（硬截断致块状伪影），极端视图预算由连续自适应 LOD 解决 | `docs/config-reference.md` [tile] |
| 5 | `--headless` 需要可用 GPU 与桌面会话；纯离屏无显示器 CI 不在当前范围 | `src/main.cpp`、control-plane.md §6 |
| 6 | 控制面只绑 `127.0.0.1`、无认证（设计决策）；并发连接上限 60；单条消息 64 MiB；单飞截图（同一时刻一张，2 s 超时） | `docs/control-plane.md` §2/§6 |
| 7 | `ThreadPool::shutdown()` 取消未开始工作并令对应 future 抛 `TaskCancelled`；pool 必须比它返回的每个 future 活得更久 | `src/util/`（见 architecture.md 风险 6） |
| 8 | 控制面 `menu.view.theme` 的 `set_value` 只接受主题 0-3（`% 4`），高对比主题 (4) 不可达；UI 菜单则五套齐全 | `src/control/MenuComponents.cpp` |
| 9 | 控制面 `screenshot` 截的是**整窗完整客户区**（swapchain 全幅回读，非画布裁剪；UI 保存面板路径才裁剪画布） | `ViewerAppScreenshot.cpp:request_control_capture` |
| 10 | 控制面在**欢迎窗口阶段不监听**；自动化必须 `--no-welcome` | `docs/control-plane.md` §6、本手册第 5 节 |
| 11 | **禁止 OS 级桌面/窗口截屏取证**（BitBlt/PrintWindow/第三方工具）；窗口不得获得前台焦点；取图一律控制面 `screenshot`（swapchain 回读），自动化全程 `--headless` 隐藏窗口 | 本手册 §4.4、`src/main.cpp`（headless 忽略布局 ini） |

## 7. 给文档读者的核对指引

- 怀疑文档过期时按优先级核对：源码 > 本手册 > architecture.md >
  config-reference.md > README.md；修改实现行为后须同步更新这些文档
  （见 `CONTRIBUTING.md` 与 `docs/agents/domain.md` 的文档-代码一致性约定）。
- 行号引用类信息以「核对基准」日期为准，跨版本后请复查。
