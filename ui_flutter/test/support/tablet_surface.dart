import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/main.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/theme/gs3d_form_factor.dart';

/// 平板 surface 测试辅助。
///
/// 基准（总监裁决）：Pixel_10_Pro_Fold 内屏 + iPad 类 1192×820 逻辑分辨率。
/// pump 前设置 `tester.view.physicalSize / devicePixelRatio`，测试结束后
/// 自行复位（R4：不留环境脏状态）。
class TabletSurface {
  TabletSurface._();

  /// 折叠屏内屏竖向（主战场，tablet 档）。
  static const Size foldInnerPortrait = Size(1192, 820);

  /// 折叠屏内屏横向（大屏，expanded 档）。
  static const Size foldInnerLandscape = Size(2208, 1840);

  /// 手机基准（compact 档，仅档位回归验证）。
  static const Size phoneCompact = Size(500, 800);

  /// 设定测试 surface。
  static void apply(WidgetTester tester, Size surface) {
    tester.view.physicalSize = surface;
    tester.view.devicePixelRatio = 1.0;
  }

  /// 装配平板形态的全应用（复用 welcome_ui_test_service 的假 service 模式）。
  ///
  /// [service] 建议由调用方先 `markEngineUnavailable(...)` 预设引擎降级。
  static Future<void> pumpTabletApp(
    WidgetTester tester,
    GeoScatter3dService service, {
    Size surface = foldInnerPortrait,
  }) async {
    apply(tester, surface);
    addTearDown(() {
      tester.view.resetPhysicalSize();
      tester.view.resetDevicePixelRatio();
    });
    await tester.pumpWidget(GeoScatter3dApp(service: service));
    await tester.pump(const Duration(milliseconds: 120));
  }

  /// 断点推导单元断言辅助。
  static Gs3dFormFactorSlot slotForWidth(double width) =>
      Gs3dFormFactorScope.resolve(
        MediaQueryData(size: Size(width, 800)),
      );
}
