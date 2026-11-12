# 架构

## 目标

一个应用，两种语言：C++ 引擎负责数据、渲染与状态，Flutter 只负责界面。
三端（Windows / Linux / Android）本地离线运行，都能打开千万级点云，
都能自己完成预处理。工程包三端通用，可直接拷贝。

## 目录

```
engine/
  foundation/      无业务依赖的地基
    diagnostics/     Log, CpuInfo, Stopwatch, FrameClock, Percentile
    io/              FileIO, MemoryMappedFile, Base64, ResourcePath
    concurrency/     ThreadPool, Cancellation, TaskQueue
    math/            Camera 数学, Ray, BoxSelect 几何, Bounds
  data/            只读的数据理解，不碰 GPU
    csv/             Sniffer, Delimiter, Schema, 分块读取
    format/          Gs3dFormat, ByteOrder, Reader, Dataset, Exporter, Verifier
    lod/             LodFormat, LodReader, LodDataset
    tile/            TileFormat, TileReader, 索引与网格
  pipeline/        写数据：原始文件 → 工程包
    stages/         阶段机、进度、恢复、原子提交
    compute/        统计、CSV→点、体素降采样
    writers/        点块、LOD 块、瓦片块写入器
  render/          Vulkan，只认 GPU，不认窗口
    device/         Context, Buffer, DepthBuffer, GpuInfo, FrameTimer
    scene/          Viewport, Camera, Selection, LodSelector, NearestPoint
    point/          PointPipeline, PointCloud{Gpu,Lod,Tile}, AxisGrid
    targets/        OffscreenFramebuffer, PresentSurface
    streaming/      TileDeviceArena, TilePointCache, TileLoadCommit, FrameChannel
  session/         一次运行的业务状态
    project/        Bundle 打开/关闭、最近项目、任务表
    measure/        MeasurementManager、结果存取
    view/           Viewport{Interaction,Presentation}State、预算
  config/          AppConfig、命令行、校验、viewer.toml
  abi/             geoscatter3d.h、EngineApi、EngineHost
  cli/             preprocess、verify（无 GPU 也能跑）
app/               Flutter
  lib/ffi/         绑定、结构体镜像、中枢（GeoScatter3dService）
  lib/state/       快照模型（不可变）
  lib/viewport/    视口画布、手势、纹理通路
  lib/panels/      数据集 / 渲染设置 / 测量 / 导出 / 最近项目
  lib/theme/       设计令牌、配色
  lib/widgets/     通用组件
  android/ windows/ linux/
platform/          三端原生适配（不属于引擎，也不属于 Dart）
  android/         Surface/JNI、后台任务、存储权限
  windows/         窗口句柄、文件对话框、安装包钩子
  linux/           同上
packaging/         cmake 模块、安装包、签名
assets/  config/  examples/  scripts/  tests/  docs/
```

依赖方向见 [RULES.md](RULES.md) 规则一。`platform/` 只向下看 `engine/abi`。

## 线程模型

| 线程 | 拥有 | 不许做 |
| --- | --- | --- |
| 主线程（Flutter UI） | Dart 快照、界面 | 等待引擎、读文件、提交 GPU |
| 渲染线程 | `VkDevice`、队列、视口资源 | 回调 Dart、阻塞在 IO |
| 分发线程 | 唤醒通知（原子掩码 → 回调） | 访问 GPU、加锁渲染 |
| 工作池（N = 核数） | 分块读、解码、预处理 | 提交 GPU、改引擎状态 |

- 引擎对外的调用要么立即返回，要么返回一个任务号；没有"调用了就等它做完"的接口。
- 长任务（预处理、LOD 构建）可取消；取消点按阶段划分。
- 4 GB Android 设备上工作池默认为 `min(4, 核数)`，且同时只有一个重任务。

## ABI 契约

头文件：`engine/abi/geoscatter3d.h`，纯 C，`extern "C"`。规则二（只增不改）优先于
这里的任何写法示例。

```c
typedef struct gs3d_engine gs3d_engine;

/* 所有结构体首部一致：接收方先查 struct_size，未知版本拒绝执行 */
typedef struct { uint32_t struct_size; uint32_t abi_version; } gs3d_header;

typedef struct {
    uint32_t mask;              /* 只有掩码，不带数据 */
} gs3d_wake;

enum { GS3D_WAKE_FRAME = 1u << 0, GS3D_WAKE_TASK = 1u << 1,
       GS3D_WAKE_ERROR = 1u << 2, GS3D_WAKE_PROGRESS = 1u << 3 };

/* 唯一的读取口：一次拿齐一帧所需的全部数字 */
int32_t gs3d_engine_poll_state(gs3d_engine*, gs3d_state* out);

/* 唯一的写入口：命令是 POD，字符串只在调用期间有效 */
int32_t gs3d_engine_submit(gs3d_engine*, const gs3d_command* cmd);

/* 唤醒回调：只允许传 mask，不许传指针；由分发线程调用 */
int32_t gs3d_engine_set_wake(gs3d_engine*,
                             void (*on_wake)(void* user, gs3d_wake),
                             void* user);
```

- 像素：桌面走 `gs3d_engine_read_frame_rgba`（一次 memcpy，调用方给缓冲）；
  Android 走 `gs3d_engine_attach_surface`，引擎直接 present 到 Flutter 纹理，
  不产生像素回读。
- 错误：所有 `int32_t` 返回 0 为成功、负数为错误码；`gs3d_engine_last_error`
  取文本。`poll_state` 里的错误码与文本用于界面显示。
- 回调约定：`on_wake` 可能被高频调用，必须立即返回；它只表示"有变化，来拉一次"。

## 工程包格式 v3

```
<bundle>/
  manifest.json         版本、原点、包围盒、点数、属性表、块表、各阶段状态
  points/000000.chunk   点块，定长，小端，mmap 直读，不压缩
  lod/level-000.chunk   预计算层级
  tiles/index.bin       网格尺寸 + 每格偏移/点数
  tiles/000000.chunk
  analysis.json         测量结果
```

不变量：

1. 点存相对于 `manifest.origin` 的坐标；对外一律给绝对坐标，只有一个换算入口。
2. 块写入后不可变；重写产生新块号，旧块在提交时删除。
3. `manifest.json` 最后写，且用"临时文件 + rename"原子替换。manifest 存在且校验
   通过 = 包完整；否则视为未完成，可继续预处理。
4. 每阶段产出自己的文件并在 manifest 里标记完成；恢复从最后一个完成阶段开始。
5. 未知块类型必须被忽略而不是报错，这样新版包可以被旧程序打开。

## 性能预算

50M 点、37 km × 40 km、高程 −190…282 m 为基准数据集。

| 指标 | Windows / Linux | Android（4 GB） |
| --- | --- | --- |
| 打开到可交互 | ≤ 1.5 s | ≤ 4 s |
| 拖动 / 旋转帧时间 | ≤ 16 ms | ≤ 33 ms |
| 预处理 50M 点 | ≤ 60 s（16 核） | ≤ 4 min |
| 峰值内存 | ≤ 2 GB | ≤ 1.2 GB |
| 桌面回读额外延迟 | ≤ 3 ms | 不适用 |

预处理必须随核数近似线性（8 核 ≤ 180 s）。达不到时优先降像素比或 LOD 预算，
不允许提高内存上限。

## 平台差异

| 能力 | Windows | Linux | Android |
| --- | --- | --- | --- |
| 图形 | Vulkan 1.1+ | Vulkan 1.1+ | Vulkan 1.0+ 驱动 |
| 视口呈现 | RGBA 回读 | RGBA 回读 | 原生纹理零拷贝 |
| 后台预处理 | 进程常驻 | 进程常驻 | 前台服务 + 阶段恢复 |
| 文件选择 | 原生对话框 | 原生对话框 | SAF / 应用私有目录 |
| 大文件读取 | mmap | mmap | pread（部分设备 mmap 受限） |

三端都必须能在启动时检测 Vulkan 并给出可读的失败原因，不许静默退出。

## 迁移映射

当前树 → 目标树（`git mv` 保留历史）：

| 现在 | 以后 |
| --- | --- |
| `src/data/Csv*.cpp`, `Delimiter`, `DataSchema` | `engine/data/csv/` |
| `src/data/Gs3d{Format,Reader,Dataset,Exporter,Verifier,ByteOrder}.cpp` | `engine/data/format/` |
| `src/data/Gs3dLod*.cpp` | `engine/data/lod/` |
| `src/data/Gs3dTile*.cpp` | `engine/data/tile/` |
| `src/preprocess/*` | `engine/pipeline/{stages,compute,writers}/` |
| `src/render/*` | `engine/render/{device,scene,point,targets,streaming}/` |
| `src/camera/*` | `engine/render/scene/`（纯数学部分进 `foundation/math/`） |
| `src/app/{config/*, session/*}` | `engine/config/`, `engine/session/project/` |
| `src/app/systems/MeasurementManager.cpp` | `engine/session/measure/` |
| `src/app/state/*`, `src/ui/{layouts,color}/*` | `engine/session/view/`（工作区布局第一版不做） |
| `src/ffi/{EngineHost,FrameChannel,Gs3dEngineApi}` | `engine/abi/` |
| `src/ffi/HeadlessRenderKernel.cpp`（2,036 行） | 按职责拆进 `engine/session/` 与 `engine/render/` |
| `src/ffi/LodBuilder.cpp` | `engine/pipeline/compute/` |
| `src/platform/{CpuInfo,MemoryMappedFile}` | `engine/foundation/{diagnostics,io}/` |
| `src/ffi/AndroidSurfaceJni.cpp`, `platform/Window.cpp` | `platform/android/`, `platform/windows/` |
| `ui_flutter/` | `app/` |
| `src/control/*`（2,554 行） | 删除 |
| `src/main.cpp`, `src/app/ViewerApp*.cpp`, `src/render/Vulkan{Renderer,Swapchain}.cpp`, `src/ui/{WorkspaceManager,color,layouts}`, `include/app/Viewer*.hpp` | 删除（35 个未编译 `.cpp`，约 9,744 行） |

迁移顺序，每一步单独提交且仓库必须可构建：

1. 删死代码与旧 ABI，保留一个可运行的引擎。
2. 目录搬迁，只改路径，不改逻辑。
3. 格式 v3：写入器、读取器、迁移工具、删除 v1/v2。
4. ABI v1：结构体 + 推送 + 拉取，Dart 绑定重写。
5. 前端：视口 → 数据集面板 → 渲染设置 → 测量 → 导出 → 最近项目。
6. CI 三矩阵 + lavapipe 渲染冒烟。

## 未决

- 桌面是否需要零拷贝纹理：等回读实测超过 3 ms 再决定。
- lavapipe 在本仓库 CI 上的可用性未验证（Linux 需要 `mesa-vulkan-drivers`）。
- Android 后台预处理的存活策略（前台服务类型、电量与存储配额）未验证。
- 60 s 预处理目标与当前实现的差距未测。
