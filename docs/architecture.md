# GeoScatter3D 架构与现状

## 数据流

```text
viewer.toml / CLI
        |
        v
AppConfigLoader
        |
        +-- input=csv --> Statistics/CsvChunkReader --> Gs3dWriter
        |                                      \----> Gs3dTileWriter
        |
        v
Gs3dDataset metadata/full points + optional LOD/tile sidecars
        |
        v
PointCloudGpu / PointCloudLodGpu / PointCloudTileGpu
        |
        v
PointPipeline --> OffscreenFramebuffer[N] --> ImGui::Image[N]
```

## 主要模块

| Module | Interface | Implementation 与职责 |
|---|---|---|
| `app` | `AppConfig`, `AppState`, `UiActions`, `ViewerApp` | 解析配置、保存 UI 状态、编排生命周期、resize 防抖和 tile LRU 缓存 |
| `data` | CSV 与 GS3D/LOD/tile reader/format | 文件格式校验、索引查询、点数据读取 |
| `preprocess` | converter/writer | 并行统计、CSV 转换、LOD 和 tile 生成 |
| `render` | Vulkan context/buffer/pipeline/cloud/viewport | Vulkan 资源所有权与命令录制 |
| `camera` | `Camera`, `CameraInput`, `CameraController`, `CameraHub` | 相机数学、视图局部输入映射和可选同步组传播 |
| `gui`/`ui` | `ImGuiLayer`, `UiRoot` | ImGui 生命周期、Adobe 风格 docking 布局和 UI 命令生成 |
| `platform` | `Window` | GLFW 初始化、主窗口和键盘/事件处理 |

## 启动流程

1. `main()` 读取 TOML 和命令行覆盖项。
2. CSV 模式先生成 GS3D；启用 tile 时同时生成 tile index/data。
3. LOD sidecar 可用时只加载 GS3D header；需要构建 LOD 或全量渲染时才加载点数组。
4. 创建窗口、Vulkan context、swapchain、renderer 和 ImGui。
5. 预分配最多 4 个独立离屏视口，默认显示配置指定的 1 个视图，并创建点渲染
   管线、相机控制器和 GPU 点云。
6. 进入逐帧循环。

## 逐帧流程

1. 轮询窗口事件并更新帧率。
2. 从真实运行状态生成 `AppState`，由 `UiRoot` 返回包含各视图输入快照的
   `UiActions`。
3. `CameraController` 使用 ImGui 视图画布上的局部输入更新目标相机；相机默认独立，
   只有勾选“联动相机”的视图才由 `CameraHub` 传播变化。
4. 相机稳定后触发 tile 选择；后台 future 完成后按
   `tile.gpu_upload_budget_bytes` 分帧批量上传 GPU。
5. 在 swapchain render pass 前仅渲染激活标签页和实际可见的独立平台窗口。
6. 每个视图由独立 ImGui 窗口采样，可作为中央标签页或拖成跨屏平台窗口；输入不再
   依赖 GLFW 主窗口鼠标坐标。
7. resize 请求稳定 150 ms 后批量处理，一批只执行一次 GPU 空闲同步。

## GPU Pick Contract

- 生产 hover 与 box-select anchor 现在走 GPU pick，而不是 CPU
  `NearestPointQuery`。
- 每个渲染点会写出一个 `point_id` 到离屏 pick attachment。这个
  `point_id` 只保证**单次运行内稳定**，用于本次进程中的 hover/框选反查；
  它不是跨运行、跨文件的持久 ID，不能拿去做选中结果存盘或长期引用。
- pick 规则是**纯 front-most**：围绕光标回读 `5 x 5` 邻域，只看命中的像素，
  在这些像素里选择深度最小的那个点。
- 这个规则不会先按屏幕距离挑像素，所以当前后点在邻域内并排出现时，会优先选中
  更靠前的已渲染点，而不是更靠后的“屏幕更近像素”。
- `NearestPointQuery` 仍保留，语义依旧是“屏幕空间 2D 最近且带阈值”；它现在主要
  用于历史测试/对照，不再代表生产 hover 契约。

## 已完成的性能优化

- 3302 万点样例在 LOD sidecar 模式下，GS3D 读取由约 `0.57 s` 降至
  `0.00003 s`，并避免约 `504 MiB` 完整点数组常驻。
- tile CPU 缓存受 `tile.cpu_cache_max_bytes` 限制，使用 LRU 淘汰并显示真实命中率。
- tile future 直接持有本次上传数据，缓存淘汰不会破坏正在进行的 GPU 上传。
- tile GPU buffer 先批量准备，再在一个命令缓冲中复制；上传按帧预算切片，避免每个
  tile 单独 `vkQueueWaitIdle` 和一次性百毫秒级主线程停顿。
- 交互期间暂停高分辨率 tile 上传并使用最低 LOD；相机稳定后再恢复 tile 选择和
  渐进上传。
- 视口 resize 从“每次尺寸变化执行一次 `vkDeviceWaitIdle`”改为防抖后批量重建。
- 隐藏视图和未激活标签页不再执行离屏点云渲染。
- 各视图保留自己的 tile 选择集合；移动一个独立窗口不会清空其他视图的高分辨率
  结果。
- 多视图状态一次分配后原地更新，移除了逐帧 clear/push 和颜色选项临时数组。
- LOD 只在相机真正移动或缩放时进入交互态，消除了静止状态的层级抖动。

在 3302 万点样例和同一台测试机器上，旧版默认 4 视图高 LOD 空闲时 GPU 持续
100%；默认单视图后约为 35%-38%。高分辨率 tile 已驻留时，连续拖动由 100%
降至约 40%-51%。一次约 234 MiB、110 ms 的集中上传被拆为受 8 MiB 预算约束的
多帧切片，除首片外多数约 3-5 ms。以上数字用于说明本机前后差异，不是跨设备指标。

## 资源所有权

- `VulkanContext` 比所有 Vulkan 子资源活得更久。
- `VulkanRenderer` 拥有 swapchain framebuffer、同步对象和命令缓冲。
- `ViewportManager` 拥有每个视口的 `Camera + OffscreenFramebuffer`。
- `OffscreenFramebuffer` 拥有颜色图、深度图、render pass、framebuffer 和
  ImGui texture descriptor。
- `PointCloud*Gpu` 拥有点数据对应的 Vulkan buffer。
- `ImGuiLayer` 在 `ViewportManager` 之后析构，保证 descriptor 先注销、ImGui
  backend 后关闭。

## 尚未完成

- 打开、保存、添加数据、移除、属性、截图和首选项动作尚未由应用层处理。
- 框选和测量尚未接线。
- 当前 UI 只提供单个“联动相机”组，尚不能创建和命名多个同步组。
- 色带、裁剪、光照、截图等高级可视化功能尚未实现，界面中也不再展示无效控件。
- 没有数据集热切换，打开新文件仍需重启。

## 重大风险

1. `ViewerApp.cpp` 当前有 2569 行，其中 `ViewerApp::run()` 独占 2204 行；它仍同时
   负责数据加载、缓存、GPU 上传、输入、UI 映射和渲染。逐视口旋转手势历史已提取为
   可单测的 `ViewportInteractionState`，但主循环的其余职责边界仍不清晰，修改任何功能
   都容易影响主循环。
2. 新写入的 GS3D v2 使用固定小端、显式 IEEE-754 字段编码，且允许 `header_size`
   大于已知最小头部以保持前向读取兼容。读取端仍保留 GS3D v1 的原生布局兼容路径；
   已有 v1 数据应重建为 v2，LOD/tile sidecar 也需要独立评估相同的可移植性问题。
3. swapchain 重建假设颜色格式和 image count 不变。显示模式或 surface 能力变化时，
   ImGui pipeline/render pass 以及 image-count 配置可能失配。
4. resize 已防抖并批量同步，但批次仍使用 `vkDeviceWaitIdle`。进一步优化应改为按
   frame fence 延迟回收旧 framebuffer，彻底消除设备级停顿。
5. 自动化测试覆盖 GS3D 格式、元数据加载、resize 调度、LRU/帧上传预算和视图局部
   相机输入，并由 Linux/Windows 构建工作流执行；仍缺少 CSV、LOD、tile 选择和 Vulkan
   生命周期集成测试。
6. 仓库跟踪约 500 MiB 的 `data/test.gs3d`，Git 对象目录接近 900 MiB。大型样例
   应迁移到 release artifact、Git LFS 或可重复生成的小型 fixture。

## 后续拆分方向

1. **Tile 运行时服务**：从 `ViewerApp` 提取 tile 后台任务、CPU 缓存和 GPU 同步。
   对外只提供相机更新和可渲染结果，让异步状态机可以独立测试。
2. **逐帧编排器**：集中生成 `AppState`、应用 `UiActions` 和路由输入，避免主循环
   逐项复制 UI 字段。
3. **稳定的文件格式层**：用明确的小端字段编码替代 C++ struct 原样写盘，把版本、
   校验和兼容策略集中管理。
4. **Swapchain 变更接口**：明确通知 ImGui 和依赖 render pass 的管线重建，而不是
   依赖当前隐含的初始化顺序。
