# 架构

## 目标

一个应用，两种语言：C++ 引擎负责数据、渲染与状态，Flutter 只负责界面。
三端（Windows / Linux / Android）本地离线运行，都能打开千万级点云，
都能自己完成预处理。工程包三端通用，可直接拷贝。

## 目录

```
engine/                 引擎核心，不依赖窗口与 Dart
  foundation/             diagnostics、io（含 SHA-256）、concurrency、math、util
  data/                   csv（分块读取）、format（Manifest、PointChunk、V3BundleReader）、
                          lod（Gs3dLodDataset/Writer）、tile
  pipeline/               stages（BundleBuild、BundleJob、各阶段）、compute、writers、verify
include/ffi/             C ABI 头 geoscatter3d_ffi.h，宿主侧 EngineHost、TaskBoard、
                          HeadlessRenderKernel、LodBuilder、FrameChannel
src/                     ffi（C ABI 实现与内核）、render、camera、app；
                         control/ui 与 platform 是旧查看器遗留，Flutter 不依赖
ui_flutter/              Flutter 应用
  lib/src/layouts/        main_shell（顶栏 + 工具轨 + 面板 + 视口 + 状态条）
  lib/src/widgets/        七个面板、视口画布、状态条、视图轴、色标图例
  lib/src/ui/             ui_kit、format
  lib/src/ffi/            ABI 绑定、引擎包装、中枢服务、应用态、显示设置、管线服务
  lib/src/theme/          app_tokens
  android/ linux/ windows/ linux 与 windows runner、Android 前台服务
tests/                   C++ 测试（Catch2）
scripts/                 构建、打包、依赖与护栏检查、性能测量
data/  assets/  config/  docs/  tools/  third_party/
```

依赖方向见 [RULES.md](RULES.md) 规则一。宿主层（`ui_flutter/linux`、
`ui_flutter/windows`、`ui_flutter/android` 与 `src/ffi` 里的 JNI 胶水）位于最上层：
只调用 C ABI，引擎从不 include、也不链接宿主代码。

## 线程模型

| 线程 | 拥有 | 不许做 |
| --- | --- | --- |
| 主线程（Flutter UI） | Dart 侧状态、界面、命令参数校验 | 等待长任务、读文件、提交 GPU |
| 渲染线程 | `VkDevice`、队列、视口与 GPU 资源、快照发布 | 回调 Dart、阻塞在 IO |
| IO 线程（单个） | 打开工程包时的读盘与解码 | 提交 GPU、改引擎状态 |
| 工作池 | 点块解码、预处理各阶段、统计 | 提交 GPU、改引擎状态 |

- 普通命令（相机、点大小、配色、背景、缩放、出帧、卸载）经 `gs3d_engine_submit`
  入队即返回；开包这类长任务返回任务号，由 Dart 每 200 ms 轮询快照。
- 长任务（预处理任务、开包）可取消，取消点是阶段边界；导出与读帧是渲染线程上的
  同步调用，没有取消点，所以有明确点数上限。
- 只有渲染线程发布快照；只有主线程读取快照。见下文「状态快照」。
- 引擎侧只有一个 IO 线程，同一时刻只跑一个开包任务；4 GB Android 设备上统计线程
  上限为 4（`StatisticsPass`），管线同时只允许一个重任务（`BundleJob` 的 `JobSlot`）。

## ABI 契约

头文件：`include/ffi/geoscatter3d_ffi.h`（`engine/abi/geoscatter3d.h` 是早期设计稿，API 与现在的 ABI 不同、
不参与构建，不要 include）。规则二（只增不改）优先于本文任何写法示例。

### 通则

- 所有结构体第一个字段是 `uint32_t struct_size`；接收方先查大小，过小直接拒绝。
- ABI 自身的版本由 `uint32_t gs3d_engine_abi_version(void)` 给出，不塞进每个结构体。
- 字符串与数组指针**只在调用期间有效**。异步命令的参数由引擎深拷贝；引擎绝不
  保存调用方指针。
- 错误：`int32_t` 返回 0 成功、负数错误码；引擎任务的错误文本放在已发布的
  快照里（定长 `char[256]`），管线任务放在其状态中；不返回裸指针。
- 任何一次 ABI 改动同时改 `ui_flutter/lib/src/ffi/gs3d_bindings.dart` 的镜像与
  `ui_flutter/test/engine_state_layout_test.dart` 的布局断言。

### 状态快照

只有一个写者（渲染线程）、一个读者（主线程）。做法与 `FrameChannel` 相同：

```c
typedef struct {
    uint32_t struct_size;
    uint64_t generation;      /* 每次发布 +1 */
    /* …相机、点数、预算、任务表、char error[256]… */
} gs3d_state;

/* 拷贝最近一次已发布的完整一代；没有发布过则返回 -2 */
int32_t gs3d_engine_poll_state(gs3d_engine*, gs3d_state* out);
```

引擎用短时间持有的互斥锁保护已发布的 `gs3d_state`：渲染线程先在本地构造快照，
只在复制发布时持锁；`poll_state` 持同一把锁复制快照。锁内不做 IO、GPU 操作或
回调，禁止半写半读。不得仅靠双缓冲和序号重试：写者再次复用缓冲时仍可能与读者并发访问。

任务表也在这份快照里（固定上限 4 项）：`{id, stage, state, progress}`。任务结束时
状态保留到被读取一次为止，防止 Dart 错过终态；任务表已满时必须明确返回错误。

**当前实现**：`include/ffi/geoscatter3d_ffi.h` 第 21 节与 `include/ffi/TaskBoard.hpp`（
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
  轮询快照），预处理面板与顶栏的「打开工程包」通过平台目录选择器选包并显示阶段；
  基准/样例路径仍用同步 `gs3d_engine_load_dataset`。二者共用同一内核装载代码。
- 导出（成果面板）：`gs3d_engine_export` 写 PLY/CSV、`gs3d_engine_read_frame_png`
  取当前帧 PNG，文件落在应用状态目录的 `exports/`，界面只展示路径。导出仍是同步
  调用（占渲染线程），因此超过 200 万点直接拒绝并说明原因，另存为对话框也尚未做。
- 测量（测量面板）：视口单击调用 `gs3d_engine_pick_point`，依次记录起点和终点，
  显示两点距离与带符号高差；第三次点击开始新的一组，未命中不改变现有测量。
  拾取在渲染线程上线性扫描已装载的点（`src/ffi/HeadlessRenderKernel.cpp` 的
  `pick_point`），样例数据即时，50M 数据集一次点击要数秒，尚未接 GPU id 缓冲。
- 显示设置（渲染面板）：配色索引与点大小先改本地状态，拖动结束才通过
  `submitColormap`/`submitPointSize` 入队，落盘到平台应用状态目录的
  `render-settings.json`（临时文件 + 改名）；读回失败回到默认值，不影响启动。
  设置只影响本机显示，工程包内容不变。

### 命令

```c
typedef struct {
    uint32_t struct_size;
    uint32_t op;
    uint32_t flags;           /* 保留，必须为 0 */
    union { /* 每类命令一个 POD 参数块，字符串为 const char* */ } args;
} gs3d_command;

int32_t gs3d_engine_submit(gs3d_engine*, const gs3d_command*,
                           uint64_t* out_task);   /* 可为 NULL */
int32_t gs3d_engine_cancel(gs3d_engine*, uint64_t task);
```

普通命令包括相机输入、点大小、配色、背景、请求一帧；返回成功表示已校验并入队，
不表示渲染线程已执行。调用线程只做参数校验与深拷贝，不等待执行。
只有 `gs3d_engine_open_dataset` 返回任务号：读盘在 IO 线程、装载在渲染线程，
用 `poll_state` 看阶段与终态。卸载、导出、PNG 与 JSON 快照都是同步调用
（`unload_dataset` 走 submit，导出与读帧在渲染线程内完成），因此有明确上限。

### 唤醒

没有唤醒回调：Dart 侧按固定间隔（界面 200 ms，Android 服务 1 s）轮询
`gs3d_engine_poll_state`。回调在 Flutter 平台通道上会引入线程归属问题，
轮询的实现代价低于它，所以不做。

### 像素与表面

```c
int32_t gs3d_engine_read_frame_rgba(gs3d_engine*, uint8_t* out, uint32_t cap,
                                    uint32_t* w, uint32_t* h, uint32_t* stride);
int32_t gs3d_engine_read_frame_png(gs3d_engine*, uint8_t* out, uint32_t cap,
                                   uint32_t* out_len);
int32_t gs3d_engine_request_frame(gs3d_engine*);
```

- 桌面与测试走回读：Flutter 把 RGBA 缓冲解成 `ui.Image` 贴到 `Texture`。
  1080p 回读加绘制约 4.5 ms，回读同步占渲染线程，因此一次只允许一个未完成的请求。
- 窗口呈现走 `gs3d_engine_set_surface(provider)` / `gs3d_engine_clear_surface`：
  `gs3d_surface_provider` 只带 instance 扩展名、`create_surface` 回调与 `user_data`，
  由平台适配层把 `HWND` / `ANativeWindow` 转成句柄，ABI 里不出现 Vulkan 类型。
  之后 `present_frame` 把帧呈到交换链，`has_surface` 查询当前模式。
- 表面句柄的所有权留在宿主（Windows runner、Kotlin 前台服务）；宿主在销毁前
  停止任务并 `clear_surface`，引擎不接管句柄生命周期。
- Android 的零拷贝：Kotlin 侧持有 `Surface`，经 JNI（`MainActivity.nativeAttachSurface`
  → `gs3d_engine_set_surface`）交给引擎，Dart 只拿到 texture id 用于显示；失败时
  自动退回 RGBA 回读（`ui_flutter/lib/src/ffi/present_surface.dart`）。

### JSON 通道

低频结构化数据走 JSON，不塞进结构体。打开数据集后由专用 getter 返回，字符串由
ABI 内部持有到下一次同线程调用：

```c
const char* gs3d_engine_dataset_info_json(gs3d_engine*);   /* 路径、点数、范围、值域、属性、CRS */
const char* gs3d_engine_get_snapshot_json(gs3d_engine*);
const char* gs3d_engine_get_diagnostics_json(gs3d_engine*);
```

字段只增不改，未知字段忽略；热点路径（出帧、相机输入、拾取）不走 JSON。

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
  lod.gs3dlod           预计算层级（包旁单个 sidecar，临时文件 + 改名发布）
```

不存"全分辨率单体文件"：点只存在于 chunk 中，打开时只读 manifest 与块表，
按需读块。整文件加载会占用较大的内存预算，是否超限须以目标设备实测为准。

manifest 必须包含：

```json
{
  "format_version": 3,
  "min_reader_version": 3,
  "required_features": ["points"],
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
- 已实现：建包末尾的 `lod` 阶段用 `Gs3dLodDataset` / `Gs3dLodWriter` 生成
  `lod.gs3dlod`（临时文件 + 改名，失败不阻塞建包，读取端会自行建链）；
  引擎打开工程包时优先用它，不再运行时建链。
- 未实现：`lod/` 与 `tiles/` 的 v3 分块写入与读取（现阶段 LOD 是包旁的单个
  sidecar 文件，不参与 manifest 校验）；在它们落地前，manifest 的
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
  注意 `build/` 是 Debug 构建：同一命令在其中跑 50M 要 177 s、开包要 394 s，只能用于功能
  验证，性能数字必须取 `tmp/build-linux`（Release）。
- 打开 50M 工程包（Release，Linux，RTX 5060）：没有 sidecar 时 `gs3d_engine_load_dataset`
  同步读盘 + 运行时建链共 29.7 s；其中 LOD 建链 22.3 s、块解码并行后约 2.6 s、GPU 上传
  0.17 s。预处理的 `lod` 阶段补上 sidecar 后同一命令 **2.40–2.46 s**（多次测量），LOD 5 层、总 GPU 点 3.15 M、
  默认视角绘制 813 k 点，峰值 RSS 1.2 GB。距离“打开到可交互 ≤ 1.5 s”仍有差距，差距来自
  766 MB 点块的解码与上传，不是建链。
  代价：50M 建包多出 `lod` 阶段（读回点块 + 建 5 层 + 写 50 MB sidecar，实测 +49 s），
  总建包时间因此超出 60 s 预算；这是把耗时从每次打开搬到一次的取舍。不需要快速打开时
  可用 `gs3d_pack --no-lod` 跳过它（1.34 M 点样本实测 6.3 s → 2.8 s，打开时改由运行时
  建链）。
- 帧时间（Release，1920×1080，50M 包、813 k 点）：`render_rgba` 4.1 ms/帧，首帧 17 ms。
  修复前同一命令 68 ms：回读用的 staging 缓冲挑到的是显卡上的 host-visible BAR 内存，
  CPU 侧未缓存，单是整屏 memcpy 就 33 ms；改挑 `HOST_CACHED` 的系统内存后 memcpy 降到
  1.4 ms。离屏回读路径（Windows/Linux 视口）因此从 68 ms 降到 4 ms，进入 16 ms 预算。
  `estimate_spacing` 曾按输入顺序取连续块，50M 数据按列导出时估出 537,970 的“点距”，
  整份数据被压成一层、屏幕上只剩 67 k 点；现以 bbox 最大面的面密度 sqrt(area/n) 作为
  与顺序无关的上界，同一数据改为 4 层。Android 设备没有实测数据。


## 性能预算

50M 点、37 km × 40 km、高程 −190…282 m 为基准数据集。

| 指标 | Windows / Linux | Android（4 GB） |
| --- | --- | --- |
| 打开到可交互 | ≤ 1.5 s | ≤ 4 s |
| 拖动 / 旋转帧时间 | ≤ 16 ms | ≤ 33 ms |
| 预处理 50M 点 | ≤ 60 s（16 核） | ≤ 4 min |
| 峰值内存 | ≤ 2 GB | ≤ 1.2 GB |
| 桌面回读额外延迟 | ≤ 3 ms | 不适用 |

测量门禁：`gs3d_bench`（`engine/cli/gs3d_bench.cpp`，桌面目标；`--bundle` 测开包与帧、
`--csv ... --out ...` 测预处理各阶段）输出一行 JSON：开包毫秒、首帧毫秒、稳态帧毫秒、
点数、峰值 RSS（Linux 取 `/proc/self/status` 的 VmHWM）。跨进程的同一组数字也可用
`scripts/measure_performance.py open|pack` 取。CI 应记录数字并与基线比较（超过 1.5× 让
任务失败）；当前 CI 只做构建与测试，逐渐落到门禁还需要基线机器。Android 在发布前手工在 4 GB 设备上跑同一组
数字并记入 PR。

瓦片选择应利用网格索引或缓存缩小候选范围。注意瓦片流式只对旧版带
`tiles.gs3dtiles(.index)` 的数据集生效，v3 工程包没有瓦片，走的是
`lod.gs3dlod` 的多层 LOD；两者不能混为一谈，耗时须在目标设备上测量。

### 本机实测（2026-11-13）

硬件 Ryzen 9 7945HX（32 线程）、RTX 5060 Laptop 8 GB、DDR5；GCC Release（`tmp/build-linux`）；
数据 `data/field_and_ele_source_rec_50m.dat`（3.35 GB、50,000,000 点、763 块）。

| 指标 | 预算 | 实测 | 结论 |
| --- | --- | --- | --- |
| 预处理 50M（写 lod sidecar） | ≤ 60 s / ≤ 2 GB | 115.1–119.6 s / 3.09 GB | 两项都超：lod 阶段把整包再读进内存（约多 64 s、+2.4 GB） |
| 预处理 50M（`--no-lod`） | ≤ 60 s | 54.3–55.5 s，峰值 637 MiB | 达标，代价是开包回落到约 47 s |
| 打开到可交互（带 sidecar，1920×1080） | ≤ 1.5 s | 2.40–2.46 s，峰值 1.22 GB | 超预算约 0.9 s |
| 稳态帧时间 / 首帧 | ≤ 16 ms | 4.5 ms / 22 ms | 达标 |
| 打开峰值 RSS | ≤ 2 GB | 1.22 GB | 达标 |

复现：

```bash
/usr/bin/python3 scripts/measure_performance.py pack \
  --pack tmp/build-linux/src/gs3d_pack \
  --source data/field_and_ele_source_rec_50m.dat --out tmp/bench-50m [--no-lod]
# 打开与帧时间需要在带 data/gs3d_shaders（或 assets/shaders）的 cwd 下跑：
cd ui_flutter && ../tmp/build-linux/src/gs3d_bench --bundle ../tmp/bench-50m \
  --frames 60 --width 1920 --height 1080
```

两条超预算的取舍已写在「工程包格式」与「未决」：lod 阶段把 5 层粗化串行遍历全量点，
要么接受更长的预处理，要么接受打开回落到约 47 s；下一步只有把 lod 与 points 阶段
合并成一趟读写，才可能两头都达标。

## 平台差异

| 能力 | Windows | Linux | Android |
| --- | --- | --- | --- |
| 图形 | Vulkan 1.1+ | Vulkan 1.1+ | Vulkan 1.1+ 驱动 |
| 视口呈现 | RGBA 回读 | RGBA 回读 | 原生纹理零拷贝 |
| 后台预处理 | 进程常驻 | 进程常驻 | 前台服务 + 管线 ABI |
| 文件选择 | 原生对话框 | 原生对话框 | SAF / 应用私有目录 |
| 大文件读取 | mmap | mmap | pread（部分设备 mmap 受限） |

三端都必须在启动时检测 Vulkan 并给出可读的失败原因，不许静默退出。

### 渲染冒烟记录

| 平台 | 方法 | 结果 |
| --- | --- | --- |
| Linux 窗口 | `flutter build linux --release` 后直接运行 `bundle/ui_flutter`（Wayland 会话），用 `grim` 截屏核对视口 | 部分：窗口起来、Vulkan 选中 RTX 5060、样例 10 000 点可见、无崩溃；但 Flutter 3.50 的 GTK embedder 在本机分数缩放（1.25）下把窗口内容只合成一部分（`flutter create` 空模板同样复现，改缩放/软件渲染/XWayland 都无效），窗口级界面核对不可靠，界面检查改用离屏 golden 预览 |
| Linux 离屏 | `GS3D_ENGINE_E2E=1 GS3D_PIPELINE_LIBRARY=... flutter test`，读回 RGBA 断言像素 | 通过：回读、轨道、复位、resize、拾取，全套 60 用例 0 跳过 |
| Windows | 无机器 | 未验证：只有 CI 构建与单元测试，窗口与呈现未跑 |
| Android | 无设备、无模拟器（SDK 缺 emulator 包，下载 <1%/20 s，已放弃） | 部分：两个 ABI 的原生库与 `flutter build apk --debug` 均在本机通过，APK 内两个 ABI 的 `libgs3d_ffi.so`、`libgs3d_pipeline_ffi.so` 与 `libc++_shared.so` 齐全（管线库随仓库预置，干净克隆也能直接出包；换引擎代码后必须重新交叉编译并覆盖）；JNI 建包与 Kotlin 直接编译已过。前台服务、后台恢复、零拷贝呈现仍未上机 |

失败路径同样算冒烟的一部分：`VK_ICD_FILENAMES=/nonexistent.json gs3d_bench --bundle ...`
打印 `VulkanContext: failed to create Vulkan instance (没有可用的 Vulkan 1.1 驱动或 ICD；
Linux 可用 vulkaninfo 检查)` 并以 1 退出，不会静默黑屏。

窗口缩放的呈现路径由合成器决定（Hyprland 平铺本次把窗口放到 703×1194 逻辑像素，
物理 1406×2388 超出 2560×1600 屏），本机没有可用的 `hyprctl` 缩放语法，
所以窗口缩放只由离屏 resize 用例覆盖，窗口级未见回归。

窗口冒烟的前置条件：`data/gs3d_shaders` 必须在可执行文件目录下，否则引擎创建失败并且只在日志里留一行 shader 路径。

## 前端

界面是测绘仪器风工作台：近黑壳体（`viewport 0x070B0F`、`panel 0x0F161C`）、1px 发丝线、
单一信号色（`0x58D6A8`）、等宽 tabular 数字。没有卡片套卡片，没有装饰性图标，
视口里只放视图轴、缩放、色标图例与测量读数。

```
ui_flutter/lib/
  main.dart                  主题装配（buildAppTheme）
  src/theme/app_tokens.dart  AppTokens：颜色 / 字阶 / 间距 / 圆角 / 动效
  src/ui/                    ui_kit（PanelHeader、KeyValueRow、Segmented、SliderField…）与 format
  src/layouts/main_shell.dart 顶栏 + 工具轨 + 面板 + 视口 + 状态条
  src/widgets/               viewport_canvas、project、render、camera、measurement、
                             result、diagnostics、pipeline 七个面板与视口浮层
  src/ffi/                   ABI 绑定（gs3d_bindings）、引擎包装、中枢服务、
                             应用态（app_abi：显卡与最近项目）、显示设置与管线服务
```

布局规则：左侧 52px 工具轨，面板展开时把视口挤窄（IDE 式），不覆盖点云，
所以缩放窗口时投影中心不跳。工具轨再次点击当前项即收起面板。宽度不超过 520 时
面板宽度收在 `200..320`；200% 字号下顶栏与面板都不许溢出（顶栏高度随字号放宽，
面板内容滚动）。底部状态条是唯一的全局状态显示：就绪 / 载入 / 错误、数据集、
`源 → 绘制`、LOD、FPS、帧时间、渲染时间、GPU。

已接入的能力（C ABI → Dart → 界面）：

| 能力 | ABI | 界面位置 |
| --- | --- | --- |
| 打开工程包 / 卸载 | `gs3d_engine_open_dataset`、`poll_state`、`unload_dataset` | 顶栏「打开工程包」、工程面板 |
| 数据集清单 | `gs3d_engine_dataset_info_json` | 工程面板详情（点数、范围、值域、属性、坐标系） |
| 最近项目 | `gs3d_engine_recent_projects_json`、`remember/forget/clear` | 工程面板「最近项目」 |
| 配色 / 点大小 / 点形状 | `set_colormap_index`、`set_point_size`、`set_point_shape` | 渲染面板 |
| 高程与着色 | `set_height_source`、`set_color_source`、`set_height_exaggeration` | 渲染面板 |
| 值域裁剪 | `set_value_clip` | 渲染面板（上下限取数据集值域） |
| 背景 / 绘制上限 | `set_background`、`set_point_budget` | 渲染面板 |
| 相机 | `camera_input`、`gizmo_axis`、`reset_camera` | 视口浮层、相机面板 |
| 拾取与测量 | `pick_point` | 视口单击、测量面板、视口读数 |
| 导出 | `gs3d_engine_export`、`read_frame_png` | 成果面板 |
| 诊断 | `get_diagnostics_json`、`clear_tile_cache`、`gpus_json`、`set_preferred_gpu` | 诊断面板 |
| 预处理 | 管线 ABI（`pipeline.start/resume/cancel/poll`） | 预处理面板 |

刻意不接入的：`gs3d_ffi_*` legacy 段的区域统计、导航图缩略图、属性列表与
`performance_metrics`——它们读 legacy 数据集缓存，本应用走 `gs3d_engine_*` 开包，
这些调用只会返回空数据或 stub（`performance_metrics` 原生就是 `fps 0.0`、
`cache hit -1.0`），接上去等于伪造读数。要接就得先让它们在引擎打开的数据上工作。

显示设置（配色、点大小、形状、背景、高程、值域、上限）原子写入应用状态目录，
重启后恢复；加载失败保留默认值，不阻塞启动。拖动滑块只改本地预览，
`onChangeEnd` 才提交给引擎并落盘，避免每帧生成一条命令。

界面自检：`flutter test` 覆盖单一控制台结构、面板挤压视口、工具轨开合、200% 字号
与 800×600 不溢出、开包、渲染设置提交、测量读数、成果上限拒绝。视觉核对用
一次性 golden 预览（`FontLoader` 加载 Noto CJK，1440×900）而非真机截图——
Hyprland 平铺会在 subsurface 尺寸不一致时把界面合成为残缺画面，不能作为界面证据。

## 验收记录

对照本文档的成功标准，逐项证据（2026-11-13，Linux 本机 + GitHub Actions）：

| 标准 | 证据 | 结论 |
| --- | --- | --- |
| 三端可构建 | Linux `flutter build linux --release`、`build/` 与 `tmp/build-linux` 两个 CMake 目录；Android 两个 ABI 交叉编译 + `flutter build apk --debug`（141 MB，内含两个 ABI 的引擎与管线库）；Windows 由 CI `windows` 作业构建 | Linux/Android 本机通过；Windows 只在 CI |
| 50M 工程包 | `tmp/bench-50m-lod*`：50,000,000 点、763 块、763 MiB，开包 2.40 s、帧 4.5 ms | 通过（打开时间超预算，见性能预算） |
| 三端离线预处理、同一 v3 格式 | `gs3d_pack`（Linux）50M 建包两轮；`gs3d_pipeline_ffi` + `gs3d_preprocess` 交叉编译到 arm64/x86_64；格式测试 `ManifestTests`/`BundleBuildTests`/`PointStageTests` 全绿 | 通过（Android 上未真机跑） |
| Android 后台与阶段恢复 | `PipelineJobService` 持有 JNI 任务、`BundleJob` 阶段检查点与 `--resume`；JVM 直连冒烟（UTF-8 路径建包、poll、NUL 拒绝）与 Kotlin 直接编译通过 | 代码级通过，设备级未验证 |
| 单进程 ABI / 快照 / 异步任务 | `geoscatter3d_ffi.h` §21–23 + `TaskBoard`、`EngineHost` 单 IO 线程；`TaskBoardTests`、`EngineHostTests`（真实 GPU）、Dart `engine_state_layout_test` | 通过 |
| 视口/相机/拾取/数据集/渲染设置/测量/导出/最近项目/偏好 | 对应 widget 与服务测试：`main_shell_test`、`measurement_panel_test`、`export_test`、`render_settings_test`、`pipeline_history_test`、`engine_test` | 通过（离屏 + 窗口冒烟） |
| 自动化测试 | CTest 41/41；Flutter 54 通过 6 跳过，带 `GS3D_ENGINE_E2E=1` 与 `GS3D_PIPELINE_LIBRARY` 时 60 通过 0 跳过；`check_include_deps.py`、`check_engineering_guardrails.py` 均 OK | 通过 |
| 三端 CI | run 43455696344（引擎收尾）与 run 43477435097（前端重设计）：Linux、Android、Windows 三条均通过，含 Flutter Windows 打包与三端 Flutter 测试 | 通过 |
| 渲染冒烟 | 离屏回读与真实 GPU 测试全过；Linux 窗口能起且点云可见（窗口合成受本机 Flutter GTK 分数缩放缺陷影响，见「渲染冒烟记录」）；Vulkan 缺失给出可读报错并退出 1 | 通过（离屏）；窗口级受限 |
| 可复现性能测量 | `scripts/measure_performance.py`（open/pack，含 `--no-lod`）与 `gs3d_bench`；数字见性能预算 | 通过，两条超预算已记录 |

## 未决

- 桌面是否需要零拷贝纹理：等回读实测超过 3 ms 再决定（当前 1080p 回读帧 4.5 ms，含回读）。
- lavapipe 本机没装（需要 `vulkan-swrast` / `mesa-vulkan-drivers`），冒烟脚本未写。
- 打开 ≤ 1.5 s：实测 2.40 s，瓶颈是读点块与 LOD/GPU 上传；要达标得把 sidecar 读取与上传
  重叠，或让 sidecar 直接给 GPU 可用的紧凑布局。
- 预处理 ≤ 60 s 且 ≤ 2 GB：带 lod 时 115–120 s / 3.09 GB，`--no-lod` 时 55 s / 637 MiB。
  lod 阶段当前把整包再读进内存；要两头达标得改成逐块流式累积各层体素。
- Android 前台服务的存活策略（服务类型、电量与存储配额）、后台恢复与零拷贝呈现未在设备上验证。
