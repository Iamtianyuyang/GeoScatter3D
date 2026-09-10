import 'package:flutter/material.dart';

import 'gs3d_tokens.dart';

/// 主题装配层（`ThemeData` 组装）。
///
/// 本文件**只**负责把 [Gs3dTokens] / [Gs3dType] / [Gs3dMotion] 装配成
/// Material `ThemeData`，不持有任何独立色值。业务组件请直接引用 `Gs3dTokens`。
abstract final class Gs3dTheme {
  const Gs3dTheme._();

  /// 浅色主题（默认）。
  static ThemeData light() {
    final scheme = ColorScheme.fromSeed(
      seedColor: Gs3dTokens.accent,
      brightness: Brightness.light,
      surface: Gs3dTokens.surface,
      surfaceContainerLow: Gs3dTokens.surfaceSunken,
      error: Gs3dTokens.danger,
    );

    return _base(scheme).copyWith(scaffoldBackgroundColor: Gs3dTokens.windowBg);
  }

  /// 深色主题（分析舱 / 沉浸模式）。
  static ThemeData dark() {
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
      // 控件反馈：按压即时收缩由各组件按 Gs3dMotion.feedback 实现。
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
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          foregroundColor: Gs3dTokens.textBody,
          side: const BorderSide(color: Gs3dTokens.border),
          padding: const EdgeInsets.symmetric(
            horizontal: Gs3dTokens.space12 + 2,
            vertical: 10,
          ),
          textStyle: Gs3dType.body.copyWith(fontWeight: FontWeight.w500),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(Gs3dTokens.radiusSm),
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
