/// GeoScatter3D 形态因子（Form Factor）断点中枢。
///
/// 本文件是**唯一**的响应式档位判定来源：
///   1. 任何组件需要档位 → `context.gs3dFormFactor`（由 [Gs3dFormFactorScope]
///      在 `MaterialApp.builder` 与既有 `Gs3dA11yScope` 并排注入）。
///   2. **禁止**在业务组件里散落 `MediaQuery.of(context).size.width < 600`
///      之类的阈值判断（架构红线：档位判定只许一处）。
///   3. 无 scope 的上下文（独立测试 / 直接 pump 组件）安全回退到
///      [Gs3dFormFactorSlot.expanded]，保证桌面既有行为零变化。
///
/// 平板基准（总监裁决）：Pixel_10_Pro_Fold 内屏 1192×820 命中 tablet 档，
/// iPad 类 2208×1840 横向命中 expanded 档。
///
/// P1 预留（折叠分屏不堵死）：档位推导收敛在 [Gs3dFormFactorScope.resolve]
/// 这一个工厂函数里（入参 = MediaQuery 数据）。下迭代做折叠分屏时，只需在
/// 该函数里叠加 `MediaQuery.displayFeatures`（铰链区判定）扩展出
/// `foldHalfOpen` 档，所有消费方无需改任何布局组件签名。
library;

import 'package:flutter/material.dart';

/// 形态因子档位。
enum Gs3dFormFactorSlot {
  /// 窄屏（手机）：现有桌面布局退化为窄屏堆叠（本阶段仅回归验证）。
  compact,

  /// 平板：本阶段主战场——抽屉 + 触控工具条 + 底部拇指动作条。
  tablet,

  /// 桌面 / 大屏横向平板：三套布局照旧（键鼠优先）。
  expanded;

  /// 触控优先：compact / tablet 档一律按触控命中区（≥48）与手势范式组织。
  bool get isTouchPreferred => this != Gs3dFormFactorSlot.expanded;
}

/// 形态因子尺寸常量（Gs3d 命名空间，视觉常量仍归单一令牌源体系）。
///
/// 抽屉宽度 / 把手宽度 / 顶栏高度在此集中，避免平板布局文件散落字面量。
abstract final class Gs3dFormFactorDims {
  const Gs3dFormFactorDims._();

  /// compact 档上界（不含）。
  static const double breakpointCompactMax = 600.0;

  /// tablet 档上界（不含）；≥ 1280 归 expanded。
  ///
  /// 取值依据（总监裁决）：平板基准 Pixel_10_Pro_Fold 内屏 1192×820 必须命中
  /// tablet 档，故上界须 > 1192；iPad 类 2208×1840 与 ≥1400 桌面窗口归
  /// expanded。取 1280 兜住 1192 基准并留出余量。
  static const double breakpointTabletMax = 1280.0;

  /// 平板抽屉宽度区间下限。
  static const double drawerWidthMin = 320.0;

  /// 平板抽屉宽度区间上限。
  static const double drawerWidthMax = 400.0;

  /// 平板抽屉实际采用宽度（取区间中值，可容纳 320 左面板 / 290 右面板）。
  static const double tabletDrawerWidth = 360.0;

  /// 平板左右边缘把手宽度（收起态不占布局位）。
  static const double drawerHandleWidth = 24.0;
}

/// 形态因子注入 Widget，与 `Gs3dA11yScope` 同构。
class Gs3dFormFactorScope extends InheritedWidget {
  const Gs3dFormFactorScope({
    super.key,
    required this.slot,
    required super.child,
  });

  /// 当前档位。
  final Gs3dFormFactorSlot slot;

  /// 是否触控优先（compact / tablet）。
  bool get isTouchPreferred => slot.isTouchPreferred;

  /// 档位推导唯一入口（入参 = MediaQuery 数据）。
  ///
  /// 断点依据窗口宽度：`< 600` → compact；`600–1280` → tablet（含 1192
  /// 平板基准）；`≥ 1280` → expanded。P1 折叠分屏时在此叠加 displayFeatures
  /// 判定即可。
  static Gs3dFormFactorSlot resolve(MediaQueryData mq) {
    final width = mq.size.width;
    if (width < Gs3dFormFactorDims.breakpointCompactMax) {
      return Gs3dFormFactorSlot.compact;
    }
    if (width < Gs3dFormFactorDims.breakpointTabletMax) {
      return Gs3dFormFactorSlot.tablet;
    }
    return Gs3dFormFactorSlot.expanded;
  }

  static Gs3dFormFactorScope of(BuildContext context) =>
      maybeOf(context) ?? const Gs3dFormFactorScope._fallback();

  /// 允许在无 scope 的上下文（独立组件测试 / 直接 pump）中安全读取；
  /// 无 scope 时回退 expanded（桌面键鼠假设），保证既有用例零改动。
  static Gs3dFormFactorScope? maybeOf(BuildContext context) =>
      context.dependOnInheritedWidgetOfExactType<Gs3dFormFactorScope>();

  /// 无 scope 时的桌面回退实例（child 永不使用）。
  const Gs3dFormFactorScope._fallback()
      : slot = Gs3dFormFactorSlot.expanded,
        super(child: const SizedBox.shrink());

  @override
  bool updateShouldNotify(Gs3dFormFactorScope oldWidget) =>
      slot != oldWidget.slot;
}

/// `context.gs3dFormFactor` / `context.isTouchPreferred` 便捷访问器。
extension Gs3dFormFactorContext on BuildContext {
  /// 当前档位；无 scope 时回退 expanded（桌面）。
  Gs3dFormFactorSlot get gs3dFormFactor =>
      Gs3dFormFactorScope.maybeOf(this)?.slot ?? Gs3dFormFactorSlot.expanded;

  /// 是否触控优先；无 scope 时回退 false（桌面键鼠假设）。
  bool get isTouchPreferred => gs3dFormFactor.isTouchPreferred;
}
