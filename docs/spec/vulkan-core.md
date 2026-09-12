# GeoScatter3D Vulkan 核心规范

状态：现行基线 v0.2（2026-09-12）
范围：多端 Vulkan 核心、离屏引擎、渲染线程、平台 Surface 适配和 C-ABI 边界。
变更（v0.2）：渲染线程（`EngineHost`）与非阻塞帧通道（`FrameChannel`）落地，
`gs3d_engine_request_frame` / `read_frame_rgba` / `frame_sequence` /
`last_render_ms` 进入 C-ABI；内核帧资源改为跨帧复用。

## 1. 目标与非目标

核心的第一目标是让同一套 C++20/Vulkan 实现可以被 Windows、Android 以及后续
Linux/macOS/iOS 平台复用。当前 Flutter 是一个调用方，不是渲染核心的所有者；
前端功能按 `gs3d_engine_*` 能力逐项增加。

核心必须满足：

- Vulkan 对象只在 C++ 内部出现；Dart、JSON、文件格式和 UI 不得接触图形句柄。
- 离屏路径不依赖 GLFW、窗口、Surface 或 Flutter，可在 Android 和无窗口测试环境运行。
- 窗口路径通过 `VulkanSurfaceProvider` 注入平台能力，核心不认识 `HWND`、
  `ANativeWindow`、`NSView` 或 `GLFWwindow`。
- 任何引擎不可用、设备不满足要求或资源创建失败都必须返回明确错误，不能退回
  Dart 软件点云或伪造成功帧。

核心不负责：Flutter Widget、平台文件选择器、窗口事件循环、JSON-RPC 路由、
应用业务状态和持久化 UI 状态。

## 2. 模块边界

```text
platform adapter ──┐
                   v
               VulkanContext ── Vulkan resources ── PointPipeline/Framebuffer
                   ^                                      ^
                   │                                      │
             gs3d_engine_core ── data + camera ────────────┘
                   ^
                   │
             gs3d_ffi / JNI / future native frontends
```

当前 CMake 目标的职责：

| 目标 | 允许依赖 | 禁止依赖 | 职责 |
|---|---|---|---|
| `gs3d_camera_core` | C++ 标准库 | app、Flutter、窗口系统 | 相机和输入数学 |
| `gs3d_render` | Vulkan、data、platform 基础文件 I/O | Flutter、GLFW、UI | Vulkan instance/device、buffer、image、pipeline、离屏目标 |
| `gs3d_engine_core` | render、data、camera_core | Flutter、GLFW、JSON-RPC | 单个引擎句柄、数据集、相机、离屏帧和交互状态 |
| `gs3d_ffi` | engine_core + A 组元数据/控制面 | 向 C-ABI 泄漏 Vulkan 类型 | C ABI 适配和内存边界 |

`VulkanSwapchain` 和 `VulkanRenderer` 属于未来的窗口呈现适配层。它们不能成为
离屏引擎的必需依赖；如果重新启用，必须单独建立 window-renderer 目标并由平台
适配器提供 Surface。

## 3. VulkanContext 契约

### 3.1 两种明确模式

- `headless=true`：不创建 Surface，不启用 swapchain 扩展，present queue 与 graphics
  queue 复用；用于 `gs3d_engine_core`。
- `headless=false`：必须提供完整的 `VulkanSurfaceProvider`。缺少 provider 是
  配置错误，构造直接失败，严禁静默切换到 headless。

provider 只包含三类信息：平台所需的 instance extension 名称、创建 Surface 的
函数指针和不透明 `user_data`。核心拥有创建成功后的 `VkSurfaceKHR`，平台拥有
`user_data`，并保证它在回调返回前有效。

### 3.2 生命周期

创建顺序固定为：

```text
instance → optional surface → physical device selection → logical device/queues
          → child resources → frame submission
```

销毁顺序严格反向。`VulkanContext` 必须晚于所有使用 `VkDevice` 的子资源；构造
过程中任一步抛出异常时，已创建的 instance、surface 和 device 必须立即清理。

### 3.3 能力与版本

桌面 Profile 要求 Vulkan 1.3；Android 离屏 Profile 最低为 Vulkan 1.1，以覆盖
三星 Tab S7 等目标设备。设备选择必须把当前 Profile 的 API 版本、graphics queue、
Surface present 能力（窗口模式）和所需 device extensions 一并作为 suitability 条件。
设备 feature 只能启用已查询为支持的 feature。扩展或 feature 不满足时要把原因
保存在设备诊断信息中。

Instance 创建前必须通过 `negotiate_vulkan_instance_api_version()` 查询 loader
能力：loader 不满足当前 Profile 时直接失败；满足时只请求
`min(loader_api_version, profile_api_version)`。GPU 枚举入口也必须复用这一协商
结果，禁止在不同入口维护固定的 `apiVersion`。

## 4. 线程与句柄规则

- 一个 `gs3d_engine*` 内部持有**唯一渲染线程**（`gs3d::ffi::EngineHost`）：
  所有 `Vk*` 调用都发生在该线程上，内核对象在渲染线程创建、在渲染线程销毁。
- 控制面命令（load/resize/camera/settings/export/pick/snapshot）通过
  `EngineHost::invoke` 入队并在渲染线程执行，调用线程按原同步语义等待结果，
  但自身不接触 Vulkan；命令串行执行，不存在并行提交。
- 帧请求是异步且**可合并**的：渲染中重复 `request_frame` 不会排队堆积。
- 同一 `VkDevice` 的命令池、command buffer、fence、semaphore 和资源所有权必须
  有唯一 C++ owner。跨 owner 传递只能是短生命周期的只读引用。
- C ABI 返回的字符串由库持有，调用方必须立即复制；需要释放的内存只能使用
  配对的 `gs3d_ffi_free_string`。
- `gs3d_engine*` 是不透明句柄，只能由 `gs3d_engine_create*` 创建并由
  `gs3d_engine_destroy` 销毁；任何空句柄或失效句柄都不能触发 Vulkan 调用。

## 5. 帧与数据契约

- 当前跨端稳定交换格式是离屏 `RGBA8`：调用方提供容量，核心返回实际宽高；容量
  不足必须失败且不写越界。
- 两条读取路径：
  - `gs3d_engine_request_frame` + `gs3d_engine_read_frame_rgba`（推荐）：请求在
    渲染线程异步出帧，读取只做一次 memcpy，不触发 Vulkan、不等待渲染；
    `-2` 表示尚无完成帧，`-1` 表示容量不足并回填实际宽高（允许 `out_rgba = NULL`
    做一次纯探测）；`frame_sequence` 用于判断是否有新帧。
  - `gs3d_engine_render_rgba`（兼容）：同步渲染到调用方缓冲，供测试与单发调用。
- 帧资源（命令缓冲、栅栏、staging 缓冲+常驻映射）跨帧复用，禁止每帧
  分配/释放 4 MiB 级资源；staging 缓冲按需扩容，扩容前必须等设备空闲。
- 渲染计时（`gs3d_engine_last_render_ms`）只统计本内核线程的提交+回读耗时，
  与调用方解码/绘制解耦，用于区分“核心成本”与“前端成本”。
- resize 必须拒绝零尺寸；资源重建前等待受影响的 GPU 工作完成。
- shader 路径由调用方传入，未传入时由 ABI façade 按部署布局解析；核心本身不读取
  Flutter 工作目录，也不假定仓库根目录。
- dataset、camera、render settings 和 diagnostics 都由 engine 句柄持有；UI 只读
  取快照并发出命令。diagnostics 额外暴露活动 GPU 名称/类型/UUID/Profile/
  apiVersion/显存与设备挑选原因。

## 6. 返回码和错误处理

沿用 C ABI 统一返回码：`0` 成功，正数只用于明确的业务结果（例如拾取命中），
负数表示失败：`-1` 参数错误、`-2` 目标不存在或状态前置条件缺失、`-3` 内部/
Vulkan 失败、`-4` 状态冲突、`-5` 超时、`-8` 句柄无效。

C++ 层可以使用异常表达构造失败；跨过 C ABI 时必须捕获异常并转换为返回码，
同时更新 `last_error`。错误信息不得依赖平台语言才能判断，诊断文案可以本地化。

## 7. 新平台接入清单

新增平台时只允许新增适配层，不改动 `gs3d_engine_core` 的业务和 Vulkan 资源代码：

1. 提供 Vulkan loader、对应 Profile 的能力和 shader 资源部署；桌面为 1.3，
   Android 离屏路径最低为 1.1。
2. 实现 instance extension 列表与 `VulkanSurfaceProvider`（窗口呈现需要时）。
3. 将路径编码、线程启动和生命周期映射到平台 ABI；不把平台句柄加入公共头文件。
4. 为 `create → load_sample → resize → render_rgba → destroy` 增加最小烟囱测试。
5. 验证 Vulkan 不可用时 Flutter 显示明确错误，且没有 Dart 软件渲染旁路。

## 8. 当前实施顺序

1. ✅ 完成 `gs3d_camera_core`、`gs3d_render`、`gs3d_engine_core` 三层拆分并保持
   `gs3d_ffi` ABI 兼容。
2. ✅ 以 Windows 1.3 和 Android 1.1 的离屏 RGBA 路径为验收基线；
   渲染线程 + 非阻塞帧通道 + 帧资源复用已在 Windows 与 Android arm64 真机
   （三星 Tab S7 / Adreno 650）上验证。
3. ⏭ 再接入窗口 Surface/swapchain 适配；不得为兼容旧 Viewer 把 GLFW 引入离屏目标。
4. 每增加一个前端能力，同时增加 ABI、引擎单测和 Flutter 投影测试。
