// 实机链路：打开 sample → 启动原生渲染 → 停止 → 再打开（曾因 Isolate 捕获 this 崩溃）。
// 设置 GS3D_ENGINE_E2E=1 启用。必须走单进程 engine，不得静默落到双进程。
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('open → start → stop → re-open sample uses single-process engine', () async {
    if (Platform.environment['GS3D_ENGINE_E2E'] != '1') {
      markTestSkipped('Set GS3D_ENGINE_E2E=1');
      return;
    }
    if (!Platform.isWindows) {
      markTestSkipped('Windows only');
      return;
    }

    final root = Directory.current.parent.path;
    final bundle =
        '$root${Platform.pathSeparator}data${Platform.pathSeparator}sample-points.gs3d.bundle';
    expect(Directory(bundle).existsSync(), isTrue, reason: bundle);

    final service = GeoScatter3dService.detached();
    expect(service.initialize(), isTrue, reason: 'initialize failed');

    // 第一轮：打开并启动（对应欢迎页「打开工程」）
    expect(service.loadDataset(bundle, activate: false), isTrue,
        reason: 'first loadDataset failed');
    final first = await service.startNativeRenderer();
    expect(first, isTrue,
        reason: 'first startNativeRenderer failed: ${service.backendStatus}');
    expect(service.isEngineNative, isTrue,
        reason: 'must use single-process engine, got dual-process. '
            'err=${service.backendStatus}');
    expect(service.isNativeRendererActive, isTrue);

    // 第二轮：再次打开同工程（曾在此处 stopNativeRenderer → Isolate 崩溃）
    final second = await service.executeActionAsync(
      'welcome.open_project',
      {'path': bundle},
    );
    expect(second['success'], isTrue,
        reason: 're-open failed: ${second['message']} / ${service.backendStatus}');
    expect(service.isEngineNative, isTrue,
        reason: 're-open must stay on engine. err=${service.backendStatus}');
    expect(service.isNativeRendererActive, isTrue,
        reason: 'renderer not active after re-open');

    // 再次 stop/start，确认 destroy 路径可重复
    await service.stopNativeRenderer();
    expect(service.isNativeRendererActive, isFalse);
    final third = await service.startNativeRenderer();
    expect(third, isTrue,
        reason: 'start after stop failed: ${service.backendStatus}');
    expect(service.isEngineNative, isTrue);

    await service.stopNativeRenderer();
  }, timeout: const Timeout(Duration(minutes: 2)));
}
