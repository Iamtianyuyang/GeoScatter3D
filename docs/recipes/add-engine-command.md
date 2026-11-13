# 配方：加一个引擎命令

命令 = Dart 发意图，引擎执行，结果通过状态快照回到 Dart。

## 先选通道

| 通道 | 适用 | 入口 |
| --- | --- | --- |
| 普通命令 | 立即完成、参数是小 POD（相机、点大小、配色、背景、缩放、出帧、卸载） | `gs3d_engine_submit` |
| 长任务 | 读盘、预处理、LOD、开包 | 返回任务号，`gs3d_engine_poll_state` 读阶段与终态 |
| JSON getter | 低频结构化数据（数据集清单、最近项目、显卡、诊断） | `gs3d_engine_dataset_info_json` 等，返回内部持有的字符串 |
| 无 GPU 管线 | 预处理任务，Android 前台服务与 Flutter 共用 | `gs3d_pipeline_*`（同一头文件第 23 节） |

热点路径（出帧、相机输入、拾取）不走 JSON：解析与字符串分配的代价明显高于
一次结构体 `memcpy`，而且会引入生命周期问题。

## 步骤

1. **定类型**（`include/ffi/geoscatter3d_ffi.h`）：普通命令加一个 `GS3D_CMD_*` 与
   参数块，`gs3d_command` 的 `struct_size` 不变则参数块只能塞进既有并集；要加字段
   就在尾部追加并更新 `static_assert(sizeof(gs3d_command) == …)`。
   参数只放 POD 与"调用期间有效"的 `const char*`；异步任务要求引擎**深拷贝**。
2. **校验**（`src/ffi/EngineHost.cpp` 的 `submit`）：在调用线程做完整校验——范围、
   有限性、`struct_size`、`flags`、未知 op——失败返回 `-1`，未启动返回 `-2`。
   校验通过才入队，渲染线程不再校验第二遍。
3. **执行**（同文件 `execute`）：只碰渲染线程拥有的对象；失败调 `set_error`，
   错误文本进快照，不抛异常穿过 C ABI。
4. **C 适配**（`src/ffi/Gs3dEngineApi.cpp`）：把 ABI 结构映射成内部命令，处理空句柄
   与返回码，并保证 `out_task` 之类的可选出参允许为 `NULL`。
5. **Dart 镜像**（`ui_flutter/lib/src/ffi/gs3d_bindings.dart` 加 `ffi.Struct` 与
   `lookupFunction`；`ui_flutter/lib/src/ffi/gs3d_engine.dart` 加语义方法）。
   在 `ui_flutter/test/engine_state_layout_test.dart` 断言 `sizeOf` 与字段偏移。
6. **回流**：一次性结果写进 `gs3d_state`（下一次 `poll_state` 读出）；长任务写进
   任务表——阶段名、进度、终态。终态保留到被读取一次，防止 Dart 错过。
   没有唤醒回调，界面 200 ms、Android 服务 1 s 轮询。
7. **线程**：调用线程只做校验与深拷贝；GPU 相关在渲染线程，读盘在单个 IO 线程，
   预处理在工作池。工作线程要改渲染状态必须投递回渲染线程。

## 测试（缺一不可）

- 正常路径：提交后用 `poll_state` 读到预期字段（参考 `tests/EngineHostTests.cpp`）；
- 非法参数：特定错误码 + `last_error` 有内容，进程不崩；
- 前置条件不满足（没打开数据集、没有 GPU）：明确报错，不是静默无事发生；
- 长任务额外一条：立即返回任务号、进度单调、取消后进入终态、终态只被读取一次
  （参考 `tests/TaskBoardTests.cpp`）。

## 反面例子

把命令处理函数里 `std::string` 的 `data()` 直接给 Dart，或者异步命令保存了
`const char*` 参数。后果：调用返回后指针失效，Dart 读到随机内存，只在特定分配
顺序下崩溃——最难查的一类 bug。

## 检查

- [ ] 慢操作返回任务号，不阻塞调用线程
- [ ] 参数已深拷贝，引擎没保存调用方指针
- [ ] Dart 结构体有尺寸与偏移断言
- [ ] 四条测试都在，失败用例断言的是具体错误码
