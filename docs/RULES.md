# GeoScatter3D 规则

六条硬规矩，违反任何一条都算缺陷，不论功能是否"能用"。
目录结构见 [architecture.md](architecture.md)，操作步骤见 [recipes/](recipes/)。

---

## 规则一：依赖只能单向

```
engine/foundation  ←  engine/data  ←  engine/pipeline
src/render  →  engine/{foundation,data,pipeline}
src/{camera,app,control,ui}  →  src/render
src/ffi  →  全部（唯一对外出口）

src/platform  →  engine/foundation   （只剩 gs3d_platform 这个静态库，
                                      源文件是 CpuInfo 与 mmap 包装）
```

- 下层不知道上层存在。`engine/data` 不许 include `src/render`，`src/render` 不许
  include `src/ffi`。
- 同层的兄弟模块之间不互相 include；需要共享的类型放在更低层。层内需要分层
  时（如 `engine/data/format` 被 `engine/data/csv`、`engine/data/lod` 使用），在
  `scripts/check_include_deps.py` 的 `ENGINE_INTRA_ORDER` 里显式声明先后。
- `engine/` 只依赖标准库与第三方头文件，不依赖窗口系统、不依赖 Dart、不链接宿主
  代码；`engine/cli`（`gs3d_pack`、`gs3d_bench`）与 `engine/pipeline` 不需要 Vulkan
  就能编。
- C ABI 头只有 `include/ffi/geoscatter3d_ffi.h` 一个；`engine/abi/geoscatter3d.h`
  是早期的另一套设计（`geoscatter3d_engine_*` 命名），已入库但不参与构建，
  不要 include，也不要把它接进构建。

**校验**：`python3 scripts/check_include_deps.py`，CI 必跑。

**反例**：`engine/data/format/` 里直接 include `engine/render/device/VulkanBuffer.hpp`
去"顺手上传点云"。这会让数据层依赖 GPU，预处理工具链就再也编不进无 GPU 环境。

---

## 规则二：ABI 只增不改

`include/ffi/geoscatter3d_ffi.h` 是与 Flutter 的合同。

- 结构体只允许在**尾部追加**字段；不改已有字段类型、顺序、语义。
- 版本只有 `gs3d_engine_abi_version()` 一个出口，不进结构体；每个结构体首字段是
  `struct_size`，接收方先查大小再读其余字段。布局变化时同步升这个返回值。
- 删字段等于改类型：先标记弃用，等 Dart 侧不再读取，再删。
- 字符串只在调用期间有效，指针不许被引擎保存；引擎不许持有 Dart 侧指针（异步命令
  由引擎深拷贝参数）。
- 任意一次 ABI 改动必须同时改 `ui_flutter/lib/src/ffi/` 的镜像结构体**和**一个往返测试。
- ABI 1 已冻结：此后只追加，不重排、不改语义。

**反例**：为了"干净"把 `gs3d_state` 里的两个 `float` 合并成 `vec2`。Dart 侧用
`Struct` 按偏移读，字段顺序一改就静默读到垃圾值，而且不会崩溃，只在界面上显示错数。

---

## 规则三：状态只有一个权威

引擎是唯一权威：数据、相机、选择、渲染参数、任务进度都在 C++。

- Dart 只保存**最近一次快照的副本**用于绘制，不参与推导。
- 跨线程只传快照：渲染线程和 `poll_state` 用同一把短时持有的锁发布、复制完整一代。
  锁内禁止 IO、GPU 操作和回调；禁止让 Dart 读引擎内部结构。
- Dart 不许自己算点数、LOD 层级、坐标变换、是否可见——这些必须由引擎给出。
- 界面上的乐观更新只允许"按钮按下态"这类纯 UI 状态，且必须在快照到达时被覆盖。

**反例**：Dart 侧为了少一次调用，自己按视口尺寸和点大小估算"当前显示点数"。
一旦引擎因为预算裁剪了瓦片，两边数字就对不上，用户看到的是两个互相矛盾的读数。

---

## 规则四：一个文件一个职责

- 建议：源文件 ≤ 400 行，头文件 ≤ 300 行。这是提醒，不是闸门。
- 硬要求：一个类不做两件事。判断标准是"改其中一个功能时，是否会因为另一个功能的
  原因需要改动同一个文件"。
- 拆分按职责，不按行数。禁止为了缩短函数而拆出只被调用一次的转发函数。
- 新代码不要新增"全能内核"：同时做加载、渲染循环、选择、拾取、导出的类必须拆开。

**反例**：`HeadlessRenderKernel`（2,132 行）同时管数据集加载、LOD 重建、瓦片选择、
拾取和导出。想改拾取精度的人必须先读完渲染循环。

---

## 规则五：错误必须冒泡

- C++ 侧：不可恢复的失败返回负错误码并调用 `set_error`；不许 `catch (...) {}`。
- 长任务：失败必须在状态快照里可见（任务 `state = FAILED` 加 `error` 文本），
  没有唤醒回调，靠 Dart 侧轮询读到，不许静默吞掉。
- Dart 侧：统一由中枢转成界面提示，不许只在 `debugPrint` 里留一行。
- 启动失败必须能看见：不许出现"双击图标什么都没发生"。

**反例**：瓦片读取失败被记进日志，界面继续显示上一帧，用户以为地图是空的。
更糟的是异常从后台任务里抛出、一路冒到事件循环顶层，整个应用直接退出。

---

## 规则六：新模块必带测试

- 有分支、解析、写入、安全边界的模块，至少一个**会失败**的用例：故意给错输入，
  断言得到预期错误，而不是断言"没崩"。
- 新 ABI 命令：一个往返测试（Dart 侧编参数 → 引擎执行 → 快照里读到结果）。
- 新数据块类型：一个读写回环 + 一个损坏输入被拒绝的用例。
- 渲染改动：`ctest -R GeoScatter3D.EngineHost`（真实 GPU 回读与命令执行）必须仍通过。
- 不许提交"以后补测试"的模块。

**反例**：新增的格式校验写了 300 行边界判断，但没有一个测试喂过截断文件。
半年后有人改了偏移计算，没有任何测试会红。

---

## 什么时候可以破例

只能在 PR 里写明理由、并在同一 PR 里补一条 TODO 与责任人。允许的例外只有两种：

1. 临时不一致（例如一次 ABI 扩展需要两个提交才能收口），必须在下一个提交里消掉。
2. 第三方代码与 vendored 目录，本仓库不适用上述规则。
