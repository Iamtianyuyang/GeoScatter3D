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
    io/              FileIO, MemoryMappedFile, Crc32c, Base64, ResourcePath
    concurrency/     ThreadPool, Cancellation, TaskQueue
    math/            Camera 数学, Ray, BoxSelect 几何, Bounds
  data/            只读的数据理解，不碰 GPU
    csv/             Sniffer, Delimiter, Schema, 分块读取
    format/          Gs3dFormat, ByteOrder, Reader, Dataset, Exporter, Verifier
    lod/             LodFormat, LodReader, LodDataset
    tile/            TileFormat, TileReader, 索引与网格
  pipeline/        写数据：原始文件 → 工程包（无 Vulkan 依赖）
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
  abi/             geoscatter3d.h、EngineApi、PipelineApi、EngineHost
  cli/             preprocess、verify（无 GPU 也能跑）
app/               Flutter
  lib/ffi/         绑定、结构体镜像、中枢（GeoScatter3dService）
  lib/state/       快照模型（不可变）
  lib/viewport/    视口画布、手势、纹理通路
  lib/panels/      数据集 / 渲染设置 / 测量 / 导出 / 最近项目
  lib/theme/       设计令牌、配色
  lib/widgets/     通用组件
  android/ windows/ linux/
platform/          三端原生工具（只被 app/ 链接）
  android/         Surface/JNI、后台服务、SAF
  windows/         窗口句柄、文件对话框、安装包钩子
  linux/           同上
packaging/         cmake 模块、安装包、签名
assets/  config/  examples/  scripts/  tests/  docs/
```

依赖方向见 [RULES.md](RULES.md) 规则一。`platform/` 位于最上层：它调用 `engine/abi`，
引擎从不 include、也不链接 `platform/`。

## 线程模型

| 线程 | 拥有 | 不许做 |
| --- | --- | --- |
| 主线程（Flutter UI） | Dart 快照、界面 | 等待长任务、读文件、提交 GPU |
| 渲染线程 | `VkDevice`、队列、视口资源 | 回调 Dart、阻塞在 IO |
| 分发线程 | 唤醒通知（原子掩码 → 回调） | 访问 GPU、加锁渲染 |
| 工作池（N = 核数） | 分块读、解码、预处理 | 提交 GPU、改引擎状态 |

- 对外的调用要么立即返回，要么返回任务号；没有"调用了就等它做完"的接口。
  `EngineHost::invoke()` 这种阻塞形态在迁移第 4 步取消。
- 长任务（开包、预处理、LOD、导出）可取消；取消点是阶段边界。
- 只有渲染线程发布快照；只有主线程读取快照。见下文「状态快照」。
- 4 GB Android 设备上工作池为 `min(4, 核数)`，且同时只允许一个重任务。

## ABI 契约

头文件：`engine/abi/geoscatter3d.h`。规则二（只增不改）优先于本文任何写法示例。

### 通则

- 所有结构体第一个字段是 `uint32_t struct_size`；接收方先查大小，过小直接拒绝。
- ABI 自身的版本由 `uint32_t gs3d_engine_abi_version(void)` 给出，不塞进每个结构体。
- 字符串与数组指针**只在调用期间有效**。异步命令的参数由引擎深拷贝；引擎绝不
  保存调用方指针。
- 错误：`int32_t` 返回 0 成功、负数错误码；引擎任务的错误文本放在已发布的
  快照里（定长 `char[256]`），管线任务放在其状态中；不返回裸指针。
- 任何一次 ABI 改动同时改 `app/lib/ffi/` 的镜像与一个布局测试。

### 状态快照

只有一个写者（渲染线程）、一个读者（主线程）。做法与 `FrameChannel` 相同：

```c
typedef struct {
    uint32_t struct_size;
    uint64_t generation;      /* 每次发布 +1 */
    /* …相机、点数、预算、任务表、表面操作状态、last_error[256]… */
} gs3d_state;

/* 拷贝最近一次已发布的完整一代；没有发布过则返回 -2 */
int32_t gs3d_engine_poll_state(gs3d_engine*, gs3d_state* out);
```

引擎用短时间持有的互斥锁保护已发布的 `gs3d_state`：渲染线程先在本地构造快照，
只在复制发布时持锁；`poll_state` 持同一把锁复制快照。锁内不做 IO、GPU 操作或
回调，禁止半写半读。不得仅靠双缓冲和序号重试：写者再次复用缓冲时仍可能与读者并发访问。

任务表也在这份快照里（固定上限 4 项）：`{id, stage, state, progress}`。任务结束时
状态保留到被读取一次为止，防止 Dart 错过终态。任务表已满时必须明确返回错误；
宿主须保留表面句柄，并在能提交 detach 后等待该任务完成。

**当前实现**：`include/ffi/geoscatter3d_ffi.h` 第 21 节与 `ffi/TaskBoard`（
`TaskBoard::Snapshot` 由 `EngineHost::poll_state` 复制）。`gs3d_engine_open_dataset`
先登记任务再投递到单个 IO 线程；该线程只调 `HeadlessRenderKernel::load_dataset_from_disk`
（纯读盘），完成后把结果投回渲染线程调 `install_dataset`。限制：

- 只读一次终态：任何漏读都会独占一个槽位，直到进程退出；Dart 侧每 200 ms 轮询。
- 取消只保证结果不被装载，文件读取本身不可中断；`gs3d_engine_destroy` 会等 IO 线程
  读完当前文件后返回。
- 只有一个 IO 线程，只服务开包类重任务；相机、点大小、配色、背景、视口、出帧和
  卸载走 `gs3d_engine_submit` 入队（调用线程校验参数、渲染线程执行，不等待），
  参数非法返回 -1、引擎未启动返回 -2；执行失败通过快照 error 发布（-1 只表示已入队）。
  `tests/EngineHostTests.cpp` 的 “async open reports a snapshot before and after
  loading” 在有 Vulkan 设备的机器上真实跑通：建 64 点工程包、提交、轮询到 Done、
  校验点数与终态一次性释放，并验证取消后装载被丢弃。
- 界面已接线：`GeoScatter3dService.openBundle` 调用 `loadDatasetAsync`（每 200 ms
  轮询快照），“数据”抽屉里的「打开工程包」通过平台目录选择器选包并显示阶段；
  基准/样例路径仍用同步 `gs3d_engine_load_dataset`。二者共用同一内核装载代码。
- 导出（“成果”抽屉）：`gs3d_engine_export` 写 PLY/CSV、`gs3d_engine_read_frame_png`
  取当前帧 PNG，文件落在应用状态目录的 `exports/`，界面只展示路径。导出仍是同步
  调用（占渲染线程），因此超过 200 万点直接拒绝并说明原因，另存为对话框也尚未做。
- 测量（“属性”抽屉）：视口单击调用 `gs3d_engine_pick_point`，依次记录起点和终点，
  显示两点距离与带符号高差；第三次点击开始新的一组，未命中不改变现有测量。
  拾取在渲染线程上线性扫描已装载的点（`src/ffi/HeadlessRenderKernel.cpp` 的
  `pick_point`），样例数据即时，50M 数据集一次点击要数秒，尚未接 GPU id 缓冲。
- 显示设置（“显示”抽屉）：配色索引与点大小先改本地状态，拖动结束才通过
  `submitColormap`/`submitPointSize` 入队，落盘到平台应用状态目录的
  `render-settings.json`（临时文件 + 改名）；读回失败回到默认值，不影响启动。
  设置只影响本机显示，工程包内容不变。

### 命令

```c
typedef struct {
    uint32_t struct_size;
    uint32_t op;
    uint32_t flags;           /* GS3D_CMD_ASYNC；0 = 普通入队命令 */
    union { /* 每类命令一个 POD 参数块，字符串为 const char* */ } args;
} gs3d_command;

int32_t gs3d_engine_submit(gs3d_engine*, const gs3d_command*,
                           uint64_t* out_task);   /* 可为 NULL */
int32_t gs3d_engine_cancel(gs3d_engine*, uint64_t task);
```

普通命令包括相机输入、点大小、配色、背景、请求一帧；返回成功表示已校验并入队，
不表示渲染线程已执行。调用线程只做参数校验与深拷贝，不等待执行。
开包、卸载、预处理、LOD 重建、导出和截图写盘使用异步任务号跟踪执行与终态。

### 唤醒

```c
enum { GS3D_WAKE_FRAME = 1u << 0, GS3D_WAKE_TASK = 1u << 1,
       GS3D_WAKE_ERROR = 1u << 2, GS3D_WAKE_PROGRESS = 1u << 3 };

int32_t gs3d_engine_set_wake(gs3d_engine*,
                             void (*on_wake)(void* user, uint32_t mask),
                             void* user);
```

只传掩码。`on_wake` 由分发线程调用，可能高频触发，必须立即返回；收到后 Dart
下一次 `poll_state` 拿全量。

### 像素与表面

```c
int32_t gs3d_engine_read_frame_rgba(gs3d_engine*, uint8_t* out, uint32_t cap,
                                    uint32_t* w, uint32_t* h, uint32_t* stride);

typedef struct { uint32_t struct_size; void* handle; uint32_t width, height, kind; }
    gs3d_native_surface;
int32_t gs3d_engine_attach_surface(gs3d_engine*, const gs3d_native_surface*,
                                    uint64_t* out_task);
/* 请求释放；对应任务完成前宿主须保留句柄 */
int32_t gs3d_engine_detach_surface(gs3d_engine*, uint64_t* out_task);
```

- 表面由宿主（`app/android`）创建与销毁；attach 只登记，不接管所有权。
- 宿主销毁前请求 `detach_surface`；引擎完成 GPU 资源释放并确认后，宿主才能释放句柄。
  实现须保证句柄引用覆盖异步处理期间，并处理旋转、锁屏和退出。
- attach/detach 入队后立即返回任务号；快照中的任务终态标识具体操作是否完成或失败。
  引擎在渲染线程执行操作，等待 GPU 的操作不能阻塞 Flutter UI 线程。

### JSON 通道

配置文件、对话框、诊断这类低频结构化数据走 JSON，不塞进结构体：

```c
/* 返回 -3 表示 cap 不足，out_len 给出所需字节数；成功时 out 内含完整应答 */
int32_t gs3d_engine_request(gs3d_engine*, const char* request_json,
                            char* out, uint32_t cap, uint32_t* out_len);
```

请求与应答都带 `"v": 1`；字段只增不改，未知字段忽略。同步请求仅处理内存中的
短操作；涉及文件或其他长操作时，返回任务号并通过快照取得结果。热点路径禁止走这里。

### 管线 ABI（无 GPU）

预处理不能依赖渲染线程、窗口或 `VkDevice`，否则 Android 后台服务无法复用：

```c
typedef struct gs3d_pipeline gs3d_pipeline;
gs3d_pipeline* gs3d_pipeline_create(const gs3d_pipeline_config*);
int32_t gs3d_pipeline_start(gs3d_pipeline*, const gs3d_pipeline_job*, uint64_t* task);
int32_t gs3d_pipeline_poll(gs3d_pipeline*, gs3d_pipeline_status*);
int32_t gs3d_pipeline_cancel(gs3d_pipeline*, uint64_t task);
/* 进程被杀后重新接管一个未完成的包：指纹不一致就返回错误 */
int32_t gs3d_pipeline_resume(gs3d_pipeline*, const char* bundle_dir, uint64_t* task);
void    gs3d_pipeline_destroy(gs3d_pipeline*);
```

Android 前台服务通过 JNI 直接调这一组，不经过 `EngineHost`，不需要 Flutter 引擎存活。

## 工程包格式 v3

```
<bundle>/
  manifest.json         版本、CRS、原点、属性表、块表、任务与阶段状态
  points/000000.chunk   点块，定长，小端，mmap 直读，不压缩
  lod/level-000.chunk   预计算层级
  tiles/index.bin       网格尺寸 + 每格偏移/点数
  tiles/000000.chunk
  analysis.json         测量结果
```

不存"全分辨率单体文件"：点只存在于 chunk 中，打开时只读 manifest 与块表，
按需读块。整文件加载会占用较大的内存预算，是否超限须以目标设备实测为准。

manifest 必须包含：

```json
{
  "format_version": 3,
  "min_reader_version": 3,
  "required_features": ["tiles"],
  "crs": { "authority": "EPSG", "code": 32650, "axis_order": "east,north,up",
           "unit": "m", "vertical_unit": "m", "datum": "WGS84" },
  "origin": [28165200.0, 57031700.0, 3243.5],
  "bounds_min": [-18688700.0, -20026100.0, -400.5],
  "bounds_max": [18688700.0, 20026100.0, 400.5],
  "point_count": 50000000,
  "stride": 16,
  "attributes": [ { "name": "elevation", "type": "f32", "unit": "m",
                    "has_null": true, "null_value": -9999 } ],
  "chunks": [ { "path": "points/000000.chunk", "points": 65536,
                "bytes": 1048608, "crc32c": "1a2b3c4d" } ],
  "job": { "id": "uuid", "source_path": "x.dat", "source_bytes": 3350000067,
           "source_sha256": "…", "options_hash": "…",
           "stages": { "stats": "done", "points": "done" } }
}
```

字段名以 `engine/data/format/Manifest.hpp` 为准，本文与代码不一致时按代码修文档。

不变量：

1. 点存相对 `origin` 的坐标（原点用双精度）；对外只给绝对坐标，只有一个换算入口。
2. 块写入后不可变；重写产生新块号，旧块在提交时删除。
3. 块表的 `bytes` 是完整文件长度；点块包含 32 字节头，`crc32c` 是点数据载荷的
   校验和，与块头中的值一致。读取先验证文件长度，再映射并校验载荷，校验成功后
   才能将数据交给调用方。
4. 每个阶段完成后持久化块和阶段检查点；最终 manifest 只在全部必需阶段完成后发布。

   写块和临时元数据后先同步文件，再用同一文件系统内的原子替换发布并同步目录；
   各平台分别实现并验证等效的持久化语义。最终 manifest 校验通过才视为包完整。
5. 恢复只从最后一个完成阶段开始：`job.id` + 源文件指纹 + `options_hash` 三者都
   一致才允许续跑；不一致就拒绝并提示从头开始，绝不混合两次预处理的数据。
6. 兼容：`min_reader_version` 高于自身 → 拒绝打开；`required_features` 有未知项 →
   拒绝；块表中未知的**可选**块类型忽略，未知的必需块类型拒绝。
7. CRS 或 `axis_order` 不一致的两个包不允许在同一个会话里叠加测量，界面必须提示。

实现状态（2026-11）：

- 已实现：`engine/data/format/Manifest.*`（读写 + 原子发布）、`PointStage`（写块、
  校验和、阶段检查点）、`BundleBuild`（按源文件 SHA-256 与 options_hash 判定可否
  续跑，指纹不一致时拒绝，跳过已完成点阶段前校验块；支持阶段边界回调和从包目录
  恢复源路径、选项）、`BundleJob`（单任务后台执行、状态轮询和阶段边界取消）、
  `gs3d_pipeline_ffi`（独立的无 GPU 管线 C 接口及 Dart 结构镜像）、
  `gs3d_pack`（命令行入口）、`V3BundleReader`（引擎直接打开 v3 包）。
- 未实现：`lod/` 与 `tiles/` 的 v3 写入与读取；在它们落地前，manifest 的
  `required_features` 只应写 `points`。约定：`required_features` 里的特性在包里
  找不到对应块时，读取方必须拒绝加载。
- 阶段检查点目前写在 `manifest.json` 中；指纹完成后记录 UTF-8 绝对源路径与选项，
  因此分享工程包时会暴露源路径；移动或删除源文件后不能自动恢复。未完成点阶段的
  检查点不能作为完整包读取。指纹阶段中断时尚无检查点，需重新提交任务。
- 未实现：块级续跑（中断的块重写），按决议只做阶段级续跑；独立检查点与最终
  manifest 分离和任务自动调度仍待实现。Android 前台服务在进程存活时执行管线并持有
  唤醒锁；进程被系统回收后，Flutter 从应用私有状态目录读取最近输入和工程包路径，
  由用户手动恢复，记录中包含本地源路径。管线动态库中的 `BundleJob` 实例共享单重任务
  限制；其他静态链接副本及独立进程不受此限制。Android 统计阶段的工作线程最多为 4；
  管线 C 接口目前暂存于
  统一 ABI 头 `include/ffi/geoscatter3d_ffi.h` §23（`gs3d_pipeline_ffi` 动态库），销毁管线实例会等待
  工作线程，调用方须避开 UI 线程。
- Android 服务实施约定：用户在前台发起任务时启动 `dataSync` 前台服务并展示通知；
  服务持有进程内原生管线，Flutter 页面销毁不取消任务。系统回收进程后不假定任务仍在
  运行，用户通过保存的目录从已提交阶段恢复。Android 15 对 `dataSync` 前台服务有累计
  时限，超时应请求阶段边界取消并停止服务；不得在后台擅自重启前台服务。
- 实测（Ryzen 9 7945HX / clang / 单流）：682 MB CSV → 33,021,621 点、504 块、
  72.1 s、峰值 RSS 514 MB、包 506 MB。按点数折算 50M 约 110 s，未达 60 s 预算，
  属性能任务待优化（当前主要成本是两遍扫描、逐块 fsync 与 SHA-256）。后续在相同
  硬件以 GCC Release 重测该 CSV 为 79.0 s、峰值 RSS 514 MiB；当时有其他构建任务，
  不能直接与先前结果比较。实测 `data/field_and_ele_source_rec_50m.dat`（3.35 GB，
  50,000,000 点）：`tmp/build-linux/src/gs3d_pack --csv <输入> --out <新目录>`，
  763 块、239.6 s、峰值 RSS 517 MiB；统计阶段 32 线程，Android 构建同时在后台下载。
  此后复用分块解析器并按输入顺序写点块（Android 最多 4 个解析线程，桌面最多 8 个）；
  3300 万点样本的本机阶段计时从 70.1 s 降至 44.3 s，均有后台活动，不是严格对照。
  同一 50M DAT 优化后重测 129.4 s、峰值 RSS 609 MiB：指纹 90.5 s、统计 5.2 s、
  点块 33.7 s。Linux 构建如检测到 OpenSSL 3 则使用其 SHA-256 实现加速指纹，Flutter
  安装包附带所需 libcrypto 和许可证；未检测到时使用内置实现。启用 OpenSSL 后同一机器和数据一次实测
  54.5 s、峰值 RSS 626 MiB（指纹 15.6 s、统计 5.0 s、点块 33.8 s），文件系统缓存已热，
  达到本机 60 s 预算。冷缓存、Windows 和 Android 设备性能尚未测量；Windows 和 Android
  目前使用内置指纹实现。


## 性能预算

50M 点、37 km × 40 km、高程 −190…282 m 为基准数据集。

| 指标 | Windows / Linux | Android（4 GB） |
| --- | --- | --- |
| 打开到可交互 | ≤ 1.5 s | ≤ 4 s |
| 拖动 / 旋转帧时间 | ≤ 16 ms | ≤ 33 ms |
| 预处理 50M 点 | ≤ 60 s（16 核） | ≤ 4 min |
| 峰值内存 | ≤ 2 GB | ≤ 1.2 GB |
| 桌面回读额外延迟 | ≤ 3 ms | 不适用 |

测量门禁：`engine/cli/gs3d_bench`（从 `tools/gs3d_bench_bundle.cpp` 移植）无界面
输出四个数字：开包毫秒、预处理各阶段秒数、一帧渲染毫秒、峰值 RSS。CI 记录数字并
与基线比较（超过 1.5× 让任务失败）。Android 在发布前手工在 4 GB 设备上跑同一组
数字并记入 PR。

瓦片选择应利用网格索引或缓存缩小候选范围；当前包有 30,479 个瓦片，具体耗时
须在目标设备上测量，不能预先假定。

## 平台差异

| 能力 | Windows | Linux | Android |
| --- | --- | --- | --- |
| 图形 | Vulkan 1.1+ | Vulkan 1.1+ | Vulkan 1.0+ 驱动 |
| 视口呈现 | RGBA 回读 | RGBA 回读 | 原生纹理零拷贝 |
| 后台预处理 | 进程常驻 | 进程常驻 | 前台服务 + 管线 ABI |
| 文件选择 | 原生对话框 | 原生对话框 | SAF / 应用私有目录 |
| 大文件读取 | mmap | mmap | pread（部分设备 mmap 受限） |

三端都必须在启动时检测 Vulkan 并给出可读的失败原因，不许静默退出。

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
| `src/app/state/*` | `engine/session/view/` |
| `src/ffi/{EngineHost,FrameChannel,Gs3dEngineApi}` | `engine/abi/` |
| `src/ffi/HeadlessRenderKernel.cpp`（2,036 行） | 按职责拆进 `engine/session/` 与 `engine/render/` |
| `src/ffi/LodBuilder.cpp` | `engine/pipeline/compute/` |
| `src/platform/{CpuInfo,MemoryMappedFile}` | `engine/foundation/{diagnostics,io}/` |
| `include/{util,core}` 下的头文件 | `engine/foundation/`、`engine/data/core/` |
| `src/ffi/AndroidSurfaceJni.cpp` | `platform/android/` |
| `ui_flutter/` | `app/` |
| `src/control/*` | 旧 ABI 及其调用方迁移后删除；目前仍由 `gs3d_ffi` 使用 |
| `src/ui/{layouts,color,WorkspaceManager}`, `include/app/ViewerWorkbenchLayout.hpp` | 在现有引擎与控制层依赖迁移后删除（第一版不做工作区布局） |

已完成：`engine/foundation/{Gs3dVersion,diagnostics,concurrency,io}`、
`engine/data/{core,format,csv,lod,tile}`、`engine/pipeline/{compute,writers,verify}`。
`src/{data,preprocess}` 只剩目标定义，源码都在 `engine/` 下；CMake 目标名未变。
剩余 `src/{render,camera,app,control,ui,ffi,platform}` 随 ABI 与平台步骤迁移。

旧 GLFW 查看器中不参与构建的实现已移除；其需要保留的行为在第 5 步迁移。
旧查看器测试开关当前无法链接，相关测试须迁移后才能作为验收依据。

## 迁移顺序

每一步单独提交，提交后仓库必须能配置、构建、通过测试。

1. **清理旧代码**：先删已确认没有构建或运行依赖的代码；其余部分按依赖顺序迁移。
   旧 ABI 的预处理入口仍被引擎使用，必须先实现并接通替代入口，再与调用方、测试和
   构建配置一起删除旧实现。每次删除都保留仍有价值的测试，并让该次提交通过构建和测试。
   调整目录时同步更新工程守卫，不能提前移除仍适用的检查。
2. **目录搬迁**：只改路径与 include，不改逻辑；同时把 `check_include_deps.py` 的
   扫描根扩到 `engine/`，按两级模块（`engine/<层>/<模块>`）校验依赖方向。
3. **格式 v3**：写入器、读取器、迁移工具、校验和与恢复；删除 v1/v2 与旧 spec。
4. **ABI v1**：受锁保护的快照、异步命令与任务表、管线 ABI、JSON 通道；逐步重写
   Dart 绑定（Dart 只依赖 `gs3d_engine_*`，无第二消费者需要共存）。
5. **前端**：视口 → 数据集 → 渲染设置 → 测量 → 导出 → 最近项目；将旧查看器中
   需要保留的行为迁移到 `engine/session/`。
6. **CI**：Windows / Linux / Android 三矩阵 + lavapipe 渲染冒烟 + `gs3d_bench`
   基线比较（移植 `tools/gs3d_bench_bundle.cpp`）。

## 未决

- 桌面是否需要零拷贝纹理：等回读实测超过 3 ms 再决定。
- lavapipe 本机没装（需要 `vulkan-swrast` / `mesa-vulkan-drivers`），冒烟脚本未写。
- 60 s 预处理目标未实测：三个阶段都已并行，但没有仓库内的计时数字。
- Android 前台服务的存活策略（服务类型、电量与存储配额）未验证。
