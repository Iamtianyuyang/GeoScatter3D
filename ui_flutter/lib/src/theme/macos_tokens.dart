import 'package:flutter/material.dart';

/// macOS 现代化桌面设计系统令牌 (Human Interface Guidelines & Vibrancy)
class MacOsTokens {
  MacOsTokens._();

  // 1. 经典 macOS 交通灯控制三色
  static const Color trafficRed = Color(0xFFFF5F56);
  static const Color trafficRedBorder = Color(0xFFE0443E);
  static const Color trafficYellow = Color(0xFFFFBD2E);
  static const Color trafficYellowBorder = Color(0xFFDEA123);
  static const Color trafficGreen = Color(0xFF27C93F);
  static const Color trafficGreenBorder = Color(0xFF1AAB29);

  // 2. Apple System 动态强调色
  static const Color systemBlue = Color(0xFF007AFF);
  static const Color systemBlueHover = Color(0xFF0062CC);
  static const Color systemBlueLight = Color(0xFFEBF5FF);
  static const Color systemPurple = Color(0xFFAF52DE);
  static const Color systemPurpleLight = Color(0xFFF8F0FE);
  static const Color systemIndigo = Color(0xFF5856D6);
  static const Color systemGreen = Color(0xFF34C759);
  static const Color systemGreenLight = Color(0xFFEAFBF0);
  static const Color systemSky = Color(0xFF0284C7);
  static const Color systemSkyLight = Color(0xFFF0F9FF);

  // 3. 材质与表面颜色 (Vibrancy & Frosted Glass)
  static const Color windowBg = Color(0xFFF4F5F7);
  static const Color windowBgDark = Color(0xFF1C1C1E);
  static const Color frostedSurface = Color(0xD8FFFFFF); // ~85%
  static const Color frostedSurfaceHover = Color(0xF5FFFFFF); // ~96%
  static const Color frostedHeader = Color(0xEEF8FAFC); // ~93%
  static const Color frostedFooter = Color(0xEEF8FAFC); // ~93%
  static const Color hairlineBorder = Color(0x14000000); // 8% 黑
  static const Color hairlineBorderHover = Color(0x55007AFF); // 33% 蓝
  static const Color innerGlow = Color(0x66FFFFFF);

  // 4. 文字颜色 (Apple SF Typography hierarchy)
  static const Color textPrimary = Color(0xFF1D1D1F);
  static const Color textSecondary = Color(0xFF6E6E73);
  static const Color textTertiary = Color(0xFF86868B);
  static const Color textLink = Color(0xFF007AFF);

  // 5. 圆角规范 (Apple Continuous Curve / Squircle)
  static const double radiusSm = 6.0;
  static const double radiusMd = 10.0;
  static const double radiusCard = 16.0;
  static const double radiusIcon = 20.0;
  static const double radiusPill = 999.0;

  // 6. 弥散阴影 (Diffuse Ambient Shadows)
  static const List<BoxShadow> cardShadow = [
    BoxShadow(
      color: Color(0x0A000000), // 4%
      blurRadius: 18,
      offset: Offset(0, 4),
    ),
    BoxShadow(
      color: Color(0x05000000), // 2%
      blurRadius: 3,
      offset: Offset(0, 1),
    ),
  ];

  static const List<BoxShadow> cardHoverShadow = [
    BoxShadow(
      color: Color(0x1F007AFF), // 12% 蓝光
      blurRadius: 26,
      offset: Offset(0, 8),
    ),
    BoxShadow(
      color: Color(0x0A000000), // 4%
      blurRadius: 4,
      offset: Offset(0, 2),
    ),
  ];

  static const List<BoxShadow> heroIconShadow = [
    BoxShadow(
      color: Color(0x40007AFF), // 25% 蓝光
      blurRadius: 28,
      offset: Offset(0, 10),
    ),
    BoxShadow(
      color: Color(0x14000000),
      blurRadius: 6,
      offset: Offset(0, 2),
    ),
  ];

  // 7. 动画时长
  static const Duration microDuration = Duration(milliseconds: 160);
  static const Duration normalDuration = Duration(milliseconds: 240);
}
