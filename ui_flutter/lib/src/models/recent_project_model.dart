class RecentProjectItem {
  final String path;
  final int timestampUnix;

  const RecentProjectItem({
    required this.path,
    required this.timestampUnix,
  });

  String get displayName {
    final normalized = path.replaceAll('\\', '/');
    final parts = normalized.split('/');
    if (parts.isEmpty) return path;
    var last = parts.last;
    if (last.isEmpty && parts.length > 1) {
      last = parts[parts.length - 2];
    }
    if (last.endsWith('.gs3d.bundle')) {
      return last.substring(0, last.length - 12);
    }
    if (last.endsWith('.gs3d')) {
      return last.substring(0, last.length - 5);
    }
    return last;
  }

  String get relativeTimeStr {
    if (timestampUnix <= 0) return '最近打开';
    final now = DateTime.now().millisecondsSinceEpoch ~/ 1000;
    final diff = now - timestampUnix;
    if (diff < 60) return '刚刚';
    if (diff < 3600) return '${diff ~/ 60} 分钟前';
    if (diff < 86400) return '${diff ~/ 3600} 小时前';
    if (diff < 86400 * 7) return '${diff ~/ 86400} 天前';
    return DateTime.fromMillisecondsSinceEpoch(timestampUnix * 1000)
        .toString()
        .substring(0, 10);
  }

  Map<String, dynamic> toJson() => {
    'path': path,
    'displayName': displayName,
    'timestamp': timestampUnix,
    'time': relativeTimeStr,
  };
}
