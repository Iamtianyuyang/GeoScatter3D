# GeoScatter3D 平板适配迭代 — QA 测试报告

> QA 工程师：严过关（Edward）｜日期：2026-09-11
> 被测物：工程师寇豆码「平板适配 + 核心功能接入」实现（`ui_flutter/` 全部未提交改动）
> 验收依据：`tablet-prd.md`（许清楚）＋ `tablet-architecture.md`（高见远）

---

## 1. 总结（Summary）

| 门禁项 | 结果 |
| --- | --- |
| `flutter analyze` | ✅ **0 告警**（No issues found!） |
| `flutter test`（全量回归） | ✅ **81 通过 / 3 跳过 / 0 失败**（原 71 通过 + 本轮 QA 补强 10 条；3 跳过为既有引擎依赖用例，需 Windows DLL 环境） |
| 离屏预览 `tools/render_preview_test.dart` | ✅ 6/6 通过，`tmp/ui-preview/05-tablet.png`、`06-tablet-welcome.png` 已人工核对（平板工作台含边缘把手/底部动作条/命令按钮；欢迎页无横向溢出） |
| 平板模拟器实测（安装/启动/走查） | ⚠️ **未完成**——`flutter build apk --debug` 在本沙箱 2 次尝试均失败（详见 §4），已按预案转入备选验证链 |
| 补测文件 | 新增 `test/tablet_qa_supplement_test.dart`（10 条，全绿），未改动任何源码 |

**路由判定：Engineer（1 项源码问题 KB-01，待工程师修复）**；其余全部通过，无测试代码遗留问题。

---

## 2. 重点专项测试结果（任务 §2 逐项）

### a) 断点三档边界值 ✅
- `Gs3dFormFactorScope.resolve`（单一事实源）边界扫描：**599 → compact，600 → tablet，1279 → tablet，1280 → expanded，1281 → expanded**，另回归 500/800/1192/2208 基准，全部正确。边界语义与常量注释一致（600 含于 tablet、1280 含于 expanded）。

### b) 触控目标 ≥48 ⚠️（1 项遗留：KB-01）
在 tablet 档（1192×820）实测命中区（宽与高均断言 ≥48）：

| 控件 | 结果 |
| --- | --- |
| 底部动作条全部 11 个按钮（复位/放大/缩小/平移/测距/统计/主题/截图/布局/命令/快捷键） | ✅ 全部 ≥48×48 |
| 左抽屉关闭按钮 | ✅ ≥48×48 |
| 顶栏常驻「命令」按钮 | ✅ ≥48×48 |
| **视口浮层工具条**（复位视角/平移模式/地图轴/世界轴/十字准线/标尺测距/视角/±缩放/截图/命令/快捷键/性能 + 右下角 Gizmo「顶/前/侧/等」预设标签） | ❌ **仅 28px 高（KB-01，见 §3）** |

### c) 引擎不可用显式降级（R3）✅
- **真实 service 失败路径直测**（非假 service 预设）：`GeoScatter3dService.detached().initialize(非法路径)` 强制 `DynamicLibrary.open` 抛异常 → `engineAvailability == unavailable`、`initializationError` 非空、`backendStatus` 含「渲染内核在本平台不可用」、`nativeFrameImage == null` 且 `isNativeRendererActive == false`（**绝无 Dart 假视口**）。
- `executeAction` R3 前置 guard：对 `engineDependentActionPrefixes` **全部 13 个前缀逐一探测**，均返回 `success:false` + 显式降级文案；非引擎依赖动作（命令面板）不受影响照常可达。
- 视口区在 unavailable 时渲染 `ViewportEngineErrorCard`（key `workbench.viewport.engine_error_card`，含原因与回桌面版引导），无任何 Dart 点云替代物。
- Windows 有 DLL 路径回归零变化（既有 `ffi_test` / `engine_e2e` / `action_coverage` 真后端用例照常）。

### d) 视口多指手势状态机 ✅
WidgetTester 多指针序列实测（`center_viewport.dart` 裸 Listener 路径）：
- 单指拖拽 → `cameraAzimuth` 旋转增量 ✅
- 双指同向拖动 → `cameraPanX/PanY` 平移增量，方位角不变 ✅
- 双指外扩捏合 → `cameraZoom` 放大增量 ✅
- **第三指按下不崩溃**，三指在场移动无异常，全部抬指后单指拖拽恢复旋转（`_dragPointer` 正确重绑，状态复位）✅
- **双指抬一指 → 剩余一指接续单指旋转**（手势不断流，仰角变化验证）✅
- 长按视口 → 上下文菜单（等价右键），菜单项经 `executeAction` 路由 ✅

### e) 45 项动作平板档可达性 ✅
- `action_coverage_test.dart` tablet surface（1192×820）复跑：目录 ≥45 项动作 **100% 有路由分支**（无「未知的动作 ID」、无异常、返回结构合规）。
- 引擎依赖类：可达且显式降级反馈 100%（无静默假成功）；纯 UI 类（quick_demo / 布局切换 / 命令面板 / 抽屉开合）真实执行通过。
- 桌面专属布局入口守卫：tablet 档切 floatingDock 出横幅并回退触控工作台 ✅。

### 补充：响应式对话框档位路由 ✅
- tablet 档（1192×820）→ 底部 sheet（`BottomSheet`，拖拽把手）✅；expanded 档（2208×1840）→ 居中模态（无 BottomSheet）✅。

---

## 3. 路由判定与问题清单

### → Engineer（源码 Bug，待工程师修复）

**KB-01｜视口浮层工具条与 Gizmo 预设标签触控命中区未达标（PRD §5.1 P0「平板档位下全部可交互控件命中区 ≥48」）**
- 位置：`lib/src/panels/center_viewport.dart` — `_buildPillButton` / `_buildIconPill` / `_buildPillToggleButton`（Container 高 28，约 L1389–1483）；`_buildGizmoPresetTag`（约 L1226–1247，padding 5/2、字号 10）。
- 复现：tablet 档（1192×820）进入工作台 → 量测 `WorkbenchUiKeys.viewportResetButton` 高度 = 28 < 48（补测文件 KB-01 记录用例可自动复现）。
- 影响：视口浮层工具条在平板上触控困难；缓解因素：底部动作条已冗余覆盖多数同类动作（复位/平移/测距/截图/命令），故**不阻断本迭代门禁**，建议下迭代经 `Gs3dTouchTarget`（isTouchPreferred 档）包裹修复；修复后请翻转 `tablet_qa_supplement_test.dart` 中 KB-01 记录用例的断言方向。
- 附带观察（非 Bug）：`tools/render_preview_test.dart` 的 05-tablet.png 中视口显示「gs3d_engine 未就绪」文案而非 R3 错误卡片——预览工具未预设 `markEngineUnavailable` 所致，产品代码行为已由用例验证正确；建议预览工具补预设以还原真实模拟器观感。

### → QA（已自行修复，无遗留）
- 补测文件首跑 2 处测试代码问题：① 缺 `TabletDrawerScaffold` import；② expanded 档用 `find.byType(Dialog)` 断言居中模态——经最小探针验证本 Flutter 版本 `showDialog` 路由内部不含 `Dialog` 类型 widget（内容已弹出），改用「内容出现 + 无 BottomSheet」断言。已修复并复跑通过。

### → NoOne（其余全部通过）
断点边界 / 手势状态机 / R3 降级 / 45 项动作路由 / 响应式对话框 / 既有 71 用例回归 —— 全绿。

---

## 4. 平板模拟器实测（Pixel_10_Pro_Fold）

| # | 清单项 | 结果 | 说明 |
| --- | --- | --- | --- |
| 0 | 模拟器启动 | ✅ | `emulator -avd Pixel_10_Pro_Fold -no-snapshot -no-boot-anim`，`adb devices` 见 `emulator-5554 device`，`sys.boot_completed=1` |
| 1 | 构建 debug APK | ❌ | 2 次尝试均失败（见下），按约定封顶转备选链 |
| 2 | 安装/冷启动/5 视图导航/命令面板/sheet 对话框/错误卡可见/姿态变化 | ⏸ 未执行 | 依赖 #1 的 APK；相应交互已在 widget test 层等价验证（见 §2） |

**构建失败记录（如实）**：
- 尝试 1：`flutter build apk --debug` 挂起 >41 分钟无任何进度输出，人工终止。
- 尝试 2（限时 600s、落盘 `tmp/apk-build-attempt2.log`）：gradle daemon 正常启动（`Gradle 9.1.0 / JBR java 25`），执行 `assembleDebug` 中被超时截断，报错原文：
  > `FAILURE: Build failed with an exception. * What went wrong: Gradle build daemon disappeared unexpectedly (it may have been killed or may have crashed)` — `Gradle task assembleDebug failed with exit code 1`（运行 594.0s）

- 判断：非工程代码问题（gradle/Java 链路本身能启动），为**沙箱内依赖解析/网络受限导致的构建停滞**；未超过 2 次重试上限，依约停止。

**补做步骤**（脱离沙箱后）：
1. `flutter build apk --debug`（首次允许 gradle 完成依赖下载，建议 ≥30 分钟或预热 `--parallel`）；
2. `adb install -r build/app/outputs/flutter-apk/app-debug.apk`；
3. `adb shell am start ...` 冷启动 → 按 §4 清单走查（欢迎页 / rail 5 视图 / 命令面板 / sheet / R3 错误卡 / `wm size` 姿态切换）；
4. `adb exec-out screencap -p > tmp/ui-preview/emulator-*.png` 留证（OS 级截屏仅用于 UX 取证，不违反 R5——R5 约束产品内截屏特性）；
5. 收尾 `adb emu kill`。

**备选验证链结论（本迭代采信）**：`flutter analyze` 0 告警 + `flutter test` 81/3 全绿（含 1192×820 / 2208×1840 / 500×800 surface）+ `05/06-tablet` 预览图人工核对通过。模拟器 UX 走查与真实触控手感（跟手性、惯性）属未覆盖残留风险，需按上述步骤补做。

---

## 5. 红线合规抽查

- **R1/R2**：动作条/顶栏/长按菜单全部经 `service.executeAction`（读码确认，无绕过控制面的直接 setter 调用）。
- **R3**：失败路径直测 + 13 前缀 guard 全量探测 + 错误卡渲染，无 Dart 假视口。
- **R4**：本轮产物仅 `tmp/`（预览图、构建日志、补测探针已自清理）；未 git commit。
- **R5**：未触碰产品截屏特性；模拟器 screencap 未执行（无 APK 可装）。
- 零 C++/CMake 改动；未跑 `flutter build windows`。

---

## 6. 第 2 轮回归（KB-01 修复验证）

> 日期：2026-09-11｜QA：严过关｜范围：聚焦 KB-01 回归验证，未扩大范围

### 6.1 门禁复跑（独立执行）

| 门禁项 | 结果 |
| --- | --- |
| `flutter analyze` | ✅ **0 告警**（No issues found!） |
| `flutter test`（全量） | ✅ **82 通过 / 3 跳过 / 0 失败**（较第 1 轮 +1：新增 Gizmo 标签用例；3 跳过仍为既有引擎依赖用例） |

### 6.2 KB-01 修复质量复核（读码 + 用例）

修复方式：`center_viewport.dart` 新增 `_touchHitArea` 助手（约 L1232–1238），`context.isTouchPreferred` 时以 `Gs3dTouchTarget(minSize: 48)` 包裹视觉容器，否则原样返回。

| 检查项 | 结论 |
| --- | --- |
| 浮层工具条 5 处触控档命中区 ≥48 | ✅ `_buildPillButton` / `_buildIconPill` / `_buildPillToggleButton` / 预设视角下拉（PopupMenuButton child）均经 `_touchHitArea` 包裹；`tablet_qa_supplement_test.dart` KB-01 两条用例实测 `viewportResetButton` / `viewportZoomInButton` 宽高均 ≥48 |
| Gizmo 预设标签命中区 ≥48 | ✅ `_buildGizmoPresetTag` 经 `_touchHitArea` 包裹，新增用例量测「顶」标签外包 Gs3dTouchTarget ≥48×48，通过 |
| **expanded 档保持 28、视觉零变化** | ✅ **读码确认**：`_touchHitArea` 首行 `if (!context.isTouchPreferred) return visual;` 早退——expanded 档不进入 `Gs3dTouchTarget` 分支，Container 仍为高 28 的原始视觉树，包裹器不出现在 widget 树中，桌面像素级零变化；且 `isTouchPreferred => slot != expanded`（`gs3d_form_factor.dart` L34），判断与断点中枢同源，无重复口径 |
| 档位判断与断点中枢一致 | ✅ compact/tablet → true（≥48），expanded → false（原样 28），单一事实源 |
| 未绕过 executeAction | ✅ 包裹器仅改命中区布局，全部 onPressed/onTap 仍走既有 service.executeAction / service 方法路由，无直接 setter 绕过 |
| 未触碰 service / 无 OS 截屏 | ✅ 改动仅限 `center_viewport.dart` UI 层 + 测试文件，`ffi/` service 层零改动，无截屏相关代码 |

### 6.3 专项回归抽查（上一轮已绿项）

- 断点边界（599/600/1279/1280/1281）✅、R3 引擎失败显式降级（真实 service 路径 + 13 前缀 guard）✅、多指手势状态机（第三指/抬一指接续/状态复位）✅、45 项动作平板档覆盖 ✅ —— 全量回归均未回退。

### 6.4 最终判定

- **路由判定：NoOne（全绿，可交付）**。
- KB-01 修复达标：触控档命中区 ≥48、expanded 档零变化，无新增问题，无遗留问题。
- 承接第 1 轮已知残留（非本轮范围，不影响门禁）：平板模拟器 APK 构建/UX 走查仍待脱离沙箱后按 §4 步骤补做；`tools/render_preview_test.dart` 未预设 `markEngineUnavailable` 为预览工具观感问题。
