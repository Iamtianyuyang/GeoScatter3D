class DatasetSummary {
  final bool isLoaded;
  final String name;
  final int pointCount;
  final String fileSize;
  final bool lodEnabled;
  final List<String> lodDetails;
  final List<String> attributes;
  final List<double> bboxMin;
  final List<double> bboxMax;
  final List<double> valueRange;

  const DatasetSummary({
    this.isLoaded = false,
    this.name = '--',
    this.pointCount = 0,
    this.fileSize = '--',
    this.lodEnabled = false,
    this.lodDetails = const [],
    this.attributes = const [],
    this.bboxMin = const [0, 0, 0],
    this.bboxMax = const [0, 0, 0],
    this.valueRange = const [0, 0],
  });

  /// 格式化为千分位点数，例如 "50,000,000 点"
  String get formattedPointCount {
    if (pointCount <= 0) return '0 点';
    final str = pointCount.toString();
    final buffer = StringBuffer();
    int count = 0;
    for (int i = str.length - 1; i >= 0; i--) {
      buffer.write(str[i]);
      count++;
      if (count % 3 == 0 && i != 0) {
        buffer.write(',');
      }
    }
    return '${buffer.toString().split('').reversed.join()} 点';
  }

  /// 转换为 JSON 字典供控制面与 MCP 上报
  Map<String, dynamic> toJson() => {
    'isLoaded': isLoaded,
    'name': name,
    'pointCount': pointCount,
    'formattedPointCount': formattedPointCount,
    'fileSize': fileSize,
    'lodEnabled': lodEnabled,
    'lodDetails': lodDetails,
    'attributes': attributes,
    'bboxMin': bboxMin,
    'bboxMax': bboxMax,
    'valueRange': valueRange,
  };
}
