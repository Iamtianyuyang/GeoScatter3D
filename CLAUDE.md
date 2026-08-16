# GeoScatter3D 仓库导航

C++20 大规模三维散点/点云桌面查看器（Vulkan + GLFW + Dear ImGui docking，
自研 GS3D/LOD/tile 格式）。本文件只负责路由；全部细节在 docs 下。

## 按任务类型选文档

- **先读总纲**：[`docs/operations.md`](docs/operations.md) — 能力清单（布局/主题/
  面板/多视图现状，以代码为准）、模块架构、构建与运行命令、控制面驱动会话、
  进程善后硬性约定、既有红线登记。新智能体/新人从这一篇建立全局认知。
- **架构与数据流**：[`docs/architecture.md`](docs/architecture.md) — 分层、逐帧流程、
  资源所有权、当前功能状态、重大风险。
- **配置**：[`docs/config-reference.md`](docs/config-reference.md) — viewer.toml
  全量键、用户偏好（preferences.toml）、环境变量。
- **控制面协议**：[`docs/control-plane.md`](docs/control-plane.md) — JSON-RPC 2.0
  方法、帧同步语义、错误码；组件清单见
  [`docs/component-registry.md`](docs/component-registry.md)（29 个组件）。
- **格式规范**：[`docs/spec/gs3d-format.md`](docs/spec/gs3d-format.md) —
  GS3D v2 / LOD / tile 二进制格式。
- **构建/测试/分发**：[`README.md`](README.md)；变更史见 `CHANGELOG.md`。

## 硬性约定（违反即事故）

- **自动化驱动必须** `--no-welcome` 启动、控制面 `quit` 退出；收尾跑
  `Get-Process -Name GeoScatter3D` 确认为空（背景：TIA-150 曾堆积 7 个卡在
  欢迎窗口的进程）。详见 operations.md 第 5 节。
- **文档与代码冲突时以代码为准**，并同步更新文档（见 operations.md 第 7 节与
  `docs/agents/domain.md`）。
- **红线登记**：ui_scale 禁止流入 Vulkan 渲染尺寸链、GPU pick point_id 仅单次
  运行内稳定、LOD v1 拒绝/tile stride 强校验、`max_visible_tiles` 已废弃等，
  见 operations.md 第 6 节。

## 工程流程

- Issues 在 **Multica** 平台跟踪（不是 GitHub Issues）：用 `multica` CLI。
  见 `docs/agents/issue-tracker.md`；状态语义见 `docs/agents/triage-labels.md`。
- 本仓库不使用 `CONTEXT.md` / `docs/adr/` 布局。领域文档消费方式见
  `docs/agents/domain.md`。
