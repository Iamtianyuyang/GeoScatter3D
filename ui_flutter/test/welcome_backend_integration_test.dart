import 'dart:io';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/welcome/new_project_dialog.dart';
import 'package:ui_flutter/src/welcome/welcome_ui_keys.dart';

// 由 scripts/test_welcome_backend.ps1 显式启用，使用真实 DLL、预处理器和 Vulkan。
void main() {
  testWidgets('welcome creates a real bundle and waits for a native frame', (
    tester,
  ) async {
    final service = GeoScatter3dService.detached();
    tester.view.physicalSize = const Size(1280, 1000);
    tester.view.devicePixelRatio = 1;
    addTearDown(() async {
      await service.stopNativeRenderer();
      service.shutdown();
      tester.view.resetPhysicalSize();
      tester.view.resetDevicePixelRatio();
    });
    await tester.runAsync(() async {
      expect(service.initialize(), isTrue);
    });
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(body: NewProjectDialog(service: service)),
      ),
    );
    await tester.enterText(
      find.byKey(WelcomeUiKeys.newProjectDialogNameInput),
      '欢迎工程 integration',
    );
    await tester.runAsync(() async {
      await tester.tap(find.byKey(WelcomeUiKeys.newProjectDialogSubmitButton));
      final deadline = DateTime.now().add(const Duration(seconds: 60));
      while (service.welcomeBusy && DateTime.now().isBefore(deadline)) {
        await Future<void>.delayed(const Duration(milliseconds: 50));
      }
      expect(service.welcomeError, isNull);
      expect(service.welcomeBusy, isFalse);
      expect(service.isWorkbenchActive, isTrue);
      expect(service.summary.pointCount, 25);
      await service.stopNativeRenderer();
      service.closeWorkbench();
      final dat = File('${GeoScatter3dService.projectsDirectory}/原始数据.dat');
      await dat.writeAsString(
        'X Y elevation field_statics\n0 0 2 10\n1 1 3 20\n2 2 4 30\n',
      );
      final response =
          jsonDecode(
                await service.executeJsonRpcAsync(
                  jsonEncode({
                    'jsonrpc': '2.0',
                    'id': 9,
                    'method': 'ui_action',
                    'params': {
                      'action': 'welcome.new_project',
                      'params': {
                        'path': dat.path,
                        'name': 'dat-build',
                        'threads': 3,
                        'voxel_mode': 'xy',
                        'finest_target_points': 10000,
                      },
                    },
                  }),
                ),
              )
              as Map;
      expect(response['id'], 9);
      expect(
        response['result']['success'],
        isTrue,
        reason: '${response['result']}',
      );
      expect(service.summary.pointCount, 3);
      expect(service.nativeViewportPng, isNotEmpty);
      expect(service.gpus, isNotEmpty);
      expect(service.setPreferredGpu(0), isTrue);
      expect(
        await File(
          '${Platform.environment['GS3D_USER_CONFIG_DIR']}/preferences.toml',
        ).readAsString(),
        contains('uuid:'),
      );
      expect(service.isNativeRendererActive, isTrue);
      expect(service.nativeSnapshot['ready'], isTrue);
      expect(service.nativeViewportPng!.take(8), [
        137,
        80,
        78,
        71,
        13,
        10,
        26,
        10,
      ]);
      final bundle =
          '${GeoScatter3dService.projectsDirectory}/欢迎工程 integration.gs3d.bundle';
      final manifest = await File('$bundle/manifest.toml').readAsString();
      expect(manifest, contains('num_threads = 16'));
      expect(manifest, contains('voxel_mode = "XYZ"'));
      expect(manifest, contains('finest_target_points = 50000'));
      await service.stopNativeRenderer();
      service.closeWorkbench();
      final reopened = await service.executeActionAsync('welcome.recent.open', {
        'path': bundle,
      });
      expect(reopened['success'], isTrue, reason: '${reopened['message']}');
      expect(service.nativeViewportPng, isNotEmpty);
      await service.stopNativeRenderer();
      service.closeWorkbench();
      final bad = await service.executeActionAsync('welcome.open_project', {
        'path': '$bundle/missing.gs3d',
      });
      expect(bad['success'], isFalse);
      expect(service.isWorkbenchActive, isFalse);
      final conflict = await service.executeActionAsync('welcome.new_project', {
        'path': 'examples/sample-points.csv',
        'name': '欢迎工程 integration',
      });
      expect(conflict['success'], isFalse);
      expect(await File('$bundle/manifest.toml').readAsString(), manifest);
      final cancelledBuild = service.executeActionAsync('welcome.new_project', {
        'path': 'examples/sample-points.csv',
        'name': 'cancelled-build',
      });
      final duplicate = await service.executeActionAsync('welcome.quick_demo');
      expect(duplicate['success'], isFalse);
      service.cancelPreprocess();
      expect((await cancelledBuild)['success'], isFalse);
      expect(service.isPreprocessing, isFalse);
      expect(
        await Directory(
          '${GeoScatter3dService.projectsDirectory}/cancelled-build.gs3d.bundle',
        ).exists(),
        isFalse,
      );
      final corrupt = File(
        '${GeoScatter3dService.projectsDirectory}/broken.gs3d',
      );
      await corrupt.writeAsBytes([0, 1, 2, 3]);
      final invalid = await service.executeActionAsync('welcome.open_project', {
        'path': corrupt.path,
      });
      expect(invalid['success'], isFalse);
      expect(service.isWorkbenchActive, isFalse);
      final single = await service.executeActionAsync('welcome.open_project', {
        'path': '$bundle/source.gs3d',
      });
      expect(single['success'], isTrue, reason: '${single['message']}');
      await service.stopNativeRenderer();
      service.closeWorkbench();
      final demo = await service.executeActionAsync('welcome.quick_demo');
      expect(demo['success'], isTrue, reason: '${demo['message']}');
      expect(service.summary.pointCount, 25);
    });
    await tester.pumpWidget(const SizedBox.shrink());
  }, skip: Platform.environment['GS3D_NATIVE_INTEGRATION'] != '1');
}
