import 'dart:ui' show FontFeature;

import 'package:flutter/material.dart';

/// 测绘仪器风：近黑壳体、发丝线、单一信号色、等宽数字。
///
/// 只用深色：三维视口里的点云本身承载色彩，浅色底会把数据压扁，
/// 且在长时间作业中提高眼疲劳。
class AppTokens extends ThemeExtension<AppTokens> {
  const AppTokens({
    required this.colors,
    required this.type,
    required this.space,
    required this.radii,
    required this.motion,
  });

  static const AppTokens dark = AppTokens(
    colors: AppColors.dark,
    type: AppType(),
    space: AppSpace(),
    radii: AppRadii(),
    motion: AppMotion(),
  );

  final AppColors colors;
  final AppType type;
  final AppSpace space;
  final AppRadii radii;
  final AppMotion motion;

  @override
  AppTokens copyWith({
    AppColors? colors,
    AppType? type,
    AppSpace? space,
    AppRadii? radii,
    AppMotion? motion,
  }) {
    return AppTokens(
      colors: colors ?? this.colors,
      type: type ?? this.type,
      space: space ?? this.space,
      radii: radii ?? this.radii,
      motion: motion ?? this.motion,
    );
  }

  @override
  AppTokens lerp(ThemeExtension<AppTokens>? other, double t) {
    if (other is! AppTokens) return this;
    return t < 0.5 ? this : other;
  }
}

extension AppTokensContext on BuildContext {
  AppTokens get tokens => Theme.of(this).extension<AppTokens>() ?? AppTokens.dark;
}

@immutable
class AppColors {
  const AppColors({
    required this.viewport,
    required this.ink,
    required this.panel,
    required this.raised,
    required this.hover,
    required this.line,
    required this.lineStrong,
    required this.textHigh,
    required this.textMid,
    required this.textLow,
    required this.signal,
    required this.signalSoft,
    required this.alert,
    required this.warn,
  });

  static const AppColors dark = AppColors(
    viewport: Color(0xFF070B0F),
    ink: Color(0xFF0B1116),
    panel: Color(0xFF0F161C),
    raised: Color(0xFF131C24),
    hover: Color(0xFF17222B),
    line: Color(0xFF1D2A34),
    lineStrong: Color(0xFF2A3B48),
    textHigh: Color(0xFFE8EFF5),
    textMid: Color(0xFFA9BAC7),
    textLow: Color(0xFF74889A),
    signal: Color(0xFF58D6A8),
    signalSoft: Color(0x2458D6A8),
    alert: Color(0xFFE2654F),
    warn: Color(0xFFE0B15E),
  );

  final Color viewport;
  final Color ink;
  final Color panel;
  final Color raised;
  final Color hover;
  final Color line;
  final Color lineStrong;
  final Color textHigh;
  final Color textMid;
  final Color textLow;
  final Color signal;
  final Color signalSoft;
  final Color alert;
  final Color warn;

  Color get accent => signal;
  Color get error => alert;
}

@immutable
class AppType {
  const AppType();

  static const List<String> _fallback = ['NotoSansCJKsc'];

  TextStyle get panelTitle => const TextStyle(
    fontFamily: 'GeoSans',
    fontFamilyFallback: _fallback,
    fontSize: 15,
    fontWeight: FontWeight.w700,
    letterSpacing: -0.01,
    height: 1.25,
  );

  TextStyle get sectionLabel => const TextStyle(
    fontFamily: 'GeoSans',
    fontFamilyFallback: _fallback,
    fontSize: 11,
    fontWeight: FontWeight.w700,
    letterSpacing: 0.08,
    height: 1.2,
  );

  TextStyle get body => const TextStyle(
    fontFamily: 'GeoSans',
    fontFamilyFallback: _fallback,
    fontSize: 13,
    height: 1.35,
  );

  TextStyle get bodyStrong => const TextStyle(
    fontFamily: 'GeoSans',
    fontFamilyFallback: _fallback,
    fontSize: 13,
    fontWeight: FontWeight.w700,
    height: 1.35,
  );

  TextStyle get meta => const TextStyle(
    fontFamily: 'GeoSans',
    fontFamilyFallback: _fallback,
    fontSize: 11.5,
    height: 1.3,
  );

  /// 数值读数：等宽数字，避免数值跳动时界面抖动。
  TextStyle get numeric => const TextStyle(
    fontFamily: 'GeoSans',
    fontFamilyFallback: _fallback,
    fontSize: 12,
    height: 1.2,
    letterSpacing: 0.01,
    fontFeatures: [FontFeature.tabularFigures()],
  );

  TextStyle get display => const TextStyle(
    fontFamily: 'GeoSans',
    fontFamilyFallback: _fallback,
    fontSize: 22,
    fontWeight: FontWeight.w700,
    letterSpacing: -0.02,
    height: 1.2,
  );
}

@immutable
class AppSpace {
  const AppSpace();

  double get x1 => 4;
  double get x2 => 8;
  double get x3 => 12;
  double get x4 => 16;
  double get x5 => 24;
  double get x6 => 32;
}

@immutable
class AppRadii {
  const AppRadii();

  double get sm => 4;
  double get md => 6;
  double get lg => 10;
  BorderRadius get smAll => BorderRadius.circular(sm);
  BorderRadius get mdAll => BorderRadius.circular(md);
  BorderRadius get lgAll => BorderRadius.circular(lg);
}

@immutable
class AppMotion {
  const AppMotion();

  Duration get fast => const Duration(milliseconds: 110);
  Duration get base => const Duration(milliseconds: 180);
  Duration get slow => const Duration(milliseconds: 260);

  /// 指数收敛的减速曲线：起步快、尾部不拖沓。
  Curve get ease => const Cubic(0.16, 1.0, 0.3, 1.0);
}

/// 全局 ThemeData：把 tokens 同时写进 Material 主题，
/// 让下拉、滑块、对话框这些「没有手画的部件」也属于这套设计。
ThemeData buildAppTheme([AppTokens tokens = AppTokens.dark]) {
  final colors = tokens.colors;
  final scheme = ColorScheme.dark(
    primary: colors.signal,
    onPrimary: const Color(0xFF04120D),
    secondary: colors.textMid,
    onSecondary: colors.ink,
    surface: colors.panel,
    onSurface: colors.textHigh,
    surfaceContainerHighest: colors.raised,
    outline: colors.line,
    outlineVariant: colors.line,
    error: colors.alert,
    onError: const Color(0xFF1A0805),
  );
  return ThemeData(
    useMaterial3: true,
    brightness: Brightness.dark,
    colorScheme: scheme,
    scaffoldBackgroundColor: colors.ink,
    canvasColor: colors.panel,
    fontFamily: 'GeoSans',
    fontFamilyFallback: const ['NotoSansCJKsc'],
    splashFactory: InkSparkle.splashFactory,
    extensions: [tokens],
    dividerTheme: DividerThemeData(
      color: colors.line,
      thickness: 1,
      space: 1,
    ),
    textTheme: TextTheme(
      bodyMedium: tokens.type.body.copyWith(color: colors.textHigh),
      bodySmall: tokens.type.meta.copyWith(color: colors.textMid),
      labelSmall: tokens.type.sectionLabel.copyWith(color: colors.textLow),
      titleSmall: tokens.type.panelTitle.copyWith(color: colors.textHigh),
      headlineSmall: tokens.type.display.copyWith(color: colors.textHigh),
    ),
    iconTheme: IconThemeData(color: colors.textMid, size: 18),
    tooltipTheme: TooltipThemeData(
      waitDuration: const Duration(milliseconds: 400),
      decoration: BoxDecoration(
        color: colors.raised,
        border: Border.all(color: colors.line),
        borderRadius: tokens.radii.smAll,
      ),
      textStyle: tokens.type.meta.copyWith(color: colors.textHigh),
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 5),
    ),
    sliderTheme: SliderThemeData(
      activeTrackColor: colors.signal,
      inactiveTrackColor: colors.lineStrong,
      thumbColor: colors.textHigh,
      overlayColor: colors.signalSoft,
      trackHeight: 2,
      thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 7),
      overlayShape: const RoundSliderOverlayShape(overlayRadius: 16),
      valueIndicatorColor: colors.raised,
      valueIndicatorTextStyle: tokens.type.numeric.copyWith(
        color: colors.textHigh,
      ),
    ),
    switchTheme: SwitchThemeData(
      thumbColor: WidgetStateProperty.resolveWith(
        (states) => states.contains(WidgetState.selected)
            ? colors.signal
            : colors.textLow,
      ),
      trackColor: WidgetStateProperty.resolveWith(
        (states) => states.contains(WidgetState.selected)
            ? colors.signalSoft
            : colors.raised,
      ),
      trackOutlineColor: WidgetStateProperty.resolveWith(
        (states) => states.contains(WidgetState.selected)
            ? colors.signal
            : colors.lineStrong,
      ),
    ),
    dropdownMenuTheme: DropdownMenuThemeData(
      menuStyle: MenuStyle(
        backgroundColor: WidgetStatePropertyAll(colors.raised),
        padding: const WidgetStatePropertyAll(EdgeInsets.symmetric(vertical: 4)),
      ),
    ),
    popupMenuTheme: PopupMenuThemeData(
      color: colors.raised,
      surfaceTintColor: Colors.transparent,
      textStyle: tokens.type.body.copyWith(color: colors.textHigh),
      shape: RoundedRectangleBorder(
        borderRadius: tokens.radii.mdAll,
        side: BorderSide(color: colors.line),
      ),
    ),
    snackBarTheme: SnackBarThemeData(
      backgroundColor: colors.raised,
      contentTextStyle: tokens.type.body.copyWith(color: colors.textHigh),
      behavior: SnackBarBehavior.floating,
      shape: RoundedRectangleBorder(
        borderRadius: tokens.radii.mdAll,
        side: BorderSide(color: colors.line),
      ),
    ),
    scrollbarTheme: ScrollbarThemeData(
      thickness: const WidgetStatePropertyAll(6),
      radius: const Radius.circular(3),
      thumbColor: WidgetStatePropertyAll(colors.lineStrong),
      trackColor: const WidgetStatePropertyAll(Colors.transparent),
    ),
    textSelectionTheme: TextSelectionThemeData(
      cursorColor: colors.signal,
      selectionColor: colors.signalSoft,
      selectionHandleColor: colors.signal,
    ),
    focusColor: colors.signalSoft,
    hoverColor: colors.hover,
  );
}
