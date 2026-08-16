# 控制面协议（TIA-109）

GeoScatter3D 的 AI/外部驱动接口：本地 TCP + JSON-RPC 2.0。外部 agent 通过
稳定的程序化接口完全操作应用（命令 + 状态查询 + 截图），GUI 与 headless 双模式。

- 设计决策出处：TIA-108 决策表（解读 (a) 外部程序化驱动）。
- 组件契约（注册表）见 [`docs/component-registry.md`](component-registry.md)。

## 1. 启动方式

控制面**编译默认包含**，运行时**默认不监听**，用命令行显式开启：

```text
GeoScatter3D.exe --bundle <dir> --no-welcome --control-plane[=port]
GeoScatter3D.exe --bundle <dir> --no-welcome --headless --control-plane=12735
```

| 参数 | 含义 |
|---|---|
| `--control-plane` | 启用控制面，默认端口 **12735**（只绑 `127.0.0.1`） |
| `--control-plane=PORT` / `--control-plane PORT` | 指定端口 |
| `--headless` | 隐藏窗口运行（无可见 UI、无交互）；渲染/交换链/截图路径与 GUI 完全一致，需要可用 GPU 与桌面会话；无显示器的纯离屏 CI 场景不在本阶段范围 |
| `--no-welcome` | 跳过欢迎窗口直接进入查看器（自动化的前提） |

端口被占用时控制面启动失败，应用记录错误后**继续以 GUI 方式运行**，不会崩溃。

## 2. 传输与帧协议

- TCP，仅监听 `127.0.0.1`；本地进程，无认证（设计决策）。
- 消息为**换行分隔的 JSON**：一行一个 JSON-RPC 请求，一行一个响应。
- 单条消息上限 64 MB（截图 Base64 可能达数 MB）。
- 同一连接上的响应按请求顺序回送；客户端可流水线发送多个请求。
- 并发连接上限 60（`select` 的 fd_set 限制；正常使用 1 个驱动连接即可）。

### 线程模型（为什么渲染不被阻塞）

- **服务器线程**独占全部 socket I/O：单次 `select()`（50 ms 超时）同时等待
  监听、唤醒与全部连接 socket；收请求、发响应都在该线程完成。
- **主线程（渲染循环）**每帧 `poll()` 一次：取出请求 → 分发（唯一触碰
  ComponentRegistry 的路径）→ 响应进入出站队列，并通过 UDP 唤醒 socket
  立即通知服务器线程发送。
- 无跨线程并发访问同一 socket；关闭 socket 永远发生在 join 之后。

> 实现注记：Windows 上 `WSAPoll` 存在缺陷（超时可能不生效、对已关闭 socket
> 可能永久阻塞），因此统一使用 `select()`（Windows）/ `poll()`（POSIX），
> 且绝不跨线程关闭正在等待的 socket。这些是真实运行中验证过的结论。

## 3. JSON-RPC 方法

所有请求形如：

```json
{"jsonrpc":"2.0","id":1,"method":"<method>","params":{...}}
```

### `ping` → `"pong"`

健康检查。

### `list_components` → 组件清单

把注册表暴露给外部 AI 做自我发现（ID、类型、能力、是否调试组件）：

```json
{"jsonrpc":"2.0","id":2,"method":"list_components"}
→ {"jsonrpc":"2.0","id":2,"result":{"components":[
    {"id":"panel.dataset","name":"项目","description":"...","type":"panel",
     "debug":false,"capabilities":[
        {"command":"toggle","description":"切换面板显示/隐藏","requires_param":false},
        {"command":"set_value","description":"...","requires_param":true},
        {"command":"get_state","description":"...","requires_param":false}]},
    ...]}}
```

### `get_state`（同步）

```json
{"jsonrpc":"2.0","id":3,"method":"get_state","params":{"component":"panel.performance"}}
→ {"jsonrpc":"2.0","id":3,"result":{"id":"panel.performance","name":"性能",
   "description":"帧率与流式加载诊断","type":"panel","debug":false,"visible":false}}
```

### `execute` / `toggle` / `set_value` / `click`（异步动作，帧边界应用）

```json
{"jsonrpc":"2.0","id":4,"method":"execute",
 "params":{"component":"panel.performance","command":"set_visible","params":{"visible":true}}}
{"jsonrpc":"2.0","id":5,"method":"toggle","params":{"component":"panel.dataset"}}
{"jsonrpc":"2.0","id":6,"method":"set_value","params":{"component":"panel.performance","value":true}}
{"jsonrpc":"2.0","id":7,"method":"click","params":{"component":"<button组件>"}}
```

- `toggle`/`set_value`/`click` 是 `execute` 的便捷形式。
- 语义：请求在主线程帧边界应用（对 `AppState` 的修改与 UI 绘制读取的是
  **同一份状态**），应用后立即返回结果（含应用后的状态）。
- `click` 只对声明了 `click` 能力的组件有效；未声明即返回错误。

### `screenshot`（帧同步）

```json
{"jsonrpc":"2.0","id":8,"method":"screenshot"}
→ {"jsonrpc":"2.0","id":8,"result":{"width":1280,"height":720,"format":"png",
   "data":"<PNG base64>"}}
```

**帧同步保证**：请求不立即响应；主线程在**处理请求的那一帧**渲染前武装截图，
该帧 post_pass 把交换链图像拷回 staging buffer，读回并编码 PNG 后回送。
返回的截图必定是「请求被处理那一帧」的渲染结果 —— 先发 `toggle` 再发
`screenshot`，截图里就能看到 toggle 的效果，不会截到上一帧。

- 截图内容是**整个窗口完整客户区**（swapchain 全幅回读，`ViewerAppScreenshot.cpp`
  的 `request_control_capture`：`offset={0,0}`、extent=swapchain extent；宽高即实际
  渲染分辨率）。注意与 GUI 的「截图」按钮不同——后者裁剪到活动主视口画布区域。
- 同一时刻只允许一张控制面截图；已有进行中时返回错误。
- 超过 2 s 未完成回送 `-32000 screenshot capture timed out`。

### `quit`（扩展）

```json
{"jsonrpc":"2.0","id":9,"method":"quit"}
→ {"jsonrpc":"2.0","id":9,"result":{"ok":true}}
```

应用在下一帧退出（响应尽力送达；客户端应容忍连接关闭）。用于 headless 干净退出。

### 通知

不带 `id` 的请求是通知：执行但**不回响应**（JSON-RPC 2.0 规范）。

## 4. 命令语义

| 命令 | 类型 | 完成判定 | 幂等 |
|---|---|---|---|
| `get_state` | 同步 | 返回时结果已就绪 | 是 |
| `list_components` / `ping` | 同步 | 返回时结果已就绪 | 是 |
| `screenshot` | 帧同步 | 返回时截图已含处理帧效果 | 是 |
| `toggle` / `set_value` / `click` | 异步（帧边界） | 返回时已应用（结果含新状态） | toggle/否、set_value/是、click/否 |
| `quit` | 异步 | 返回后应用退出 | — |

## 5. 错误码（JSON-RPC 2.0）

| code | 含义 | 典型场景 |
|---|---|---|
| `-32700` | 解析错误 | 非 JSON / 非 2.0 / 缺 method |
| `-32601` | 方法不存在 | `method` 拼错；组件未声明该命令 |
| `-32602` | 参数非法 | `component` 未知、`command` 缺失、参数类型错 |
| `-32603` | 内部错误 | 分发器异常 |
| `-32000` | 服务器错误 | 截图不可用/进行中/超时 |

错误响应示例：

```json
{"jsonrpc":"2.0","id":3,"error":{
  "code":-32602,"message":"unknown component: panel.nonexistent",
  "data":null}}
```

## 6. 限制与已知边界

- 控制面在查看器（ViewerApp）运行期间生效；欢迎窗口阶段不监听。
- `--headless` 需要可用 GPU 与桌面会话；真正的无显示器纯离屏渲染不在本阶段。
- 截图与 GUI「另存为」路径共用同一 GPU 读回管线（同一时刻一张）。
- 60 并发连接上限；单连接单次未完成消息 64 MB 上限。
