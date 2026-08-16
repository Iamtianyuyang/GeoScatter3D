# 组件注册契约（ComponentRegistry · TIA-109）

本文档是控制面组件的**注册契约**（TIA-109 设计，TIA-111 已全量接入）：
控制面的命令分发**只走注册表**。注册表以 `list_components` 实时返回为准
（实测 29 个组件，2026-08-16）。有疑问先讨论，不要绕开注册表另起炉灶。

## 1. 核心原则：未注册即失败，注册表是唯一入口

- 控制面的命令分发**只走注册表**：`ComponentRegistry::execute(id, command, params)`。
  组件未注册 → 查找失败 → JSON-RPC 错误（`-32602 unknown component`）。
  **架构上不存在「未注册但默认可用」的路径。**
- 面板组件与 UI 绘制读取**同一份 `AppState::panels`**：控制面 `toggle` 成功
  与界面上真的出现/消失是同一件事，不存在两条真相来源。
- 覆盖保证**由构造产生**，不是白名单自觉：
  - `tests/ComponentRegistryTests.cpp` 遍历 `ui::kPanelRegistry`（UI 侧唯一
    面板清单），要求每张面板都有对应的 `panel.*` 控制组件 —— 新增面板不注册，
    测试立即失败；
  - 覆盖驱动测试遍历注册表中**每个非调试组件**的**每项能力**并真实执行，
    **要求 100% 成功**（不是 90%）。

> 阶段边界：本阶段注册表是控制面的唯一分发入口；「UI 绘制本身必须经由注册表
> 才能画出组件」的强绑定留给下一阶段把全部可交互元素接入时落实（见 §6）。

## 2. 组件模型

```cpp
// include/control/ComponentRegistry.hpp（摘录）
enum class ComponentType { kPanel, kViewport, kToolbar, kMenu, kButton,
                           kSlider, kDropdown, kCheckbox, kInput, kGizmo,
                           kStatus, kOverlay, kCount };

struct CommandSpec {
    std::string command;        // 稳定命令名："toggle" / "set_value" / "get_state" ...
    std::string description;    // 人可读说明（list_components 暴露给 AI）
    bool requires_param;
};

struct ComponentInfo {
    std::string id;             // 稳定 ID："panel.dataset"、"menu.view.theme"
    std::string name;
    std::string description;
    ComponentType type;
    bool debug;                 // 调试组件：可驱动但不参与 100% 覆盖测试
};

class Component {
public:
    virtual const ComponentInfo& info() const = 0;
    virtual const std::vector<CommandSpec>& capabilities() const = 0;
    virtual nlohmann::json get_state() = 0;                     // 可读状态快照
    virtual nlohmann::json execute(const std::string& command,
                                   const nlohmann::json& params) = 0;
};
```

- `execute` 失败抛 `ComponentError(code, message)`（code 用 JSON-RPC 风格：
  `-32601` 未知命令、`-32602` 非法参数）。
- `get_state`/`execute` 只在主线程（渲染循环）调用；实现**不得阻塞**、
  不得重入注册表。

## 3. 当前注册的组件（TIA-111 全量接入，共 29 个）

| id | 类型 | 能力 | 说明 |
|---|---|---|---|
| `panel.dataset` | panel | toggle / set_value / set_visible / get_state | 项目面板 |
| `panel.render_settings` | panel | 同上 | 属性面板 |
| `panel.performance` | panel | 同上 | 性能面板（默认隐藏） |
| `panel.navigation_map` | panel | 同上 | 导航图面板 |
| `panel.measurement` | panel | 同上 | 测量面板 |
| `panel.region_stats` | panel | 同上 | 区域统计面板 |
| `panel.tile_inspector` | panel（debug） | 同上 | 瓦片调试面板 |
| `panel.lod_view` | panel（debug） | 同上 | LOD 调试面板 |
| `viewport.main` | viewport | get_state | 活动视口渲染状态（fps/点数/尺寸等） |
| `menu.file.open` | menu | click / get_state | Ctrl+O 打开数据文件 |
| `menu.file.open_bundle` | menu | click / get_state | 打开 GS3D Bundle 项目 |
| `menu.file.screenshot` | menu | click / get_state | 触发截图保存 |
| `menu.view.new_view` | menu | click / get_state | Ctrl+N 新建视图 |
| `menu.view.restore_workspace` | menu | click / get_state | 恢复默认工作区 |
| `menu.view.theme` | menu | click / set_value / get_state | 主题切换；`set_value` 按 `value % 4` 归一（0-3），高对比 (4) 不可达 |
| `menu.window.new_workspace` | menu | click / get_state | 新建工作窗口 |
| `menu.help.welcome` | menu | click / get_state | 显示欢迎页 |
| `menu.help.shortcuts` | menu | click / get_state | 快捷键总览 overlay |
| `toolbar.open` | toolbar | click / get_state | 顶栏打开按钮 |
| `toolbar.screenshot` | toolbar | click / get_state | 顶栏截图按钮 |
| `toolbar.add_view` | toolbar | click / get_state | 顶栏新建视图按钮 |
| `toolbar.measure` | toolbar | click / get_state | 测量模式开关 |
| `toolbar.link_camera` | toolbar | click / get_state | 相机联动开关 |
| `toolbar.panel_palette` | toolbar | click / get_state | 面板命令面板开关 |
| `status.bar` | status | get_state | 状态栏只读状态 |
| `overlay.shortcut` | overlay | toggle / set_visible / get_state | 快捷键总览 |
| `overlay.panel_palette` | overlay | toggle / set_visible / get_state | 面板命令面板 |
| `gizmo.navigation` | gizmo | click / get_state | 导航球 |
| `canvas.viewport` | canvas | get_state / focus_point / reset_camera | 视口画布 |

面板组件的装配在 `src/control/PanelComponents.cpp`（`make_panel_components`），
菜单/工具栏/状态栏/overlay/gizmo/canvas 组件分别由 `MenuComponents.cpp`、
`ToolbarComponents.cpp` 装配；全部组件在 `ViewerAppControlPlane.cpp`
的 `ControlPlaneSession` 构造时注册，与 UI 绘制共享同一个 `AppState`。

## 4. 如何接入新组件（imgui-coder 步骤）

1. **实现 `Component` 子类**：稳定 ID（见 §5 命名）、能力表、`get_state`、
   `execute`。纯逻辑部分放进 `src/control/`，需要渲染/ImGui 状态的放进
   `src/app/`（如 `ViewportStateComponent` 先例）。
2. **在 `ControlPlaneSession` 构造函数注册**（`ViewerAppControlPlane.cpp`）：
   ```cpp
   registry_.register_component(std::make_unique<MyComponent>(...));
   ```
   重复 ID 会抛异常（防止两个人接同名组件）。
3. **保证状态单一来源**：组件读写 `AppState`/现有系统状态，不要自建副本。
4. **ctest 覆盖**：`ComponentRegistryTests` 的 100% 驱动测试会自动覆盖新注册
   的非调试组件（每个能力都会被真实执行）；保证 `execute` 对文档化参数
   必定成功、对非法参数必定抛 `ComponentError`。
5. **更新本文档 §3 表格**。

## 5. 命名约定

- ID 用 `<type>.<name>`：`panel.dataset`、`menu.view.theme`、`toolbar.tools`、
  `viewport.main`、`status.bar`、`overlay.shortcut`、`gizmo.navigation`。
- 命令名用小写动词：`toggle` / `set_value` / `set_visible` / `get_state` /
  `click` / `reset` / `focus`。
- 参数用 JSON 对象；布尔值参数统一用 `{"value": bool}`（`set_value` 的
  通用约定），组件可额外声明语义化参数（如 `set_visible` 的 `{"visible": bool}`）。

## 6. 已接入 / 后续清单

- 已接入（TIA-111）：全部 8 张面板、9 个菜单项、6 个工具栏按钮、状态栏、
  2 个 overlay、导航球、视口画布（共 29 个组件，`list_components` 实测）。
- 后续仍可做：
  - 「UI 绘制唯一入口」强绑定：面板/控件的绘制改为经由注册表描述符驱动，
    使「新增 UI 元素不注册就画不出来」成为编译/测试期事实；
  - 事件订阅（`subscribe`）留给后续阶段（协议层已预留方法位）。
