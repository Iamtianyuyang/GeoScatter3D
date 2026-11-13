/// 界面上的数字格式：千分位、定点、紧凑单位。
String formatCount(num value) {
  if (value.isNaN || value.isInfinite) return '—';
  final rounded = value.round();
  final text = rounded.abs().toString();
  final buffer = StringBuffer(rounded < 0 ? '-' : '');
  for (var i = 0; i < text.length; i++) {
    if (i > 0 && (text.length - i) % 3 == 0) buffer.write(',');
    buffer.write(text[i]);
  }
  return buffer.toString();
}

/// 大点数用「万 / 亿」，小数值原样保留。
String formatCompactCount(num value) {
  if (value.isNaN || value.isInfinite) return '—';
  if (value.abs() >= 100000000) {
    return '${(value / 100000000).toStringAsFixed(2)} 亿';
  }
  if (value.abs() >= 10000) {
    return '${(value / 10000).toStringAsFixed(value.abs() >= 1000000 ? 0 : 1)} 万';
  }
  return formatCount(value);
}

String formatNumber(num? value, {int digits = 2}) {
  if (value == null || value.isNaN || value.isInfinite) return '—';
  if (value.abs() >= 100000 || value == value.roundToDouble()) {
    return formatCount(value);
  }
  return value.toStringAsFixed(digits);
}

String formatBytes(num? bytes) {
  if (bytes == null || bytes <= 0) return '—';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  var value = bytes.toDouble();
  var unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  return '${value.toStringAsFixed(value >= 100 ? 0 : 1)} ${units[unit]}';
}

/// 把相对路径缩短成「上一级/文件名」，避免长路径挤爆面板。
String shortPath(String path) {
  final normalized = path.replaceAll('\\', '/');
  final parts = normalized.split('/').where((p) => p.isNotEmpty).toList();
  if (parts.length <= 2) return path;
  return '…/${parts.sublist(parts.length - 2).join('/')}';
}

String fileName(String path) {
  final normalized = path.replaceAll('\\', '/');
  final index = normalized.lastIndexOf('/');
  return index < 0 ? normalized : normalized.substring(index + 1);
}

/// 相对时间：刚刚 / 3 分钟前 / 昨天 / 2026-11-12。
String formatRelativeTime(DateTime? time, {DateTime? now}) {
  if (time == null) return '—';
  final reference = now ?? DateTime.now();
  final delta = reference.difference(time);
  if (delta.isNegative) return '刚刚';
  if (delta.inMinutes < 1) return '刚刚';
  if (delta.inHours < 1) return '${delta.inMinutes} 分钟前';
  if (delta.inDays < 1) return '${delta.inHours} 小时前';
  if (delta.inDays == 1) return '昨天';
  if (delta.inDays < 7) return '${delta.inDays} 天前';
  final local = time.toLocal();
  final month = local.month.toString().padLeft(2, '0');
  final day = local.day.toString().padLeft(2, '0');
  return '${local.year}-$month-$day';
}
