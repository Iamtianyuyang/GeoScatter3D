# 配方：加一个界面面板

以"加一个属性面板"为例。面板显示的数字必须来自引擎，面板只负责画。

## 步骤

1. **确认数据来源**：需要的数字引擎已经有吗？有 → 跳到 3。没有 → 继续。
2. **扩快照**（`engine/abi/geoscatter3d.h`）：在 `gs3d_state` **尾部**追加字段，
   `abi_version` 加一；在 `engine/session/view/` 里填这个字段。
   不许在面板里自己算（规则三）。
3. **镜像结构体**（`app/lib/ffi/gs3d_bindings.dart`）：加同名字段，偏移必须与头
   文件一致；同时在 `app/test/ffi/abi_layout_test.dart` 里断言 `sizeOf`。
4. **快照模型**（`app/lib/state/<name>_view.dart`）：不可变类，只含面板要显示的字段。
5. **中枢转出**（`app/lib/ffi/geoscatter3d_service.dart`）：从快照构造上述模型并
   `notifyListeners()`。中枢只做分发，不写业务判断。
6. **面板**（`app/lib/panels/<name>/<name>_panel.dart`）：`AnimatedBuilder` 订阅中枢，
   读快照画界面；用户操作调 `service.submit(...)`。
7. **注册**：在 `app/lib/layouts/main_shell.dart` 的对应分区里挂上面板。
8. **测试**：
   - `app/test/panels/<name>_panel_test.dart`：给定一个快照，断言显示内容；点按钮
     断言发出了对应命令（用假中枢）。
   - 若扩了快照，补一个引擎侧往返测试（见 `add-test.md`）。

## 反面例子

面板里直接调 `gs3d_engine_poll_state`，或者自己按点大小估算"可见点数"。
后果：引擎因预算裁掉瓦片后，界面数字与画面不一致，而且没法在测试里构造。

## 检查

- [ ] 面板不 import `dart:ffi`
- [ ] 数字全部来自快照
- [ ] 有 widget 测试，且测试里没有真实引擎
