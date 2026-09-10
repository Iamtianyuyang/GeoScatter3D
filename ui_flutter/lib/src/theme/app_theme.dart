import 'package:flutter/material.dart';

import 'gs3d_tokens.dart';

/// 应用主题装配层。
///
/// 常量已全部代理到 [Gs3dTokens]，本文件**不再持有独立色值**。
/// 旧的 `AppTheme.xxx` 命名保留用于兼容存量组件；新代码请直接引用
/// `Gs3dTokens` / `Gs3dType` / `Gs3dMaterial` / `Gs3dMotion`。
class AppTheme {
  const AppTheme._();

  // ---- 语义色（代理至统一令牌）----------------------------------------------
  static const Color background = Gs3dTokens.windowBg;
  static const Color surface = Gs3dTokens.surface;
  static const Color surfaceMuted = Gs3dTokens.surfaceSunken;
  static const Color border = Gs3dTokens.border;
  static const Color borderSubtle = Gs3dTokens.borderStrong;

  static const Color textTitle = Gs3dTokens.textPrimary;
  static const Color textBody = Gs3dTokens.textBody;
  static const Color textDim = Gs3dTokens.textSecondary;

  static const Color primaryBlue = Gs3dTokens.accent;
  static const Color primaryBlueLight = Color(0xFF3B82F6);
  static const Color primaryBlueHover = Gs3dTokens.accentHover;
  static const Color primaryBlueBg = Gs3dTokens.accentSubtle;
  static const Color accentBlue = Gs3dTokens.accent;
  static const Color statusGreen = Gs3dTokens.quoteDown;

  // ---- 形状与阴影 -----------------------------------------------------------
  static const double cardRadius = Gs3dTokens.radiusLg;
  static const double controlRadius = Gs3dTokens.radiusSm;
  static const List<BoxShadow> cardShadow = Gs3dTokens.elevation1;

  /// 浅色主题（默认）。
  static ThemeData lightTheme() {
    final scheme = ColorScheme.fromSeed(
      seedColor: Gs3dTokens.accent,
      brightness: Brightness.light,
      surface: Gs3dTokens.surface,
      surfaceContainerLow: Gs3dTokens.surfaceSunken,
      error: Gs3dTokens.danger,
    );

    return _base(scheme).copyWith(
      scaffoldBackgroundColor: Gs3dTokens.windowBg,
    );
  }

  /// 深色主题（分析舱 / 沉浸模式）。
  static ThemeData darkTheme() {
    const scheme = ColorScheme.dark(
      primary: Color(0xFF4C9AFF),
      onPrimary: Colors.white,
      secondary: Color(0xFF7DD3FC),
      surface: Color(0xFF12161F),
      surfaceContainerLow: Color(0xFF0C0F16),
      onSurface: Color(0xFFE6EAF2),
      error: Color(0xFFFF6B6B),
    );
    return _base(scheme).copyWith(
      scaffoldBackgroundColor: const Color(0xFF0C0F16),
    );
  }

  /// 由令牌统一装配的 `ThemeData` 基座。
  static ThemeData _base(ColorScheme scheme) {
    return ThemeData(
      useMaterial3: true,
      colorScheme: scheme,
      fontFamily: Gs3dType.fontFamily,
      fontFamilyFallback: Gs3dType.fontFallback,
      visualDensity: VisualDensity.compact,
      splashFactory: InkSparkle.splashFactory,
      dividerTheme: const DividerThemeData(
        color: Gs3dTokens.hairline,
        thickness: 1,
        space: 1,
      ),
      cardTheme: CardThemeData(
        elevation: 0,
        color: Gs3dTokens.surface,
        surfaceTintColor: Colors.transparent,
        margin: EdgeInsets.zero,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(Gs3dTokens.radiusLg),
          side: const BorderSide(color: Gs3dTokens.border),
        ),
      ),
      tooltipTheme: TooltipThemeData(
        decoration: BoxDecoration(
          color: const Color(0xF21D1D1F),
          borderRadius: BorderRadius.circular(Gs3dTokens.radiusSm),
        ),
        textStyle: const TextStyle(
          fontSize: 11.5,
          color: Colors.white,
          letterSpacing: 0.1,
        ),
        waitDuration: const Duration(milliseconds: 420),
      ),
      scrollbarTheme: ScrollbarThemeData(
        thickness: const WidgetStatePropertyAll<double>(8),
        radius: const Radius.circular(4),
        thumbColor: WidgetStateProperty.resolveWith((states) {
          if (states.contains(WidgetState.hovered)) {
            return const Color(0x4D000000);
          }
          return const Color(0x26000000);
        }),
        crossAxisMargin: 2,
      ),
      dialogTheme: DialogThemeData(
        backgroundColor: Gs3dTokens.surface,
        surfaceTintColor: Colors.transparent,
        elevation: 0,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(Gs3dTokens.radiusXl),
        ),
      ),
      // 统一控件反馈：按压即时收缩由各组件按 Gs3dMotion.feedback 实现。
      filledButtonTheme: FilledButtonThemeData(
        style: FilledButton.styleFrom(
          backgroundColor: Gs3dTokens.accent,
          foregroundColor: Gs3dTokens.textOnAccent,
          elevation: 0,
          padding: const EdgeInsets.symmetric(
            horizontal: Gs3dTokens.space16,
            vertical: Gs3dTokens.space8,
          ),
          textStyle: Gs3dType.bodyStrong,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
          ),
        ),
      ),
      textButtonTheme: TextButtonThemeData(
        style: TextButton.styleFrom(
          foregroundColor: Gs3dTokens.accent,
          textStyle: Gs3dType.bodyStrong,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(Gs3dTokens.radiusSm),
          ),
        ),
      ),
    );
  }
}
