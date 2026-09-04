import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/main.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';

void main() {
  testWidgets('GeoScatter3dApp workbench UI smoke test', (WidgetTester tester) async {
    final service = GeoScatter3dService();

    // 桌面工作台标准测试分辨率
    tester.view.physicalSize = const Size(1920, 1080);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(() {
      tester.view.resetPhysicalSize();
      tester.view.resetDevicePixelRatio();
    });

    await tester.pumpWidget(GeoScatter3dApp(service: service));

    // 验证核心组件均成功渲染
    expect(find.textContaining('GeoScatter', findRichText: true), findsOneWidget);
    expect(find.textContaining('3D', findRichText: true), findsOneWidget);
    expect(find.text('项目'), findsOneWidget);
    expect(find.text('测量'), findsOneWidget);
    expect(find.text('区域统计'), findsOneWidget);
    expect(find.text('场景'), findsOneWidget);
    expect(find.text('细节层级'), findsOneWidget);
    expect(find.text('属性'), findsAtLeastNWidgets(1));
    expect(find.text('导航图'), findsOneWidget);
  });
}
