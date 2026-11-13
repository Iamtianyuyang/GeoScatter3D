import 'dart:convert';
import 'dart:io';

import 'app_state.dart';

/// 显示设置（配色索引与点大小）；只影响显示，不改动工程包。
class RenderSettings {
  const RenderSettings({this.colormapIndex = 6, this.pointSize = 1.5});

  /// 着色器支持的配色索引（0 地形、1 Viridis…8 Rainbow）。
  static const int maxColormapIndex = 8;
  static const double minPointSize = 0.5;
  static const double maxPointSize = 8.0;
  static const RenderSettings defaults = RenderSettings();

  final int colormapIndex;
  final double pointSize;

  RenderSettings copyWith({int? colormapIndex, double? pointSize}) {
    return RenderSettings(
      colormapIndex: colormapIndex ?? this.colormapIndex,
      pointSize: pointSize ?? this.pointSize,
    );
  }
}

Future<File> renderSettingsFile() async {
  final directory = await appStateDirectory();
  return File('${directory.path}${Platform.pathSeparator}render-settings.json');
}

Future<void> writeRenderSettings(File file, RenderSettings settings) {
  return writeJsonAtomic(file, {
    'colormap_index': settings.colormapIndex,
    'point_size': settings.pointSize,
  });
}

/// 读取失败（格式或范围不对）抛 [FormatException]，让调用方回到默认值。
Future<RenderSettings?> readRenderSettings(File file) async {
  if (!await file.exists()) return null;
  final value = jsonDecode(await file.readAsString());
  if (value is! Map) throw const FormatException('显示设置格式无效');
  final index = value['colormap_index'];
  final size = value['point_size'];
  if (index is! int ||
      index < 0 ||
      index > RenderSettings.maxColormapIndex ||
      size is! num ||
      !size.isFinite ||
      size < RenderSettings.minPointSize ||
      size > RenderSettings.maxPointSize) {
    throw const FormatException('显示设置超出范围');
  }
  return RenderSettings(colormapIndex: index, pointSize: size.toDouble());
}
