import 'package:flutter/material.dart';

class Point3D {
  final double x;
  final double y;
  final double z;
  final double value;

  const Point3D({
    required this.x,
    required this.y,
    required this.z,
    required this.value,
  });

  /// 根据当前色表与数值区间映射出点颜色
  Color toColor(String colormap, double minVal, double maxVal) {
    if (maxVal <= minVal) return const Color(0xFF2563EB);
    final t = ((value - minVal) / (maxVal - minVal)).clamp(0.0, 1.0);

    switch (colormap.toLowerCase()) {
      case 'plasma':
        return _plasma(t);
      case 'turbo':
        return _turbo(t);
      case 'jet':
        return _jet(t);
      case 'coolwarm':
        return _coolwarm(t);
      case 'viridis':
      default:
        return _viridis(t);
    }
  }

  static Color _viridis(double t) {
    // 经典科学可视化 Viridis 渐变拟合
    final r = (0.267 + 2.0 * t * (1 - t) + 0.7 * t * t).clamp(0.0, 1.0);
    final g = (0.004 + 1.2 * t * (1 - t) + 0.9 * t).clamp(0.0, 1.0);
    final b = (0.329 + 1.5 * (1 - t) * (1 - t) + 0.15 * t).clamp(0.0, 1.0);
    return Color.fromARGB(
      255,
      (r * 255).toInt().clamp(0, 255),
      (g * 255).toInt().clamp(0, 255),
      (b * 255).toInt().clamp(0, 255),
    );
  }

  static Color _plasma(double t) {
    final r = (0.05 + 1.6 * t - 0.7 * t * t).clamp(0.0, 1.0);
    final g = (0.01 + 0.3 * t + 0.7 * t * t * t).clamp(0.0, 1.0);
    final b = (0.53 + 0.8 * (1 - t) - 0.4 * t).clamp(0.0, 1.0);
    return Color.fromARGB(
      255,
      (r * 255).toInt().clamp(0, 255),
      (g * 255).toInt().clamp(0, 255),
      (b * 255).toInt().clamp(0, 255),
    );
  }

  static Color _turbo(double t) {
    final r = (0.19 + 2.4 * t * (1 - t) + 0.8 * t * t).clamp(0.0, 1.0);
    final g = (0.07 + 1.5 * t * (1 - t) + 0.2 * t).clamp(0.0, 1.0);
    final b = (0.23 + 2.2 * (1 - t) * (1 - t)).clamp(0.0, 1.0);
    return Color.fromARGB(
      255,
      (r * 255).toInt().clamp(0, 255),
      (g * 255).toInt().clamp(0, 255),
      (b * 255).toInt().clamp(0, 255),
    );
  }

  static Color _jet(double t) {
    final r = (1.5 - (4.0 * t - 3.0).abs()).clamp(0.0, 1.0);
    final g = (1.5 - (4.0 * t - 2.0).abs()).clamp(0.0, 1.0);
    final b = (1.5 - (4.0 * t - 1.0).abs()).clamp(0.0, 1.0);
    return Color.fromARGB(
      255,
      (r * 255).toInt().clamp(0, 255),
      (g * 255).toInt().clamp(0, 255),
      (b * 255).toInt().clamp(0, 255),
    );
  }

  static Color _coolwarm(double t) {
    final r = (0.23 + 0.75 * t).clamp(0.0, 1.0);
    final g = (0.3 + 0.6 * (1.0 - (2.0 * t - 1.0).abs())).clamp(0.0, 1.0);
    final b = (0.88 - 0.65 * t).clamp(0.0, 1.0);
    return Color.fromARGB(
      255,
      (r * 255).toInt().clamp(0, 255),
      (g * 255).toInt().clamp(0, 255),
      (b * 255).toInt().clamp(0, 255),
    );
  }
}
