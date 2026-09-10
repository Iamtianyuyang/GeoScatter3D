import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';

void main() {
  group('C-ABI FFI unified channel', () {
    late GeoScatter3dService service;

    setUp(() {
      service = GeoScatter3dService();
      service.initialize();
      service.openWorkbench();
    });

    test('executeAction dispatches without native viewer connected', () {
      // 未连接 Viewer 时类型化 FFI 应安全失败，不抛异常。
      expect(service.executeAction('workbench.theme.set', {'index': 1})['success'], isTrue);
      expect(service.executeAction('workbench.camera.link', {'linked': true})['success'], isTrue);
      expect(service.executeAction('workbench.view.new')['success'], isTrue);
      expect(service.executeAction('workbench.workspace.restore')['success'], isTrue);
      expect(
        service.executeAction('workbench.panel.toggle', {
          'panel': 'panel.performance',
          'visible': true,
        })['success'],
        isTrue,
      );
      expect(service.executeAction('workbench.camera.reset')['success'], isTrue);
      expect(service.executeAction('workbench.cache.clear')['success'], isTrue);
      expect(service.executeAction('workbench.gizmo.axis', {'axis': 4})['success'], isTrue);
      expect(service.executeAction('workbench.screenshot.native')['success'], isTrue);
      expect(service.executeAction('workbench.diagnostics.refresh')['success'], isTrue);
    });

    test('hasViewerBridge reflects DLL capability when loadable', () {
      // 测试环境下 DLL 可能不存在；仅验证 getter 不崩溃。
      final client = service.backendStatus;
      expect(client, isNotEmpty);
    });

    test('render settings dual-path keeps local state', () {
      final res = service.executeAction('workbench.render.set_point_size', {
        'size': 2.5,
      });
      expect(res['success'], isTrue);
      expect(service.pointSize, closeTo(2.5, 0.001));

      final cm = service.executeAction('workbench.render.set_colormap', {
        'colormap': 'turbo',
      });
      expect(cm['success'], isTrue);
      expect(service.colormap.toLowerCase(), equals('turbo'));
    });

    test('export action accepts path without viewer', () {
      final dir = Directory(
        '${Directory.current.path}${Platform.pathSeparator}tmp',
      );
      if (!dir.existsSync()) dir.createSync(recursive: true);
      final path =
          '${dir.path}${Platform.pathSeparator}ffi_channel_export_test.ply';
      final res = service.executeAction('workbench.export', {
        'path': path,
        'format': 'ply',
      });
      // 无数据集时可能失败，但不得抛异常。
      expect(res, contains('success'));
      try {
        File(path).deleteSync();
      } catch (_) {}
    });
  });
}
