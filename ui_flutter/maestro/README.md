# GeoScatter3D 真机 Maestro 用例（配合 pi 的 Maestro MCP 使用）

这套流程用来在**真机**上驱动并断言 Flutter 视口，尤其是那些脚本以前碰不到的部分
（相机缩放、瓦片层开关、HUD 状态）。

## 为什么需要 `GS3D_DEBUG_CONTROLS`

视口的缩放/平移来自 Flutter 的 `GestureDetector.onScaleUpdate`（双指）与
`Listener.onPointerSignal`（滚轮）。**外部自动化注入不进去**：

| 通道 | 结果 |
|---|---|
| Maestro 手势 | 这套 Maestro CLI 没有 pinch，只有 tap/swipe/scroll |
| `adb shell input` | 没有多指；`roll`/`motionevent` 引擎不消费 |
| 触控板/S Pen | 会**自己漂移**（键盘保护套误触），不可控且会污染 A/B |

所以调试版 APK 用开关把「相机控制」变成**语义化按钮**，并把内核的选取状态变成
**可断言文本**：

```bash
cd ui_flutter
flutter build apk --debug --target-platform android-arm64 \
  --dart-define=GS3D_DEBUG_CONTROLS=true \
  --dart-define=GS3D_BENCH_DATASET=/data/local/tmp/bench-1m.bundle \
  --dart-define=GS3D_BENCH_ZOOM=0
adb install -r -d build/app/outputs/flutter-apk/app-debug.apk
```

打开后视口右下角多出（都有稳定 tooltip → Android 无障碍标签，可 `tapOn`）：

- `缩小` / `放大`：各推进一步滚轮缩放（`camera.input{scrollY:∓3}`）；
- `复位相机`：复位到拟合位姿（生产版也有，用于断言基准位姿）；
- 一行等宽文本（`cover=0 below=1243 clip=1 tile=357/357 lod=2/3 drawn=62871`）。

### 调试行字段

| 字段 | 含义 |
|---|---|
| `cover` | 瓦片层是否**盖住整屏可见面**；只有 `cover=1` 时底图才允许退回粗层 |
| `below` | 视锥内投影尺寸未达 `min_tile_pixel_size=50` 的瓦片数；>0 就是「瓦片带 + 稀疏底图」的病根 |
| `clip` | 底图是否被瓦片包围盒裁剪 |
| `tile=<驻留>/<需求>` | 需要多少块、实际驻留多少块（`0/0` = 这一帧整层不出） |
| `lod=<当前>/<总数>`、`drawn` | 底图选中的层与实际绘制点数 |

## 怎么跑

用 pi 的 Maestro MCP 工具（推荐，截图/层级都由 MCP 直接回传）：

```text
maestro_list_devices                                   # 拿 device_id（Tab S7: R52R709CWHK）
maestro_run  { device_id, dir: "ui_flutter/maestro/flows" }
maestro_run  { device_id, files: ["ui_flutter/maestro/flows/10_tiles_all_or_nothing.yaml"] }
maestro_inspect_screen { device_id }                   # 看 HUD/按钮的无障碍标签
maestro_take_screenshot { device_id }                  # 截图（见下方尺寸坑）
```

或直接用 CLI：

```bash
maestro test ui_flutter/maestro/flows                       # 全部
maestro test --include-tags regression ui_flutter/maestro/flows
```

## 坑（踩过）

1. **流程里的 `takeScreenshot` 有 4 MB gRPC 上限**：2560×1600 平板一次截图 4.4 MB →
   `RESOURCE_EXHAUSTED`。用 MCP 的 `maestro_take_screenshot`（走 adb 回退，能拿到图），
   或先把分辨率降下来（`adb shell wm size 1600x1000`）再跑带截图的流程。
2. **键盘保护套触控板会持续漂移相机**（fov 自己变），所以断言必须写成**状态**而不是位姿：
   例如「缩小后不允许出现 `驻留` 字样」，而不是「fov 必须等于 25.25°」。
3. **相机是阻尼收敛的**：`tapOn: 放大` 之后要 `extendedWaitUntil` 等状态，别用固定 sleep。
4. 流程依赖调试开关，生产包**没有** `放大/缩小` 按钮；`00_engine_ready` 会显式断言这一点，
   装错包时第一时间报错。
