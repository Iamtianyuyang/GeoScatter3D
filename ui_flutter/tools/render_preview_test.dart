// 离屏预览生成器（非自动化测试）。
//
// 把欢迎页与三套工作台布局渲染为 PNG，输出到 `tmp/ui-preview/`，
// 用于人工核对 Apple HIG 统一令牌与流体交互的最终观感。
//
// 视口截图走 `RepaintBoundary` 内存回读，不做任何 OS 级截屏（工程红线 R5）。
// 本文件位于 tools/ 而非 test/，因此不参与 `flutter test` 的默认扫描。
//
// 运行：cd ui_flutter && flutter test tools/render_preview_test.dart
import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/main.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/panels/tablet/tablet_drawer_scaffold.dart';

/// 预览画布尺寸（逻辑像素）。
const Size _canvas = Size(1680, 1000);

/// 工作台预览的动画收敛计划：只推进 3 帧。
///
/// 工作台每帧都会驱动原生渲染（帧泵 + 回读），假时钟推进得越长，真实
/// FFI 往返越多。布局模式在构建前就已设定，因此不需要为 `AnimatedSwitcher`
/// 预留过渡时间。
const List<Duration> _workbenchSettle = [
  Duration(milliseconds: 16),
  Duration(milliseconds: 16),
  Duration(milliseconds: 16),
];

late Directory _outDir;
late String _repoRoot;

/// 加载字体，避免 flutter_tester 回退到方块测试字形。
///
/// `flutter_tester` 用的是假字体管理器：任何未注册的字体族都会回退到测试
/// 字形，而该字形把**每个字符**都画成实心方块——截图里会出现成片黑条。
/// 所以凡应用真正会用到的字体族，这里都要显式注册。
Future<void> _loadFonts() async {
  /// 从资源包读取字体字节；资源包不可用时回退到仓库 `assets/fonts/`。
  ///
  /// 图标字体只走资源包：仓库里不再保留它的副本（`uses-material-design`
  /// 会按实际用到的图标裁剪出 `fonts/MaterialIcons-Regular.otf`，
  /// 这才是与发布产物同源的那份）。取不到就直接抛错，避免悄悄退化成方块。
  Future<ByteData> read(String assetKey, String? repoFile) async {
    try {
      return await rootBundle.load(assetKey);
    } on Object {
      if (repoFile == null) rethrow;
      return ByteData.sublistView(
        File(
          '$_repoRoot${Platform.pathSeparator}assets'
          '${Platform.pathSeparator}fonts${Platform.pathSeparator}$repoFile',
        ).readAsBytesSync(),
      );
    }
  }

  Future<void> load(String family, List<(String, String?)> assets) async {
    final loader = FontLoader(family);
    for (final (assetKey, repoFile) in assets) {
      loader.addFont(read(assetKey, repoFile));
    }
    await loader.load();
  }

  // 图标字体：与真实发布产物同源（Flutter 按用到的图标裁剪该字体）。
  await load('MaterialIcons', [('fonts/MaterialIcons-Regular.otf', null)]);

  // 中文正文字体：常规 + 粗体同属一个家族，字重由字体自身 OS/2 表决定，
  // 与 pubspec 中按 weight 400/700 声明的方式一致。
  await load('NotoSansCJKsc', [
    ('assets/fonts/NotoSansCJKsc-Regular.otf', 'NotoSansCJKsc-Regular.otf'),
    ('assets/fonts/NotoSansCJKsc-Bold.otf', 'NotoSansCJKsc-Bold.otf'),
  ]);

  // 等宽族别名：`Gs3dType.mono` 用 Consolas（回退 Cascadia Mono / Menlo /
  // monospace）。这些字体在 flutter_tester 里都不存在，会回退到测试字形——
  // 该字形把每个字符画成实心方块（即截图里那条黑杠）。把中文常规体同时
  // 注册到这两个家族名下，只为让预览可读，不影响真实 Windows 运行效果。
  for (final alias in const ['Consolas', 'monospace']) {
    await load(alias, [
      ('assets/fonts/NotoSansCJKsc-Regular.otf', 'NotoSansCJKsc-Regular.otf'),
    ]);
  }
}

/// 记一行耗时到 `tmp/ui-preview/_timing.log`（立即落盘）。
///
/// `print` 要等用例结束才由测试报告器刷出，而工作台用例一旦卡住就永远等不到——
/// 所以诊断信息必须直接写文件，才能在运行中看到卡在哪一步。
void _timing(String name, String phase, [Stopwatch? watch]) {
  final elapsed = watch == null ? '' : ' ${watch.elapsedMilliseconds}ms';
  final dir = _outDir;
  dir.createSync(recursive: true);
  File('${dir.path}${Platform.pathSeparator}_timing.log').writeAsStringSync(
    '[$name] $phase$elapsed\n',
    mode: FileMode.append,
  );
}

/// 渲染 [app] 并把根 [RepaintBoundary] 的像素写入 `tmp/ui-preview/<name>`。
///
/// [settle] 是收敛动画的推进计划（每一项对应一次 `pump`）。它必须按页面特性
/// 区分：**工作台在每帧都会真正驱动一次原生渲染**，而 `pump(Duration)` 走假
/// 时钟、一次长时间推进会触发帧泵的多次真实 FFI 往返；早期版本对工作台也用
/// `pump(600ms) × 2`（≈75 帧真实渲染），单条用例直接跑到 10 分钟超时。
/// 欢迎页没有原生帧泵，一次大跨度 `pump` 只渲染一帧，因此推进 900ms 反而更省。
Future<void> _capture(
  WidgetTester tester,
  Widget app,
  String name, {
  List<Duration> settle = const [Duration(milliseconds: 900)],
}) async {
  final watch = Stopwatch()..start();
  void mark(String phase) {
    // ignore: avoid_print
    print('  [$name] $phase: ${watch.elapsedMilliseconds}ms');
    _timing(name, phase, watch);
    watch.reset();
  }

  final key = GlobalKey();
  await tester.pumpWidget(RepaintBoundary(key: key, child: app));
  mark('pumpWidget');

  for (final step in settle) {
    await tester.pump(step);
  }
  mark('settle(${settle.length} pumps)');

  final boundary =
      key.currentContext!.findRenderObject()! as RenderRepaintBoundary;

  // **`toImage` / `toByteData` 必须在 `tester.runAsync` 里 await。**
  // 它们是真实光栅化，依赖引擎侧回调；若在测试默认的假时钟区里 await，Future
  // 虽然会正常返回（PNG 也写得出来），但绑定内部的未决状态永远收敛不了——
  // 用例体会跑完、tearDown 也全部执行完，**却始终无法结束**，一直拖到超时。
  // 实测：假时钟区内截屏 → 45s 超时；放进 runAsync → <1s 通过。
  ByteData? data;
  await tester.runAsync(() async {
    final image = await boundary.toImage(pixelRatio: 2.0);
    data = await image.toByteData(format: ui.ImageByteFormat.png);
    image.dispose();
  });
  mark('capture(runAsync)');

  _outDir.createSync(recursive: true);
  final bytes = data!.buffer.asUint8List();
  final file = File('${_outDir.path}${Platform.pathSeparator}$name');
  file.writeAsBytesSync(bytes);
  mark('writePng');
  // ignore: avoid_print
  print('preview written: ${file.path} (${bytes.length} bytes)');
}

/// 创建并初始化一个受 tearDown 托管的服务；[bundle] 非空时装载样例数据。
///
/// **`initialize()` / `loadDataset()` 必须放进 `tester.runAsync`。**
/// 它们会拉起 `Timer.periodic`（指标轮询、拖放轮询、原生帧泵）。若在测试默认的
/// 假时钟区里创建，`AutomatedTestWidgetsFlutterBinding` 会把这些定时器记为
/// 未决（`_verifyInvariants` 断言 `!timersPending`），而 `shutdown()` 也清不
/// 干净——表现为**每条用例都要等满 10 分钟超时才结束**，此时截图其实早已落盘。
/// 放进真实异步区后定时器归属真实 zone，释放即可干净收尾（实测 <1s 跑完）。
///
/// 这与仓库既有约定一致：见 `test/welcome_backend_integration_test.dart`。
Future<GeoScatter3dService> _managedService(
  WidgetTester tester, {
  String? bundle,
  String name = 'service',
}) async {
  final service = GeoScatter3dService.detached();
  addTearDown(() async {
    await tester.runAsync(() async {
      await service.stopNativeRenderer();
      service.shutdown();
    });
  });

  final watch = Stopwatch()..start();
  await tester.runAsync(() async {
    service.initialize();
    _timing(name, 'initialize', watch);
    watch.reset();
    // `data/sample-points.gs3d.bundle` 是目录（bundle 形态），不能用
    // `File.existsSync` 判断，否则会静默跳过装载、预览里看不到真实数据。
    if (bundle != null &&
        FileSystemEntity.typeSync(bundle) != FileSystemEntityType.notFound) {
      service.loadDataset(bundle, activate: false);
      _timing(name, 'loadDataset', watch);
    } else if (bundle != null) {
      _timing(name, 'WARNING bundle 不存在: $bundle');
    }
  });
  return service;
}

void main() {
  // 允许从 `ui_flutter/` 或仓库根目录两种方式启动。
  final cwd = Directory.current;
  _repoRoot =
      cwd.path.endsWith('ui_flutter') ? cwd.parent.path : cwd.path;
  _outDir = Directory(
    '$_repoRoot${Platform.pathSeparator}tmp'
    '${Platform.pathSeparator}ui-preview',
  );

  setUpAll(() async {
    TestWidgetsFlutterBinding.ensureInitialized();
    await _loadFonts();
  });

  testWidgets('欢迎页预览', (tester) async {
    tester.view.physicalSize = _canvas;
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    final service = await _managedService(tester, name: '01-welcome.png');
    await _capture(
      tester,
      GeoScatter3dApp(service: service),
      '01-welcome.png',
    );
  });

  testWidgets('标准工作台预览', (tester) async {
    tester.view.physicalSize = _canvas;
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    final service = await _managedService(
      tester,
      bundle: '$_repoRoot/data/sample-points.gs3d.bundle',
      name: '02-workbench-standard.png',
    );
    service
      ..openWorkbench()
      ..setLayoutMode(WorkbenchLayoutMode.standard);

    await _capture(
      tester,
      GeoScatter3dApp(service: service),
      '02-workbench-standard.png',
      settle: _workbenchSettle,
    );
  });

  testWidgets('悬浮胶囊 Dock 预览', (tester) async {
    tester.view.physicalSize = _canvas;
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    final service = await _managedService(
      tester,
      bundle: '$_repoRoot/data/sample-points.gs3d.bundle',
      name: '03-workbench-floating-dock.png',
    );
    service
      ..openWorkbench()
      ..setLayoutMode(WorkbenchLayoutMode.floatingDock);

    await _capture(
      tester,
      GeoScatter3dApp(service: service),
      '03-workbench-floating-dock.png',
      settle: _workbenchSettle,
    );
  });

  testWidgets('暗色分析舱预览', (tester) async {
    tester.view.physicalSize = _canvas;
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    final service = await _managedService(
      tester,
      bundle: '$_repoRoot/data/sample-points.gs3d.bundle',
      name: '04-workbench-analysis-rail.png',
    );
    service
      ..openWorkbench()
      ..setLayoutMode(WorkbenchLayoutMode.analysisRail);

    await _capture(
      tester,
      GeoScatter3dApp(service: service),
      '04-workbench-analysis-rail.png',
      settle: _workbenchSettle,
    );
  });

  // ==========================================================================
  // 平板预览（Pixel_10_Pro_Fold 内屏竖向基准 1192×820）。
  // 断点由 Gs3dFormFactorScope 从 MediaQuery 派生：1192 宽命中 tablet 档，
  // 呈现抽屉（收起）+ 触控顶栏 + 底部拇指动作条形态。
  // ==========================================================================
  const Size tabletCanvas = Size(1192, 820);

  testWidgets('平板预览', (tester) async {
    tester.view.physicalSize = tabletCanvas;
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    final service = await _managedService(
      tester,
      bundle: '$_repoRoot/data/sample-points.gs3d.bundle',
      name: '05-tablet.png',
    );
    // 预览要呈现工作台本体，跳过首次进入的手势引导卡遮罩。
    TabletDrawerScaffold.suppressGestureGuide();
    service
      ..openWorkbench()
      ..setLayoutMode(WorkbenchLayoutMode.standard);

    await _capture(
      tester,
      GeoScatter3dApp(service: service),
      '05-tablet.png',
      settle: _workbenchSettle,
    );
  });

  testWidgets('平板欢迎页预览', (tester) async {
    tester.view.physicalSize = tabletCanvas;
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    final service = await _managedService(tester, name: '06-tablet-welcome.png');

    await _capture(
      tester,
      GeoScatter3dApp(service: service),
      '06-tablet-welcome.png',
    );
  });
}
