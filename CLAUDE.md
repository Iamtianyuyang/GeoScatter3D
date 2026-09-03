# GeoScatter3D · CppStudio 开发者与智能体指令（CLAUDE.md）

> 项目定性：大规模三维散点/点云数据 C++20 桌面高性能查看器（Vulkan 1.3 + GLFW + Dear ImGui Docking + 自研 GS3D/LOD/Tile 格式）。
> 核心治理体系：已全面接入 **`cpp-cuda-vulkan-studio`** 现代化 C++ 图形工程规范。
> 统一规则基准：详细规则与解耦规范见 [`docs/RULES.md`](docs/RULES.md)。

---

## 一、CppStudio 过程状态与技术路由（Studio Router）

每次任务前，识别并明确唯一的活动过程状态与涉及的技术叠加层（Technical Overlays）：

### 1. 过程状态选择（Process State）
- **Standard（默认）**：明确、局部、低风险的功能修复或小改动。单一假设，单次修改，标准测试验证。
- **Investigative**：所有权不清晰、架构耦合定位、API 行为不确定、性能瓶颈排查、或单次假设失败时切入。严禁盲改，必须以 MCP 图谱与运行探针为证据。
- **Governed**：跨模块重构（`AppState` 瘦身、`run()` 阶段化、代码解耦）、多组件改动或破坏性变更。
- **Recovery**：发生循环失败、补丁堆叠或违背红线时进入事故恢复，立即回退稳定基准。

### 2. 技术叠加层路由（Technical Overlays）
| 关注领域 | 涉及技术契约 | 规范要求 |
| :--- | :--- | :--- |
| **图形与同步** | Vulkan 1.3、描述符生命周期、管线屏障 | 严禁隐式同步；离屏 Framebuffer 严格按生命周期销毁；禁止向 UI 层泄漏 Vulkan 头文件 |
| **界面与交互** | Dear ImGui (Docking)、碳蓝工作台 2.0 | 立即模式心智模型；状态外置；UI 仅输出 `UiActions`；禁止在 UI 帧内执行耗时重计算 |
| **视口与相机** | 漫游视口、相机枢轴、GPU Pick | 默认以点云几何中心为旋转枢轴；GPU pick 为 front-most 最小深度回读；坐标映射与视口像素精确对齐 |
| **控制面与自动化** | TCP + JSON-RPC 2.0、无头驱动、截图回读 | 自动化必须 `--headless` + `--no-welcome`；取图必须走控制面 `screenshot`；退出必须验证进程消亡 |
| **构建与护栏** | CMake 3.24+、MSVC 2026 / Ninja、CTest | 维护工程护栏预算（`scripts/check_engineering_guardrails.py`）；CTest 37 组测试全绿保证 |

---

## 二、不可逾越的硬性约定（违反即事故）

1. **自动化驱动与退出善后**：
   - 必须带 `--no-welcome` 启动，必须通过控制面 `quit` 或优雅关闭退出；
   - 收尾必须执行 `Get-Process -Name GeoScatter3D` 确保进程池无残余（防锁死显存与端口）。
2. **自动化截图规范（红线 #2）**：
   - 自动化截图**必须走控制面 `screenshot`**（Vulkan swapchain 客户区全幅回读）；
   - **绝对禁止**操作系统级桌面/窗口截屏（BitBlt / PrintWindow / 第三方截图工具），严禁抢占系统前台焦点；全流程必须开启 `--headless`。
3. **UI 缩放与 Vulkan 隔离**：
   - `ui_scale_multiplier` 仅缩放 ImGui 字体与界面尺寸，**严禁**流入 Vulkan 渲染像素链。
4. **数据格式安全性**：
   - 全面采用固定小端 GS3D v2；LOD v1 显式拒绝；Tile 读取端按 `point_stride` 强校验，严禁接受损坏或混合 stride 文件。
5. **代码是唯一信源**：
   - 当文档与代码行为不一致时，以实际构建运行的代码为准，并同步修订文档。

---

## 三、常用构建、运行与验证命令

### 1. 一键编译与运行（VS Code / 脚本）
- **VS Code 内**：直接按下 **`F5`** 即可触发增量编译并使用 MSVC 原生调试器运行。
- **终端一键脚本**：
  ```powershell
  # 运行最新构建 (Release)
  .\run.bat
  # 或 PowerShell 模式
  .\scripts\run.ps1 -Config Release
  # 无头模式启动（用于控制面自动化驱动）
  .\scripts\run.ps1 -NoBuild --headless --no-welcome --control-plane=9871
  ```

### 2. 自动化测试与质量门禁
```powershell
# 1. 运行全部 CTest 单元测试（Release 配置）
ctest --test-dir build-win -C Release --output-on-failure

# 2. 依赖合规性检查（检查模块间包含关系）
C:/Users/tianyy/miniconda3/python.exe scripts/check_include_deps.py

# 3. 工程护栏检查（单文件与核心函数代码行数预算）
C:/Users/tianyy/miniconda3/python.exe scripts/check_engineering_guardrails.py
```

### 3. 进程清理与善后
```powershell
# 检查是否有残留进程
Get-Process -Name GeoScatter3D -ErrorAction SilentlyContinue

# 强制终止所有残余实例
Stop-Process -Name GeoScatter3D -Force -ErrorAction SilentlyContinue
```

---

## 四、文档导航与架构索引

- **操作总纲**：[`docs/operations.md`](docs/operations.md) —— 当前布局现状、构建配置、控制面协议与故障排查指引。
- **架构全景**：[`docs/architecture.md`](docs/architecture.md) —— 模块划分、资源所有权、逐帧渲染流程。
- **解耦规范**：[`docs/RULES.md`](docs/RULES.md) —— 解耦路线图、状态模型拆解契约。
- **控制面协议**：[`docs/control-plane.md`](docs/control-plane.md) —— JSON-RPC 方法清单与帧同步语义。
- **格式规范**：[`docs/spec/gs3d-format.md`](docs/spec/gs3d-format.md) —— GS3D v2 / LOD / Tile 二进制结构定义。
