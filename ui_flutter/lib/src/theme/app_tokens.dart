import 'package:flutter/material.dart';

/// GeoScatter3D 的语义视觉令牌。
///
/// Widget 只描述结构；颜色、间距、圆角和字体统一从这里读取，避免局部值
/// 在后续迭代中漂移。
@immutable
class AppTokens extends ThemeExtension<AppTokens> {
  const AppTokens({
    required this.colors,
    required this.spacing,
    required this.radii,
    required this.sizes,
    required this.typography,
    required this.effects,
  });

  static const dark = AppTokens(
    colors: AppColorTokens(
      viewport: Color(0xFF08131F),
      shellBackground: Color(0xFFF2F6FA),
      chromeSurface: Color(0xF21A2028),
      chromeSurfaceRaised: Color(0xFF242C36),
      chromeBorder: Color(0x33FFFFFF),
      chromeDivider: Color(0x26FFFFFF),
      chromeTextPrimary: Color(0xFFF1F5F8),
      chromeTextSecondary: Color(0xFFA9B6C4),
      chromeTextMuted: Color(0xFF788696),
      chromeAccent: Color(0xFF38A8E0),
      chromeAccentSoft: Color(0x332FA9E3),
      hudSurface: Color(0xFFFFFFFF),
      hudSurfaceTop: Color(0xFFFFFFFF),
      hudSurfaceBottom: Color(0xFFFFFFFF),
      hudSurfaceRaised: Color(0xFFFFFFFF),
      hudBorder: Color(0xFFE6EDF4),
      hudDivider: Color(0xFFD7E0E8),
      accent: Color(0xFF0B86C4),
      accentSoft: Color(0x240B86C4),
      statusReady: Color(0xFF0B8F35),
      statusReadySurface: Color(0xFFE8F9ED),
      statusReadyBorder: Color(0xFFC5F0CF),
      statusReadyText: Color(0xFF123C23),
      textPrimary: Color(0xFF0D1B2A),
      textSecondary: Color(0xFF556575),
      textMuted: Color(0xFF5F6E7D),
      placeholder: Color(0xFFE6ECF3),
      error: Color(0xFFE5484D),
    ),
    spacing: AppSpacingTokens(xs: 4, sm: 8, md: 12, lg: 16, xl: 24),
    radii: AppRadiusTokens(control: 12, panel: 24, pill: 999),
    sizes: AppSizeTokens(
      hudMaxWidth: 260,
      controlBarMaxWidth: 620,
      debugStatsMaxWidth: 400,
      tapTarget: 44,
      statusDot: 7,
      accentRail: 3,
      controlDividerHeight: 28,
      controlButtonSize: 40,
      controlIconSize: 20,
      metricDividerHeight: 48,
      metricStackScaleThreshold: 1.3,
      shellHeaderHeight: 56,
      shellFooterHeight: 56,
      shellFooterCollapsedHeight: 44,
      shellPanelRailWidth: 48,
      shellDrawerWidth: 320,
      shellSidebarBreakpoint: 900,
      shellFooterBreakpoint: 650,
      navItemHeight: 44,
      placeholderBarHeight: 12,
      lodBarHeight: 14,
    ),
    typography: AppTypographyTokens(
      brand: TextStyle(
        fontFamily: 'GeoSans',
        fontFamilyFallback: <String>['NotoSansCJKsc'],
        fontSize: 12,
        fontWeight: FontWeight.w800,
        height: 1,
      ),
      metricHero: TextStyle(
        fontFamily: 'GeoSans',
        fontFamilyFallback: <String>['NotoSansCJKsc'],
        fontSize: 32,
        fontWeight: FontWeight.w800,
        height: 0.9,
      ),
      metric: TextStyle(
        fontFamily: 'GeoSans',
        fontFamilyFallback: <String>['NotoSansCJKsc'],
        fontSize: 20,
        fontWeight: FontWeight.w700,
        height: 1.1,
      ),
      metricUnit: TextStyle(
        fontFamily: 'GeoSans',
        fontFamilyFallback: <String>['NotoSansCJKsc'],
        fontSize: 13,
        fontWeight: FontWeight.w600,
        height: 1.1,
      ),
      value: TextStyle(
        fontFamily: 'GeoSans',
        fontFamilyFallback: <String>['NotoSansCJKsc'],
        fontSize: 16,
        fontWeight: FontWeight.w700,
        height: 1.15,
      ),
      label: TextStyle(
        fontFamily: 'GeoSans',
        fontFamilyFallback: <String>['NotoSansCJKsc'],
        fontSize: 12,
        fontWeight: FontWeight.w500,
        height: 1.2,
      ),
      meta: TextStyle(
        fontFamily: 'GeoSans',
        fontFamilyFallback: <String>['NotoSansCJKsc'],
        fontSize: 11,
        fontWeight: FontWeight.w500,
        height: 1.25,
      ),
      status: TextStyle(
        fontFamily: 'GeoSans',
        fontFamilyFallback: <String>['NotoSansCJKsc'],
        fontSize: 13,
        fontWeight: FontWeight.w600,
        height: 1.1,
      ),
      shellBrand: TextStyle(
        fontFamily: 'GeoSans',
        fontFamilyFallback: <String>['NotoSansCJKsc'],
        fontSize: 20,
        fontWeight: FontWeight.w800,
        height: 1.1,
      ),
      sectionTitle: TextStyle(
        fontFamily: 'GeoSans',
        fontFamilyFallback: <String>['NotoSansCJKsc'],
        fontSize: 16,
        fontWeight: FontWeight.w700,
        height: 1.2,
      ),
      navLabel: TextStyle(
        fontFamily: 'GeoSans',
        fontFamilyFallback: <String>['NotoSansCJKsc'],
        fontSize: 14,
        fontWeight: FontWeight.w500,
        height: 1.2,
      ),
    ),
    effects: AppEffectTokens(
      hudBlur: 20,
      hairline: 1,
      panelShadow: BoxShadow(
        color: Color(0x294C6B8A),
        blurRadius: 28,
        offset: Offset(0, 14),
      ),
      controlShadow: BoxShadow(
        color: Color(0x244C6B8A),
        blurRadius: 16,
        offset: Offset(0, 7),
      ),
      chromeShadow: BoxShadow(
        color: Color(0x66000000),
        blurRadius: 26,
        offset: Offset(0, 12),
      ),
    ),
  );

  final AppColorTokens colors;
  final AppSpacingTokens spacing;
  final AppRadiusTokens radii;
  final AppSizeTokens sizes;
  final AppTypographyTokens typography;
  final AppEffectTokens effects;

  @override
  AppTokens copyWith({
    AppColorTokens? colors,
    AppSpacingTokens? spacing,
    AppRadiusTokens? radii,
    AppSizeTokens? sizes,
    AppTypographyTokens? typography,
    AppEffectTokens? effects,
  }) {
    return AppTokens(
      colors: colors ?? this.colors,
      spacing: spacing ?? this.spacing,
      radii: radii ?? this.radii,
      sizes: sizes ?? this.sizes,
      typography: typography ?? this.typography,
      effects: effects ?? this.effects,
    );
  }

  @override
  AppTokens lerp(covariant AppTokens? other, double t) {
    if (other == null) return this;
    return AppTokens(
      colors: AppColorTokens.lerp(colors, other.colors, t),
      spacing: AppSpacingTokens.lerp(spacing, other.spacing, t),
      radii: AppRadiusTokens.lerp(radii, other.radii, t),
      sizes: AppSizeTokens.lerp(sizes, other.sizes, t),
      typography: AppTypographyTokens.lerp(typography, other.typography, t),
      effects: AppEffectTokens.lerp(effects, other.effects, t),
    );
  }
}

extension AppTokensContext on BuildContext {
  AppTokens get tokens =>
      Theme.of(this).extension<AppTokens>() ?? AppTokens.dark;
}

@immutable
class AppColorTokens {
  const AppColorTokens({
    required this.viewport,
    required this.shellBackground,
    required this.chromeSurface,
    required this.chromeSurfaceRaised,
    required this.chromeBorder,
    required this.chromeDivider,
    required this.chromeTextPrimary,
    required this.chromeTextSecondary,
    required this.chromeTextMuted,
    required this.chromeAccent,
    required this.chromeAccentSoft,
    required this.hudSurface,
    required this.hudSurfaceTop,
    required this.hudSurfaceBottom,
    required this.hudSurfaceRaised,
    required this.hudBorder,
    required this.hudDivider,
    required this.accent,
    required this.accentSoft,
    required this.statusReady,
    required this.statusReadySurface,
    required this.statusReadyBorder,
    required this.statusReadyText,
    required this.textPrimary,
    required this.textSecondary,
    required this.textMuted,
    required this.placeholder,
    required this.error,
  });

  final Color viewport;
  final Color shellBackground;
  final Color chromeSurface;
  final Color chromeSurfaceRaised;
  final Color chromeBorder;
  final Color chromeDivider;
  final Color chromeTextPrimary;
  final Color chromeTextSecondary;
  final Color chromeTextMuted;
  final Color chromeAccent;
  final Color chromeAccentSoft;
  final Color hudSurface;
  final Color hudSurfaceTop;
  final Color hudSurfaceBottom;
  final Color hudSurfaceRaised;
  final Color hudBorder;
  final Color hudDivider;
  final Color accent;
  final Color accentSoft;
  final Color statusReady;
  final Color statusReadySurface;
  final Color statusReadyBorder;
  final Color statusReadyText;
  final Color textPrimary;
  final Color textSecondary;
  final Color textMuted;
  final Color placeholder;
  final Color error;

  static AppColorTokens lerp(AppColorTokens a, AppColorTokens b, double t) {
    return AppColorTokens(
      viewport: Color.lerp(a.viewport, b.viewport, t)!,
      shellBackground: Color.lerp(a.shellBackground, b.shellBackground, t)!,
      chromeSurface: Color.lerp(a.chromeSurface, b.chromeSurface, t)!,
      chromeSurfaceRaised: Color.lerp(
        a.chromeSurfaceRaised,
        b.chromeSurfaceRaised,
        t,
      )!,
      chromeBorder: Color.lerp(a.chromeBorder, b.chromeBorder, t)!,
      chromeDivider: Color.lerp(a.chromeDivider, b.chromeDivider, t)!,
      chromeTextPrimary: Color.lerp(
        a.chromeTextPrimary,
        b.chromeTextPrimary,
        t,
      )!,
      chromeTextSecondary: Color.lerp(
        a.chromeTextSecondary,
        b.chromeTextSecondary,
        t,
      )!,
      chromeTextMuted: Color.lerp(a.chromeTextMuted, b.chromeTextMuted, t)!,
      chromeAccent: Color.lerp(a.chromeAccent, b.chromeAccent, t)!,
      chromeAccentSoft: Color.lerp(a.chromeAccentSoft, b.chromeAccentSoft, t)!,
      hudSurface: Color.lerp(a.hudSurface, b.hudSurface, t)!,
      hudSurfaceTop: Color.lerp(a.hudSurfaceTop, b.hudSurfaceTop, t)!,
      hudSurfaceBottom: Color.lerp(a.hudSurfaceBottom, b.hudSurfaceBottom, t)!,
      hudSurfaceRaised: Color.lerp(a.hudSurfaceRaised, b.hudSurfaceRaised, t)!,
      hudBorder: Color.lerp(a.hudBorder, b.hudBorder, t)!,
      hudDivider: Color.lerp(a.hudDivider, b.hudDivider, t)!,
      accent: Color.lerp(a.accent, b.accent, t)!,
      accentSoft: Color.lerp(a.accentSoft, b.accentSoft, t)!,
      statusReady: Color.lerp(a.statusReady, b.statusReady, t)!,
      statusReadySurface: Color.lerp(
        a.statusReadySurface,
        b.statusReadySurface,
        t,
      )!,
      statusReadyBorder: Color.lerp(
        a.statusReadyBorder,
        b.statusReadyBorder,
        t,
      )!,
      statusReadyText: Color.lerp(a.statusReadyText, b.statusReadyText, t)!,
      textPrimary: Color.lerp(a.textPrimary, b.textPrimary, t)!,
      textSecondary: Color.lerp(a.textSecondary, b.textSecondary, t)!,
      textMuted: Color.lerp(a.textMuted, b.textMuted, t)!,
      placeholder: Color.lerp(a.placeholder, b.placeholder, t)!,
      error: Color.lerp(a.error, b.error, t)!,
    );
  }
}

@immutable
class AppSpacingTokens {
  const AppSpacingTokens({
    required this.xs,
    required this.sm,
    required this.md,
    required this.lg,
    required this.xl,
  });

  final double xs;
  final double sm;
  final double md;
  final double lg;
  final double xl;

  static AppSpacingTokens lerp(
    AppSpacingTokens a,
    AppSpacingTokens b,
    double t,
  ) {
    double mix(double from, double to) => from + (to - from) * t;
    return AppSpacingTokens(
      xs: mix(a.xs, b.xs),
      sm: mix(a.sm, b.sm),
      md: mix(a.md, b.md),
      lg: mix(a.lg, b.lg),
      xl: mix(a.xl, b.xl),
    );
  }
}

@immutable
class AppRadiusTokens {
  const AppRadiusTokens({
    required this.control,
    required this.panel,
    required this.pill,
  });

  final double control;
  final double panel;
  final double pill;

  BorderRadius get controlRadius => BorderRadius.circular(control);
  BorderRadius get panelRadius => BorderRadius.circular(panel);
  BorderRadius get pillRadius => BorderRadius.circular(pill);

  static AppRadiusTokens lerp(AppRadiusTokens a, AppRadiusTokens b, double t) {
    double mix(double from, double to) => from + (to - from) * t;
    return AppRadiusTokens(
      control: mix(a.control, b.control),
      panel: mix(a.panel, b.panel),
      pill: mix(a.pill, b.pill),
    );
  }
}

@immutable
class AppSizeTokens {
  const AppSizeTokens({
    required this.hudMaxWidth,
    required this.controlBarMaxWidth,
    required this.debugStatsMaxWidth,
    required this.tapTarget,
    required this.statusDot,
    required this.accentRail,
    required this.controlDividerHeight,
    required this.controlButtonSize,
    required this.controlIconSize,
    required this.metricDividerHeight,
    required this.metricStackScaleThreshold,
    required this.shellHeaderHeight,
    required this.shellFooterHeight,
    required this.shellFooterCollapsedHeight,
    required this.shellPanelRailWidth,
    required this.shellDrawerWidth,
    required this.shellSidebarBreakpoint,
    required this.shellFooterBreakpoint,
    required this.navItemHeight,
    required this.placeholderBarHeight,
    required this.lodBarHeight,
  });

  final double hudMaxWidth;
  final double controlBarMaxWidth;
  final double debugStatsMaxWidth;
  final double tapTarget;
  final double statusDot;
  final double accentRail;
  final double controlDividerHeight;
  final double controlButtonSize;
  final double controlIconSize;
  final double metricDividerHeight;
  final double metricStackScaleThreshold;
  final double shellHeaderHeight;
  final double shellFooterHeight;
  final double shellFooterCollapsedHeight;
  final double shellPanelRailWidth;
  final double shellDrawerWidth;
  final double shellSidebarBreakpoint;
  final double shellFooterBreakpoint;
  final double navItemHeight;
  final double placeholderBarHeight;
  final double lodBarHeight;

  static AppSizeTokens lerp(AppSizeTokens a, AppSizeTokens b, double t) {
    double mix(double from, double to) => from + (to - from) * t;
    return AppSizeTokens(
      hudMaxWidth: mix(a.hudMaxWidth, b.hudMaxWidth),
      controlBarMaxWidth: mix(a.controlBarMaxWidth, b.controlBarMaxWidth),
      debugStatsMaxWidth: mix(a.debugStatsMaxWidth, b.debugStatsMaxWidth),
      tapTarget: mix(a.tapTarget, b.tapTarget),
      statusDot: mix(a.statusDot, b.statusDot),
      accentRail: mix(a.accentRail, b.accentRail),
      controlDividerHeight: mix(a.controlDividerHeight, b.controlDividerHeight),
      controlButtonSize: mix(a.controlButtonSize, b.controlButtonSize),
      controlIconSize: mix(a.controlIconSize, b.controlIconSize),
      metricDividerHeight: mix(a.metricDividerHeight, b.metricDividerHeight),
      metricStackScaleThreshold: mix(
        a.metricStackScaleThreshold,
        b.metricStackScaleThreshold,
      ),
      shellHeaderHeight: mix(a.shellHeaderHeight, b.shellHeaderHeight),
      shellFooterHeight: mix(a.shellFooterHeight, b.shellFooterHeight),
      shellFooterCollapsedHeight: mix(
        a.shellFooterCollapsedHeight,
        b.shellFooterCollapsedHeight,
      ),
      shellPanelRailWidth: mix(a.shellPanelRailWidth, b.shellPanelRailWidth),
      shellDrawerWidth: mix(a.shellDrawerWidth, b.shellDrawerWidth),
      shellSidebarBreakpoint: mix(
        a.shellSidebarBreakpoint,
        b.shellSidebarBreakpoint,
      ),
      shellFooterBreakpoint: mix(
        a.shellFooterBreakpoint,
        b.shellFooterBreakpoint,
      ),
      navItemHeight: mix(a.navItemHeight, b.navItemHeight),
      placeholderBarHeight: mix(a.placeholderBarHeight, b.placeholderBarHeight),
      lodBarHeight: mix(a.lodBarHeight, b.lodBarHeight),
    );
  }
}

@immutable
class AppTypographyTokens {
  const AppTypographyTokens({
    required this.brand,
    required this.metricHero,
    required this.metric,
    required this.metricUnit,
    required this.value,
    required this.label,
    required this.meta,
    required this.status,
    required this.shellBrand,
    required this.sectionTitle,
    required this.navLabel,
  });

  final TextStyle brand;
  final TextStyle metricHero;
  final TextStyle metric;
  final TextStyle metricUnit;
  final TextStyle value;
  final TextStyle label;
  final TextStyle meta;
  final TextStyle status;
  final TextStyle shellBrand;
  final TextStyle sectionTitle;
  final TextStyle navLabel;

  static AppTypographyTokens lerp(
    AppTypographyTokens a,
    AppTypographyTokens b,
    double t,
  ) {
    return AppTypographyTokens(
      brand: TextStyle.lerp(a.brand, b.brand, t)!,
      metricHero: TextStyle.lerp(a.metricHero, b.metricHero, t)!,
      metric: TextStyle.lerp(a.metric, b.metric, t)!,
      metricUnit: TextStyle.lerp(a.metricUnit, b.metricUnit, t)!,
      value: TextStyle.lerp(a.value, b.value, t)!,
      label: TextStyle.lerp(a.label, b.label, t)!,
      meta: TextStyle.lerp(a.meta, b.meta, t)!,
      status: TextStyle.lerp(a.status, b.status, t)!,
      shellBrand: TextStyle.lerp(a.shellBrand, b.shellBrand, t)!,
      sectionTitle: TextStyle.lerp(a.sectionTitle, b.sectionTitle, t)!,
      navLabel: TextStyle.lerp(a.navLabel, b.navLabel, t)!,
    );
  }
}

@immutable
class AppEffectTokens {
  const AppEffectTokens({
    required this.hudBlur,
    required this.hairline,
    required this.panelShadow,
    required this.controlShadow,
    required this.chromeShadow,
  });

  final double hudBlur;
  final double hairline;
  final BoxShadow panelShadow;
  final BoxShadow controlShadow;
  final BoxShadow chromeShadow;

  static AppEffectTokens lerp(AppEffectTokens a, AppEffectTokens b, double t) {
    double mix(double from, double to) => from + (to - from) * t;
    return AppEffectTokens(
      hudBlur: mix(a.hudBlur, b.hudBlur),
      hairline: mix(a.hairline, b.hairline),
      panelShadow: BoxShadow.lerp(a.panelShadow, b.panelShadow, t)!,
      controlShadow: BoxShadow.lerp(a.controlShadow, b.controlShadow, t)!,
      chromeShadow: BoxShadow.lerp(a.chromeShadow, b.chromeShadow, t)!,
    );
  }
}
