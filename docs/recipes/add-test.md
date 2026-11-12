# 配方：加一个测试

测试的价值在于"坏了会红"，不在于覆盖率数字。

## C++

1. **位置**：`tests/<模块><对象>Tests.cpp`，一个文件一个可执行目标。
2. **注册**（`tests/CMakeLists.txt`）：
   ```
   gs3d_add_test(NAME GeoScatter3D.<模块>.<对象>
                 SOURCES <模块><对象>Tests.cpp
                 LIBS gs3d_<层>)
   ```
   命名对齐已有风格（`GeoScatter3D.Gs3dV2`、`GeoScatter3D.ThreadPool`）。
3. **写什么**：至少三条——正常路径、边界（空/零/最后一项/最大值）、错误输入
   （断言得到预期错误码或异常，而不是"没崩"）。
4. **用标签**：`[gs3d]`、`[tile]`、`[abi]`，方便 `ctest -R` 与本地过滤。
5. **禁止**：依赖真实 GPU 的用例放进 `RenderSmoke`；其余用例不许需要显卡。

## Dart

1. **位置**：`app/test/<分区>/<对象>_test.dart`；组件测试放 `app/test/panels/`。
2. **不碰真引擎**：用假中枢注入快照；`app/lib/ffi/` 的绑定测试用固定字节缓冲。
3. **布局断言**：任何结构体镜像都要有 `sizeOf` 与字段偏移断言——这类错误不崩，
   只会显示错数，唯一能拦住它的就是测试。

## 端到端

- 无 GPU 冒烟：样例 CSV → 工程包 → 生产 reader 回读（对齐
  `GeoScatter3D.FreshClonePreprocess` / `FreshCloneBundle`）。
- 有 GPU 冒烟：`RenderSmoke` 用 lavapipe 渲一帧，比像素与帧时间。

## 反面例子

```cpp
REQUIRE_NOTHROW(reader.open(path));   // 错了也绿
```
应该断言：`CHECK(reader.open(truncated) == ErrorCode::TruncatedHeader)` 且
`CHECK(reader.last_error().find("truncated") != std::string::npos)`。

## 检查

- [ ] 失败用例断言的是具体错误，不是"没崩"
- [ ] 不依赖 GPU（除非在 `RenderSmoke`）
- [ ] 新增结构体镜像有 `sizeOf` 断言
- [ ] `ctest` 与 `flutter test` 都跑过
