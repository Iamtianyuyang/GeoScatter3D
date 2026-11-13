# 配方：加一个面板

以"加一个属性面板"为例。面板画的一切数字都来自引擎，面板只负责画和发命令。

## 步骤

1. **确认数据来源**：需要的数字引擎已经有吗？有 → 跳到 3。
2. **扩 ABI**（`include/ffi/geoscatter3d_ffi.h`）：在 `gs3d_state` 尾部追加字段并把
   `struct_size` 一起更新（规则二）。实现在渲染线程侧填写；结构体读数由
   `gs3d_engine_poll_state` 一次性拷出，或在专用 `*_json` getter 里给字符串。
   不许在面板里自己算（规则三）。
3. **镜像结构体**（`ui_flutter/lib/src/ffi/gs3d_bindings.dart`）：`ffi.Struct` 的字段
   顺序与头文件逐字节一致；同时在 `ui_flutter/test/engine_state_layout_test.dart`
   里断言 `sizeOf` 与关键字段偏移。
4. **引擎封装**（`ui_flutter/lib/src/ffi/gs3d_engine.dart`）：加一个语义命名的方法，
   只做参数打包与返回码转换，不做业务判断。
5. **中枢转出**（`ui_flutter/lib/src/ffi/geoscatter3d_service.dart`）：把结果存进
   service 的字段并 `notifyListeners()`；需要落盘的设置走 `app_state.dart` 的原子写。
6. **面板**（`ui_flutter/lib/src/widgets/<name>_panel.dart`）：用 `ListenableBuilder`
   订阅 service，用 `ui_kit` 的 `PanelSection`／`KeyValueRow`／`SliderField` 等拼装；
   用户操作调 service 的方法。
7. **注册**：在 `ui_flutter/lib/src/layouts/main_shell.dart` 的 `ShellSection` 里加一个
   枚举值、一条 `_SectionSpec`（标题、副标题、图标、key 名），并在 `_panelFor` 里
   返回新面板。工具轨与 key `rail_<keyName>` 会自动出现。
8. **测试**：
   - `ui_flutter/test/<name>_panel_test.dart`：给 FakeEngine 设数据，断言显示内容；
     点按钮断言发出了对应命令（FakeEngine 记录调用）。
   - 若扩了 ABI，补一条引擎侧往返测试（见 `add-test.md`）。

## 反面例子

面板里直接调 `gs3d_engine_poll_state`，或者自己按点大小估算"可见点数"。
后果：引擎因预算裁掉瓦片后，界面数字与画面不一致，而且没法在测试里构造。

## 检查

- [ ] 面板不 import `dart:ffi`（只经 service）
- [ ] 数字全部来自引擎
- [ ] 有 widget 测试，且测试里用 FakeEngine 而不是真实引擎
- [ ] 键盘可达：按钮与下拉给 tooltip，滑块给数值标签
