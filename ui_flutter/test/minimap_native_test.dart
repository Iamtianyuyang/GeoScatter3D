// 导航图：真实原生缩略图必须可用（返回码 0）。
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('refreshMinimap uses native thumbnail (rc==0) after load', () async {
    if (!Platform.isWindows) {
      markTestSkipped('Windows only');
      return;
    }
    final root = Directory.current.parent.path;
    final source =
        '$root${Platform.pathSeparator}data${Platform.pathSeparator}sample-points.gs3d.bundle${Platform.pathSeparator}source.gs3d';
    expect(File(source).existsSync(), isTrue, reason: source);

    final service = GeoScatter3dService.detached();
    expect(service.initialize(), isTrue);
    expect(service.loadDataset(source, activate: false), isTrue);

    final bytes = await service.refreshMinimap(width: 220, height: 160);
    expect(bytes, isNotNull);
    expect(bytes!.length, 220 * 160 * 4);
    expect(service.minimapImage, isNotNull, reason: 'decoded minimap image');

    // 原生路径应产生非纯背景色像素（点云投影着色）
    var colored = 0;
    for (var i = 0; i < bytes.length; i += 4) {
      // 背景是深蓝 (11,19,32)；着色点会明显偏离
      final dr = (bytes[i] - 11).abs();
      final dg = (bytes[i + 1] - 19).abs();
      final db = (bytes[i + 2] - 32).abs();
      if (dr + dg + db > 40) colored++;
    }
    expect(colored, greaterThan(0),
        reason: 'nav map should contain colored point pixels, not empty dark field');
  });
}
