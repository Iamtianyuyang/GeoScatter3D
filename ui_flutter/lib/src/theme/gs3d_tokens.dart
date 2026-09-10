/// GeoScatter3D 统一设计令牌 (Single Source of Truth)
///
/// 设计依据：Apple Human Interface Guidelines + WWDC 流体交互设计。
/// 本文件是**唯一**的视觉常量来源；`app_theme.dart` 与 `macos_tokens.dart`
/// 均为其兼容适配层，禁止再引入第二套硬编码色值。
///
/// 分层结构：
///   1. [Gs3dPalette]  原始色板（不直接使用）
///   2. [Gs3dTokens]   语义令牌（表面 / 文本 / 描边 / 强调 / 状态）
///   3. [Gs3dType]     光学字号阶梯（含动态字距与行高）
///   4. [Gs3dMaterial] 材质令牌（毛玻璃 / 层级阴影 / 描边）
///   5. [Gs3dMotion]   运动令牌（弹簧物理 / 动量投射 / 速度移交）
///   6. [Gs3dA11y]     无障碍运行时上下文（减弱动效 / 降低透明度 / 高对比）
library;

import 'dart:math' as math;
import 'dart:ui' show ImageFilter, lerpDouble;

import 'package:flutter/material.dart';

// ============================================================================
// 1. 原始色板（Primitives）— 仅供语义层引用，业务代码请勿直接使用
// ============================================================================

/// 原始色板。业务代码应引用 [Gs3dTokens] 的语义令牌而非此处。
abstract final class Gs3dPalette {
  const Gs3dPalette._();

  // 中性灰阶（Slate，用于浅色表面）
  static const Color slate0 = Color(0xFFFFFFFF);
  static const Color slate25 = Color(0xFFFCFCFD);
  static const Color slate50 = Color(0xFFF8FAFC);
  static const Color slate100 = Color(0xFFF1F5F9);
  static const Color slate150 = Color(0xFFEDF1F6);
  static const Color slate200 = Color(0xFFE2E8F0);
  static const Color slate300 = Color(0xFFCBD5E1);
  static const Color slate400 = Color(0xFF94A3B8);
  static const Color slate500 = Color(0xFF64748B);
  static const Color slate600 = Color(0xFF475569);
  static const Color slate700 = Color(0xFF334155);
  static const Color slate800 = Color(0xFF1E293B);
  static const Color slate900 = Color(0xFF0F172A);
  static const Color slate950 = Color(0xFF060B14);

  // Apple System 强调色
  static const Color systemBlue = Color(0xFF007AFF);
  static const Color systemBlueHover = Color(0xFF0062CC);
  static const Color systemBluePressed = Color(0xFF004FA6);
  static const Color systemIndigo = Color(0xFF5856D6);
  static const Color systemPurple = Color(0xFFAF52DE);
  static const Color systemTeal = Color(0xFF30B0C7);
  static const Color systemGreen = Color(0xFF34C759);
  static const Color systemMint = Color(0xFF00C7BE);
  static const Color systemOrange = Color(0xFFFF9500);
  static const Color systemRed = Color(0xFFFF3B30);
  static const Color systemYellow = Color(0xFFFFCC00);
  static const Color systemPink = Color(0xFFFF2D55);

  // macOS 交通灯
  static const Color trafficRed = Color(0xFFFF5F56);
  static const Color trafficRedBorder = Color(0xFFE0443E);
  static const Color trafficYellow = Color(0xFFFFBD2E);
  static const Color trafficYellowBorder = Color(0xFFDEA123);
  static const Color trafficGreen = Color(0xFF27C93F);
  static const Color trafficGreenBorder = Color(0xFF1AAB29);
}

// ============================================================================
// 2. 语义令牌（Semantic）— 业务代码统一引用此处
// ============================================================================

/// 语义化设计令牌。
abstract final class Gs3dTokens {
  const Gs3dTokens._();

  // ---- 表面与层级 ----------------------------------------------------------
  /// 窗口最底层背景。
  static const Color windowBg = Color(0xFFF4F5F7);

  /// 卡片 / 面板主表面。
  static const Color surface = Gs3dPalette.slate0;

  /// 抬升表面（悬浮卡片、下拉菜单）。
  static const Color surfaceRaised = Gs3dPalette.slate0;

  /// 凹陷表面（输入框、代码块、进度槽）。
  static const Color surfaceSunken = Gs3dPalette.slate100;

  /// 弱化分区表面（侧栏、表头）。
  static const Color surfaceMuted = Gs3dPalette.slate50;

  /// 工具栏 / 标题栏表面。
  static const Color surfaceHeader = Color(0xFFF8F9FA);

  /// 悬停态表面。
  static const Color surfaceHover = Color(0xFFF1F5F9);

  /// 选中态表面。
  static const Color surfaceSelected = Color(0xFFEBF2FF);

  // ---- 描边与分隔 ----------------------------------------------------------
  /// 发丝分隔线（8% 黑）。
  static const Color hairline = Color(0x14000000);

  /// 明确描边（用于卡片轮廓）。
  static const Color border = Gs3dPalette.slate200;

  /// 强描边（用于输入框聚焦前、分割重要区域）。
  static const Color borderStrong = Gs3dPalette.slate300;

  /// 焦点环颜色。
  static const Color focusRing = Color(0xFF007AFF);

  // ---- 文本 ---------------------------------------------------------------
  static const Color textPrimary = Color(0xFF1D1D1F);
  static const Color textBody = Gs3dPalette.slate700;
  static const Color textSecondary = Color(0xFF6E6E73);
  static const Color textTertiary = Color(0xFF86868B);
  static const Color textDisabled = Gs3dPalette.slate400;
  static const Color textOnAccent = Gs3dPalette.slate0;
  static const Color textLink = Gs3dPalette.systemBlue;

  // ---- 强调与交互 ----------------------------------------------------------
  static const Color accent = Gs3dPalette.systemBlue;
  static const Color accentHover = Gs3dPalette.systemBlueHover;
  static const Color accentPressed = Gs3dPalette.systemBluePressed;
  static const Color accentSubtle = Color(0xFFEBF5FF);
  static const Color accentBorder = Color(0x40007AFF);

  // ---- 状态色 -------------------------------------------------------------
  static const Color success = Color(0xFF059669);
  static const Color successSubtle = Color(0xFFECFDF5);
  static const Color warning = Color(0xFFD97706);
  static const Color warningSubtle = Color(0xFFFFFBEB);
  static const Color danger = Color(0xFFDC2626);
  static const Color dangerSubtle = Color(0xFFFEF2F2);
  static const Color info = Gs3dPalette.systemBlue;
  static const Color infoSubtle = Color(0xFFEBF5FF);

  // ---- 金融行情惯例（中国市场：涨红跌绿）------------------------------------
  /// 上涨 / 正向：红色。
  static const Color quoteUp = Color(0xFFE5484D);

  /// 下跌 / 负向：绿色。
  static const Color quoteDown = Color(0xFF30A46C);

  /// 平盘：中性灰。
  static const Color quoteFlat = Gs3dPalette.slate500;

  // ---- 圆角（Apple Squircle 连续曲率）--------------------------------------
  static const double radiusXs = 4.0;
  static const double radiusSm = 6.0;
  static const double radiusMd = 10.0;
  static const double radiusLg = 14.0;
  static const double radiusXl = 20.0;
  static const double radiusPill = 999.0;

  /// 兼容旧命名。
  static const double cardRadius = radiusLg;
  static const double controlRadius = radiusSm;

  // ---- 间距（8pt 基准栅格，含 4pt 半格）-------------------------------------
  static const double space2 = 2.0;
  static const double space4 = 4.0;
  static const double space8 = 8.0;
  static const double space12 = 12.0;
  static const double space16 = 16.0;
  static const double space20 = 20.0;
  static const double space24 = 24.0;
  static const double space32 = 32.0;
  static const double space40 = 40.0;
  static const double space48 = 48.0;
  static const double space64 = 64.0;

  // ---- 尺寸 --------------------------------------------------------------
  /// 顶部标题栏高度。
  static const double titleBarHeight = 40.0;

  /// 底部状态栏高度。
  static const double statusBarHeight = 26.0;

  /// 侧栏展开 / 折叠宽度。
  static const double sidebarExpanded = 220.0;
  static const double sidebarCollapsed = 64.0;

  /// 控件最小可点击尺寸（Apple 建议 ≥ 28×28 桌面）。
  static const double hitTarget = 28.0;

  // ---- 层级阴影（z0..z4）---------------------------------------------------
  /// z0：贴合表面，无阴影。
  static const List<BoxShadow> elevation0 = <BoxShadow>[];

  /// z1：卡片静止态（4% 黑，18px 弥散）。
  static const List<BoxShadow> elevation1 = <BoxShadow>[
    BoxShadow(color: Color(0x0A000000), blurRadius: 18, offset: Offset(0, 4)),
    BoxShadow(color: Color(0x05000000), blurRadius: 3, offset: Offset(0, 1)),
  ];

  /// z2：卡片悬停 / 轻微抬升。
  static const List<BoxShadow> elevation2 = <BoxShadow>[
    BoxShadow(color: Color(0x14000000), blurRadius: 24, offset: Offset(0, 8)),
    BoxShadow(color: Color(0x08000000), blurRadius: 4, offset: Offset(0, 2)),
  ];

  /// z3：弹出层 / 下拉菜单。
  static const List<BoxShadow> elevation3 = <BoxShadow>[
    BoxShadow(color: Color(0x1F000000), blurRadius: 34, offset: Offset(0, 12)),
    BoxShadow(color: Color(0x0A000000), blurRadius: 6, offset: Offset(0, 2)),
  ];

  /// z4：模态窗口 / 抽屉。
  static const List<BoxShadow> elevation4 = <BoxShadow>[
    BoxShadow(color: Color(0x2E000000), blurRadius: 48, offset: Offset(0, 20)),
    BoxShadow(color: Color(0x14000000), blurRadius: 10, offset: Offset(0, 4)),
  ];

  /// 强调色辉光（用于主按钮、Hero 图标）。
  static List<BoxShadow> accentGlow({double alpha = 0.28, double blur = 26}) => [
    BoxShadow(
      color: accent.withValues(alpha: alpha),
      blurRadius: blur,
      offset: const Offset(0, 8),
    ),
    const BoxShadow(color: Color(0x14000000), blurRadius: 6, offset: Offset(0, 2)),
  ];

  /// 兼容旧命名。
  static const List<BoxShadow> cardShadow = elevation1;
}

// ============================================================================
// 3. 光学字号排版（Optical Typography）
// ============================================================================

/// 光学排版阶梯。
///
/// Apple 排版原则：字号越大，字距越紧（负 tracking）；行高随字号收敛。
/// 每个标题层级都显式带 `letterSpacing` 与 `height`，避免依赖默认值漂移。
abstract final class Gs3dType {
  const Gs3dType._();

  static const String fontFamily = 'NotoSansCJKsc';
  static const List<String> fontFallback = <String>[
    'Segoe UI',
    'Microsoft YaHei',
    'PingFang SC',
    'sans-serif',
  ];

  /// 巨型展示标题（Hero）：32px / 紧字距 / 紧凑行高。
  static const TextStyle display = TextStyle(
    fontFamily: fontFamily,
    fontSize: 32,
    fontWeight: FontWeight.w700,
    letterSpacing: -0.8,
    height: 1.16,
    color: Gs3dTokens.textPrimary,
  );

  /// 页面大标题：26px。
  static const TextStyle title1 = TextStyle(
    fontFamily: fontFamily,
    fontSize: 26,
    fontWeight: FontWeight.w700,
    letterSpacing: -0.6,
    height: 1.2,
    color: Gs3dTokens.textPrimary,
  );

  /// 区块标题：20px。
  static const TextStyle title2 = TextStyle(
    fontFamily: fontFamily,
    fontSize: 20,
    fontWeight: FontWeight.w600,
    letterSpacing: -0.4,
    height: 1.24,
    color: Gs3dTokens.textPrimary,
  );

  /// 卡片标题：14.5px。
  static const TextStyle headline = TextStyle(
    fontFamily: fontFamily,
    fontSize: 14.5,
    fontWeight: FontWeight.w600,
    letterSpacing: -0.15,
    height: 1.3,
    color: Gs3dTokens.textPrimary,
  );

  /// 正文：12.5px。
  static const TextStyle body = TextStyle(
    fontFamily: fontFamily,
    fontSize: 12.5,
    fontWeight: FontWeight.w400,
    letterSpacing: 0,
    height: 1.45,
    color: Gs3dTokens.textBody,
  );

  /// 正文强调：12.5px w600。
  static const TextStyle bodyStrong = TextStyle(
    fontFamily: fontFamily,
    fontSize: 12.5,
    fontWeight: FontWeight.w600,
    letterSpacing: 0,
    height: 1.45,
    color: Gs3dTokens.textPrimary,
  );

  /// 辅助说明：11.5px。
  static const TextStyle caption = TextStyle(
    fontFamily: fontFamily,
    fontSize: 11.5,
    fontWeight: FontWeight.w400,
    letterSpacing: 0.05,
    height: 1.4,
    color: Gs3dTokens.textSecondary,
  );

  /// 极小微标：10.5px。
  static const TextStyle micro = TextStyle(
    fontFamily: fontFamily,
    fontSize: 10.5,
    fontWeight: FontWeight.w600,
    letterSpacing: 0.2,
    height: 1.3,
    color: Gs3dTokens.textTertiary,
  );

  /// 等宽数值（坐标 / 指标 / 代码）。
  static const TextStyle mono = TextStyle(
    fontFamily: 'Consolas',
    fontFamilyFallback: <String>['Cascadia Mono', 'Menlo', 'monospace'],
    fontSize: 11.5,
    fontWeight: FontWeight.w600,
    letterSpacing: 0.1,
    height: 1.35,
    color: Gs3dTokens.textPrimary,
  );

  /// 分区小标题（全大写 + 字距放宽，用于分组标签）。
  static const TextStyle overline = TextStyle(
    fontFamily: fontFamily,
    fontSize: 10.0,
    fontWeight: FontWeight.w700,
    letterSpacing: 0.8,
    height: 1.3,
    color: Gs3dTokens.textTertiary,
  );
}

// ============================================================================
// 4. 材质令牌（Materials）— 毛玻璃 / 活力
// ============================================================================

/// 材质描述，供 `BackdropFilter` 与玻璃容器复用。
@immutable
class Gs3dGlassSpec {
  const Gs3dGlassSpec({
    required this.blurSigma,
    required this.tint,
    required this.borderColor,
    required this.borderWidth,
    required this.shadows,
    this.innerHighlight,
  });

  /// 高斯模糊强度。
  final double blurSigma;

  /// 玻璃底色（半透明）。
  final Color tint;

  /// 描边颜色（发丝高光）。
  final Color borderColor;

  final double borderWidth;

  final List<BoxShadow> shadows;

  /// 内上缘高光（模拟玻璃折射）。
  final Color? innerHighlight;

  ImageFilter get filter => ImageFilter.blur(
    sigmaX: blurSigma,
    sigmaY: blurSigma,
    tileMode: TileMode.clamp,
  );

  BorderRadius get radius => BorderRadius.circular(Gs3dTokens.radiusLg);

  BoxDecoration decoration({BorderRadius? borderRadius}) => BoxDecoration(
    color: tint,
    borderRadius: borderRadius ?? radius,
    border: Border.all(color: borderColor, width: borderWidth),
    boxShadow: shadows,
  );
}

/// 材质令牌集合。
abstract final class Gs3dMaterial {
  const Gs3dMaterial._();

  /// 侧栏 / 面板玻璃（弱模糊，保持文字锐利）。
  static const Gs3dGlassSpec panel = Gs3dGlassSpec(
    blurSigma: 18,
    tint: Color(0xE6F7F8FA),
    borderColor: Color(0x14000000),
    borderWidth: 1,
    shadows: Gs3dTokens.elevation1,
  );

  /// 顶栏 / 底栏玻璃。
  static const Gs3dGlassSpec bar = Gs3dGlassSpec(
    blurSigma: 24,
    tint: Color(0xEEF8F9FA),
    borderColor: Color(0x14000000),
    borderWidth: 1,
    shadows: Gs3dTokens.elevation0,
  );

  /// 悬浮胶囊 Dock（深色沉浸）。
  static const Gs3dGlassSpec capsule = Gs3dGlassSpec(
    blurSigma: 20,
    tint: Color(0xE0181F2C),
    borderColor: Color(0x29FFFFFF),
    borderWidth: 1,
    shadows: Gs3dTokens.elevation4,
    innerHighlight: Color(0x1FFFFFFF),
  );

  /// 弹出卡片（强模糊 + 高亮描边）。
  static const Gs3dGlassSpec overlay = Gs3dGlassSpec(
    blurSigma: 30,
    tint: Color(0xF2FFFFFF),
    borderColor: Color(0x1A000000),
    borderWidth: 1,
    shadows: Gs3dTokens.elevation3,
    innerHighlight: Color(0x66FFFFFF),
  );

  /// 模态遮罩底色。
  static const Color scrim = Color(0x40000000);

  /// 减弱透明度模式下的实心替代色。
  static Color opaqueFallback(Color tint) =>
      Color.lerp(tint, Gs3dTokens.surface, 0.92) ?? Gs3dTokens.surface;
}

// ============================================================================
// 5. 运动令牌（Motion）— 弹簧物理 / 动量 / 速度移交
// ============================================================================

/// 弹簧规格：直接对应 Apple 的「阻尼比 + 响应时间」两参数模型。
@immutable
class Gs3dSpring {
  const Gs3dSpring({required this.dampingRatio, required this.response});

  /// 阻尼比。1.0 = 临界阻尼（无弹跳）；< 1 产生过冲。
  final double dampingRatio;

  /// 响应时间（秒）。描述趋近目标的速度，**不是**固定动画时长。
  final double response;

  /// 转换为 Flutter 物理弹簧。由 ω = 2π/response 与 c = 2ζ√(km) 推导。
  SpringDescription toSpringDescription({double mass = 1.0}) {
    final omega = 2 * math.pi / response;
    final stiffness = omega * omega * mass;
    return SpringDescription.withDampingRatio(
      mass: mass,
      stiffness: stiffness,
      ratio: dampingRatio,
    );
  }

  /// 归一化相对速度：释放速度 / 位移距离（Apple 速度移交公式）。
  double handoffVelocity(double releaseVelocity, double displacement) {
    if (displacement.abs() < 1e-6) return 0.0;
    return releaseVelocity / displacement;
  }
}

/// 动量投射与运动令牌集合。
abstract final class Gs3dMotion {
  const Gs3dMotion._();

  /// 普通 UI 位移（临界阻尼，无弹跳）—— Apple 官方「移动」参数。
  static const Gs3dSpring ui = Gs3dSpring(dampingRatio: 1.0, response: 0.4);

  /// 抽屉 / 底部 Sheet —— Apple 官方 sheet 参数。
  static const Gs3dSpring sheet = Gs3dSpring(dampingRatio: 0.8, response: 0.3);

  /// 旋转 / 展开 —— Apple 官方旋转参数。
  static const Gs3dSpring rotate = Gs3dSpring(dampingRatio: 0.8, response: 0.4);

  /// 手势抛动 / 轻扫（带少量弹跳，仅在有动量时使用）。
  static const Gs3dSpring fling = Gs3dSpring(dampingRatio: 0.8, response: 0.35);

  /// 快速反馈（悬停 / 按压）。
  static const Gs3dSpring feedback = Gs3dSpring(dampingRatio: 1.0, response: 0.18);

  /// 默认滚动减速率。
  static const double decelerationRate = 0.998;

  /// 干脆滚动减速率。
  static const double decelerationRateCrisp = 0.99;

  /// 动量投射终点：`(v/1000) * rate / (1 - rate)`（Apple 官方公式）。
  ///
  /// [initialVelocity] 单位 px/s，返回投射距离（px）。
  static double project(
    double initialVelocity, {
    double rate = decelerationRate,
  }) {
    final r = rate.clamp(0.0, 0.9999);
    return (initialVelocity / 1000.0) * r / (1.0 - r);
  }

  /// 橡胶边界阻尼：越界越远，位移增量越小（Apple 橡皮筋公式）。
  ///
  /// [overshoot] 为超出边界的距离，[dimension] 为边界容器尺寸。
  static double rubberBand(double overshoot, double dimension,
      {double coefficient = 0.55}) {
    if (dimension <= 0) return overshoot;
    final sign = overshoot.sign;
    final x = overshoot.abs();
    return sign * (1 - (1 / ((x * coefficient / dimension) + 1))) * dimension;
  }

  // ---- 时长令牌（用于非弹簧场景：淡入淡出、颜色过渡）--------------------------
  static const Duration instant = Duration(milliseconds: 90);
  static const Duration micro = Duration(milliseconds: 160);
  static const Duration normal = Duration(milliseconds: 240);
  static const Duration slow = Duration(milliseconds: 360);

  static const Curve enterCurve = Curves.easeOutCubic;
  static const Curve exitCurve = Curves.easeInCubic;
  static const Curve standardCurve = Curves.easeInOutCubic;

  /// 兼容旧命名。
  static const Duration microDuration = micro;
  static const Duration normalDuration = normal;
}

// ============================================================================
// 6. 无障碍（Accessibility）
// ============================================================================

/// 无障碍运行时偏好。在 `app_theme.dart` 中通过 `MaterialApp.builder` 注入。
@immutable
class Gs3dA11y {
  const Gs3dA11y({
    this.reduceMotion = false,
    this.reduceTransparency = false,
    this.highContrast = false,
    this.textScale = 1.0,
  });

  /// 系统「减弱动效」：弹簧退化为即时切换。
  final bool reduceMotion;

  /// 系统「降低透明度」：毛玻璃退化为实心表面。
  final bool reduceTransparency;

  /// 系统「高对比度」：描边与文本对比增强。
  final bool highContrast;

  /// 系统字号缩放。
  final double textScale;

  static const Gs3dA11y standard = Gs3dA11y();

  /// 从 Flutter 平台媒体查询解析无障碍偏好。
  factory Gs3dA11y.fromContext(BuildContext context) {
    final mq = MediaQuery.maybeOf(context);
    if (mq == null) return standard;
    return Gs3dA11y(
      reduceMotion: mq.disableAnimations,
      reduceTransparency: mq.highContrast && false, // 平台无独立开关，交由高对比代理
      highContrast: mq.highContrast,
      textScale: mq.textScaler.scale(1.0),
    );
  }

  /// 解析动画时长：减弱动效时归零。
  Duration resolve(Duration duration) =>
      reduceMotion ? Duration.zero : duration;

  /// 解析弹簧：减弱动效时退化为临界阻尼极小响应（视觉上即时）。
  Gs3dSpring resolveSpring(Gs3dSpring spring) => reduceMotion
      ? const Gs3dSpring(dampingRatio: 1.0, response: 0.001)
      : spring;

  /// 解析玻璃材质：降低透明度时退化为实心。
  Gs3dGlassSpec resolveGlass(Gs3dGlassSpec spec) {
    if (!reduceTransparency) return spec;
    return Gs3dGlassSpec(
      blurSigma: 0,
      tint: Gs3dMaterial.opaqueFallback(spec.tint),
      borderColor: Gs3dTokens.borderStrong,
      borderWidth: 1,
      shadows: spec.shadows,
    );
  }

  Gs3dA11y copyWith({
    bool? reduceMotion,
    bool? reduceTransparency,
    bool? highContrast,
    double? textScale,
  }) => Gs3dA11y(
    reduceMotion: reduceMotion ?? this.reduceMotion,
    reduceTransparency: reduceTransparency ?? this.reduceTransparency,
    highContrast: highContrast ?? this.highContrast,
    textScale: textScale ?? this.textScale,
  );

  @override
  bool operator ==(Object other) =>
      other is Gs3dA11y &&
      other.reduceMotion == reduceMotion &&
      other.reduceTransparency == reduceTransparency &&
      other.highContrast == highContrast &&
      other.textScale == textScale;

  @override
  int get hashCode =>
      Object.hash(reduceMotion, reduceTransparency, highContrast, textScale);
}

/// 将 [Gs3dA11y] 注入 Widget 树，供任意层级读取。
class Gs3dA11yScope extends InheritedWidget {
  const Gs3dA11yScope({
    super.key,
    required this.a11y,
    required super.child,
  });

  final Gs3dA11y a11y;

  static Gs3dA11y of(BuildContext context) =>
      context
          .dependOnInheritedWidgetOfExactType<Gs3dA11yScope>()
          ?.a11y ??
      Gs3dA11y.standard;

  @override
  bool updateShouldNotify(Gs3dA11yScope oldWidget) => a11y != oldWidget.a11y;
}

/// `context.gs3dA11y` 便捷访问器。
extension Gs3dA11yContext on BuildContext {
  Gs3dA11y get gs3dA11y => Gs3dA11yScope.of(this);
}

// ============================================================================
// 7. 动画辅助构建器（可中断弹簧）
// ============================================================================

/// 由弹簧规格推导的 [Curve]，用于 `AnimatedContainer` 等隐式动画。
///
/// 注意：隐式动画无法中途保速打断；需要「随时可打断并从当前位置续接」的
/// 场景请直接使用 `AnimationController.animateWith(SpringSimulation)`。
class Gs3dSpringCurve extends Curve {
  const Gs3dSpringCurve(this.spring);

  final Gs3dSpring spring;

  @override
  double transformInternal(double t) {
    // 归一化时间 → 二阶系统阶跃响应闭式解（欠阻尼 / 临界 / 过阻尼）。
    final zeta = spring.dampingRatio;
    final omega = 2 * math.pi / spring.response;
    final tau = t * spring.response * 4.0; // 采样窗口覆盖约 4 倍响应时间
    if (zeta >= 1.0) {
      // 临界阻尼：y = 1 - (1 + ωt) e^{-ωt}
      return (1 - (1 + omega * tau) * math.exp(-omega * tau))
          .clamp(0.0, 1.0);
    }
    // 欠阻尼：y = 1 - e^{-ζωt}(cos(ω_d t) + ζ/√(1-ζ²) sin(ω_d t))
    final wd = omega * math.sqrt(1 - zeta * zeta);
    final e = math.exp(-zeta * omega * tau);
    final value =
        1 - e * (math.cos(wd * tau) + zeta / math.sqrt(1 - zeta * zeta) * math.sin(wd * tau));
    return value.clamp(0.0, 1.0);
  }
}

/// 颜色插值辅助（保留 alpha 语义，避免 `withOpacity` 弃用告警）。
Color gs3dMix(Color a, Color b, double t) =>
    Color.lerp(a, b, t.clamp(0.0, 1.0)) ?? a;

/// 数值插值辅助。
double gs3dLerp(double a, double b, double t) =>
    lerpDouble(a, b, t.clamp(0.0, 1.0)) ?? a;
