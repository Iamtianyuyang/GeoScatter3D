# 配方：加一个引擎命令

命令 = Dart 发意图，引擎执行，结果通过快照或事件回到 Dart。

## 步骤

1. **定类型**（`engine/abi/geoscatter3d.h`）：在 `gs3d_command` 尾部加操作码与参数
   结构体，遵守规则二（只追加、不改已有字段、`abi_version` 加一）。
   参数里只放 POD 和"调用期间有效"的 `const char*`。
2. **镜像**（`app/lib/ffi/gs3d_bindings.dart` + `gs3d_command.dart`）：同名同布局的
   `Struct`，并在 `abi_layout_test.dart` 断言尺寸。
3. **Dart 封装**（`app/lib/ffi/geoscatter3d_service.dart`）：加一个语义命名的方法，
   内部只做参数校验和 `submit`，不做业务判断。
4. **引擎实现**：参数解析与校验放 `engine/session/`（需要 GPU 的放 `engine/render/`）。
   校验失败返回负错误码并 `set_error`；不许抛异常穿过 ABI。
5. **结果回流**：
   - 一次性结果 → 写进 `gs3d_state`，Dart 下一次 `poll_state` 读出。
   - 长任务 → 分配任务号，写进快照（阶段、进度、可取消），完成后发
     `GS3D_WAKE_TASK` 或 `GS3D_WAKE_ERROR`。
6. **测试**（三件事缺一不可）：
   - 正常路径：命令后快照字段符合预期；
   - 非法参数：返回错误码，`last_error` 有内容，进程不崩；
   - 前置条件不满足（没打开数据集/没 GPU）：明确报错，不是静默无事发生。

## 反面例子

命令处理函数返回 `std::string` 的 `data()` 给 Dart。
后果：调用返回后指针即失效，Dart 读到随机内存，而且只在特定分配顺序下崩溃。

## 检查

- [ ] 头文件只追加，没动已有字段
- [ ] Dart 结构体有尺寸断言
- [ ] 三种测试都有，且失败用例断言的是错误码而不是"没崩"
- [ ] 长任务能取消
