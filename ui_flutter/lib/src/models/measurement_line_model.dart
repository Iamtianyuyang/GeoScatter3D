import 'dart:math' as math;
import 'package:flutter/material.dart';
import 'point_cloud_model.dart';

/// 三维视口测量线段数据模型
class MeasurementLineItem {
  final String id;
  final Point3D start;
  final Point3D end;
  Color color;
  bool fixed;

  MeasurementLineItem({
    required this.id,
    required this.start,
    required this.end,
    this.color = const Color(0xFFF1C21B), // 默认测量黄
    this.fixed = false,
  });

  bool get isFixed => fixed;
  set isFixed(bool v) => fixed = v;

  MeasurementLineItem copyWith({
    String? id,
    Point3D? start,
    Point3D? end,
    Color? color,
    bool? isFixed,
  }) {
    return MeasurementLineItem(
      id: id ?? this.id,
      start: start ?? this.start,
      end: end ?? this.end,
      color: color ?? this.color,
      fixed: isFixed ?? this.fixed,
    );
  }

  double get dx => (end.x - start.x).abs();
  double get dy => (end.y - start.y).abs();
  double get deltaZ => (end.z - start.z).abs();

  double get distance3D =>
      math.sqrt(dx * dx + dy * dy + deltaZ * deltaZ);

  double get distance2D => math.sqrt(dx * dx + dy * dy);

  String distanceLabel([String mode = '3d']) {
    switch (mode.toLowerCase()) {
      case '2d':
      case 'planar':
        return '${distance2D.toStringAsFixed(2)} m (平)';
      case 'both':
        return '3D: ${distance3D.toStringAsFixed(2)}m / 2D: ${distance2D.toStringAsFixed(2)}m';
      case '3d':
      default:
        return '${distance3D.toStringAsFixed(2)} m';
    }
  }

  Map<String, dynamic> toJson() => {
    'id': id,
    'start': [start.x, start.y, start.z, start.value],
    'end': [end.x, end.y, end.z, end.value],
    'distance_3d': distance3D,
    'distance_2d': distance2D,
    'delta_z': deltaZ,
    'fixed': fixed,
  };
}
