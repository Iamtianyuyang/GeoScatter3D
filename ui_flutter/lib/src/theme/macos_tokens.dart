import 'package:flutter/material.dart';

import 'gs3d_tokens.dart';

/// macOS 桌面设计系统令牌（兼容适配层）。
///
/// 所有成员已代理至统一令牌 [Gs3dTokens] / [Gs3dPalette]，本文件不再持有
/// 独立色值。新代码请直接使用 `Gs3dTokens`。
class MacOsTokens {
  const MacOsTokens._();

  // 1. macOS 交通灯控制三色
  static const Color trafficRed = Gs3dPalette.trafficRed;
  static const Color trafficRedBorder = Gs3dPalette.trafficRedBorder;
  static const Color trafficYellow = Gs3dPalette.trafficYellow;
  static const Color trafficYellowBorder = Gs3dPalette.trafficYellowBorder;
  static const Color trafficGreen = Gs3dPalette.trafficGreen;
  static const Color trafficGreenBorder = Gs3dPalette.trafficGreenBorder;

  // 2. Apple System 动态强调色
  static const Color systemBlue = Gs3dTokens.accent;
  static const Color systemBlueHover = Gs3dTokens.accentHover;
  static const Color systemBlueLight = Gs3dTokens.accentSubtle;
  static const Color systemPurple = Gs3dPalette.systemPurple;
  static const Color systemPurpleLight = Color(0xFFF8F0FE);
  static const Color systemIndigo = Gs3dPalette.systemIndigo;
  static const Color systemGreen = Gs3dPalette.systemGreen;
  static const Color systemGreenLight = Gs3dTokens.successSubtle;
  static const Color systemSky = Color(0xFF0284C7);
  static const Color systemSkyLight = Color(0xFFF0F9FF);

  // 3. 材质与表面颜色（Vibrancy & Frosted Glass）
  static const Color windowBg = Gs3dTokens.windowBg;
  static const Color windowBgDark = Color(0xFF1C1C1E);
  static const Color frostedSurface = Color(0xD8FFFFFF); // ~85%
  static const Color frostedSurfaceHover = Color(0xF5FFFFFF); // ~96%
  static const Color frostedHeader = Color(0xEEF8F9FA); // = Gs3dMaterial.bar.tint
  static const Color frostedFooter = Color(0xEEF8F9FA); // = Gs3dMaterial.bar.tint
  static const Color hairlineBorder = Gs3dTokens.hairline; // 8% 黑
  static const Color hairlineBorderHover = Gs3dTokens.accentBorder; // 33% 蓝
  static const Color innerGlow = Color(0x66FFFFFF);

  // 4. 文字颜色（Apple SF Typography hierarchy）
  static const Color textPrimary = Gs3dTokens.textPrimary;
  static const Color textSecondary = Gs3dTokens.textSecondary;
  static const Color textTertiary = Gs3dTokens.textTertiary;
  static const Color textLink = Gs3dTokens.textLink;

  // 5. 圆角规范（Apple Continuous Curve / Squircle）
  static const double radiusSm = Gs3dTokens.radiusSm;
  static const double radiusMd = Gs3dTokens.radiusMd;
  static const double radiusCard = 16.0;
  static const double radiusIcon = 20.0;
  static const double radiusPill = Gs3dTokens.radiusPill;

  // 6. 弥散阴影（Diffuse Ambient Shadows）
  static const List<BoxShadow> cardShadow = Gs3dTokens.elevation1;

  static const List<BoxShadow> cardHoverShadow = Gs3dTokens.elevation2;

  static List<BoxShadow> get heroIconShadow =>
      Gs3dTokens.accentGlow(alpha: 0.25, blur: 28);

  // 7. 动画时长
  static const Duration microDuration = Gs3dMotion.micro;
  static const Duration normalDuration = Gs3dMotion.normal;
}
