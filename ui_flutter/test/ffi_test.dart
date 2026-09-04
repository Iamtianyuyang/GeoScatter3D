import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';

void main() {
  test('GeoScatter3D FFI initialize and load dataset test', () {
    final service = GeoScatter3dService();
    
    // 寻找 dll
    final dllCandidates = [
      'gs3d_ffi.dll',
      'ui_flutter/gs3d_ffi.dll',
      '../tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
      'tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
      'D:/code/GeoScatter3D/tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
    ];
    String? foundDll;
    for (final p in dllCandidates) {
      if (File(p).existsSync()) {
        foundDll = p;
        break;
      }
    }
    expect(foundDll, isNotNull, reason: 'gs3d_ffi.dll must exist');

    final inited = service.initialize(foundDll);
    expect(inited, isTrue);

    // 测试 JSON-RPC 通信
    final pingResp = service.executeJsonRpc('{"jsonrpc":"2.0","id":1,"method":"ping"}');
    expect(pingResp, contains('pong'));

    // 测试加载真实 bundle
    final bundleCandidates = [
      '../data/sample-points.gs3d.bundle',
      'data/sample-points.gs3d.bundle',
      'D:/code/GeoScatter3D/data/sample-points.gs3d.bundle',
    ];
    String? foundBundle;
    for (final b in bundleCandidates) {
      if (Directory(b).existsSync()) {
        foundBundle = b;
        break;
      }
    }
    expect(foundBundle, isNotNull, reason: 'sample-points bundle must exist');

    final loaded = service.loadDataset(foundBundle!);
    expect(loaded, isTrue);

    final summary = service.summary;
    expect(summary.isLoaded, isTrue);
    expect(summary.name, equals('sample-points'));
    expect(summary.pointCount, equals(25));
    expect(summary.formattedPointCount, equals('25 点'));
    expect(summary.attributes, contains('field_statics'));
    expect(summary.attributes, contains('elevation'));

    service.shutdown();
  });
}
