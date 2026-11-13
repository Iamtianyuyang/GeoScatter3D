import 'dart:convert';
import 'dart:io';

import 'app_state.dart';

/// 显示设置：只影响本机渲染，不写进工程包。
///
/// 除了视觉参数，也记录"上次用什么方式看这份数据"（着色/高度来源、
/// 值域裁剪、点预算），这样重开工程包时视图与用户预期一致。
class RenderSettings {
  const RenderSettings({
    this.colormapIndex = 6,
    this.pointSize = 1.5,
    this.pointShape = 0,
    this.background = 0x0B1015,
    this.heightSource = 0,
    this.colorSource = 1,
    this.heightExaggeration = 1.0,
    this.valueClipEnabled = false,
    this.valueClipMin,
    this.valueClipMax,
    this.pointBudget = 0,
  });

  /// 着色器支持的配色索引（0 地形、1 Viridis…8 Rainbow）。
  static const int maxColormapIndex = 8;
  static const double minPointSize = 0.5;
  static const double maxPointSize = 8.0;
  static const int maxPointShape = 3;
  static const double minHeightExaggeration = 0.2;
  static const double maxHeightExaggeration = 20.0;
  static const int maxPointBudget = 50000000;
  static const RenderSettings defaults = RenderSettings();

  final int colormapIndex;
  final double pointSize;
  final int pointShape;
  final int background;
  final int heightSource;
  final int colorSource;
  final double heightExaggeration;
  final bool valueClipEnabled;
  final double? valueClipMin;
  final double? valueClipMax;
  final int pointBudget;

  double get backgroundRed => ((background >> 16) & 0xFF) / 255.0;
  double get backgroundGreen => ((background >> 8) & 0xFF) / 255.0;
  double get backgroundBlue => (background & 0xFF) / 255.0;

  RenderSettings copyWith({
    int? colormapIndex,
    double? pointSize,
    int? pointShape,
    int? background,
    int? heightSource,
    int? colorSource,
    double? heightExaggeration,
    bool? valueClipEnabled,
    double? valueClipMin,
    double? valueClipMax,
    bool clearValueClip = false,
    int? pointBudget,
  }) {
    return RenderSettings(
      colormapIndex: colormapIndex ?? this.colormapIndex,
      pointSize: pointSize ?? this.pointSize,
      pointShape: pointShape ?? this.pointShape,
      background: background ?? this.background,
      heightSource: heightSource ?? this.heightSource,
      colorSource: colorSource ?? this.colorSource,
      heightExaggeration: heightExaggeration ?? this.heightExaggeration,
      valueClipEnabled: valueClipEnabled ?? this.valueClipEnabled,
      valueClipMin: clearValueClip ? null : (valueClipMin ?? this.valueClipMin),
      valueClipMax: clearValueClip ? null : (valueClipMax ?? this.valueClipMax),
      pointBudget: pointBudget ?? this.pointBudget,
    );
  }

  Map<String, Object?> toJson() => {
    'colormap_index': colormapIndex,
    'point_size': pointSize,
    'point_shape': pointShape,
    'background': background,
    'height_source': heightSource,
    'color_source': colorSource,
    'height_exaggeration': heightExaggeration,
    'value_clip_enabled': valueClipEnabled,
    if (valueClipMin != null) 'value_clip_min': valueClipMin,
    if (valueClipMax != null) 'value_clip_max': valueClipMax,
    'point_budget': pointBudget,
  };
}

/// 从一个 JSON 对象读设置；缺字段用默认值，坏值抛 [FormatException]。
RenderSettings renderSettingsFromJson(Map<Object?, Object?> value) {
  T pick<T>(String key, T fallback) {
    final raw = value[key];
    if (raw == null) return fallback;
    if (raw is! T) throw const FormatException('显示设置字段类型无效');
    return raw as T;
  }

  final colormap = pick('colormap_index', RenderSettings.defaults.colormapIndex);
  final size = pick('point_size', RenderSettings.defaults.pointSize);
  final shape = pick('point_shape', RenderSettings.defaults.pointShape);
  final background = pick('background', RenderSettings.defaults.background);
  final heightSource = pick('height_source', RenderSettings.defaults.heightSource);
  final colorSource = pick('color_source', RenderSettings.defaults.colorSource);
  final exaggeration = pick(
    'height_exaggeration',
    RenderSettings.defaults.heightExaggeration,
  );
  final clipEnabled = pick('value_clip_enabled', false);
  final budget = pick('point_budget', RenderSettings.defaults.pointBudget);
  if (colormap < 0 ||
      colormap > RenderSettings.maxColormapIndex ||
      size < RenderSettings.minPointSize ||
      size > RenderSettings.maxPointSize ||
      shape < 0 ||
      shape > RenderSettings.maxPointShape ||
      heightSource < 0 ||
      heightSource > 1 ||
      colorSource < 0 ||
      colorSource > 1 ||
      exaggeration < RenderSettings.minHeightExaggeration ||
      exaggeration > RenderSettings.maxHeightExaggeration ||
      budget < 0 ||
      budget > RenderSettings.maxPointBudget) {
    throw const FormatException('显示设置超出范围');
  }
  double? optionalDouble(String key) {
    final raw = value[key];
    if (raw == null) return null;
    if (raw is! num || !raw.isFinite) {
      throw const FormatException('显示设置字段类型无效');
    }
    return raw.toDouble();
  }

  final clipMin = optionalDouble('value_clip_min');
  final clipMax = optionalDouble('value_clip_max');
  if (clipMin != null && clipMax != null && clipMin >= clipMax) {
    throw const FormatException('值域裁剪区间无效');
  }
  return RenderSettings(
    colormapIndex: colormap,
    pointSize: size,
    pointShape: shape,
    background: background,
    heightSource: heightSource,
    colorSource: colorSource,
    heightExaggeration: exaggeration,
    valueClipEnabled: clipEnabled,
    valueClipMin: clipMin,
    valueClipMax: clipMax,
    pointBudget: budget,
  );
}

Future<File> renderSettingsFile() async {
  final directory = await appStateDirectory();
  return File('${directory.path}${Platform.pathSeparator}render-settings.json');
}

Future<void> writeRenderSettings(File file, RenderSettings settings) {
  return writeJsonAtomic(file, settings.toJson());
}

/// 读取失败（格式或范围不对）抛 [FormatException]，让调用方回到默认值。
Future<RenderSettings?> readRenderSettings(File file) async {
  if (!await file.exists()) return null;
  final value = jsonDecode(await file.readAsString());
  if (value is! Map) throw const FormatException('显示设置格式无效');
  return renderSettingsFromJson(value);
}
