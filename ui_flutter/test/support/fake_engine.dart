import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;
import 'package:ui_flutter/src/ffi/gs3d_engine.dart';

/// 测试替身：把「渲染线程异步出帧」建模为 request → publish → read 三步，
/// 默认 request 立即产生新帧（与渲染线程空闲时的真实行为一致）。
class FakeEngine implements Gs3dEngine {
  int disposeCount = 0, loadCount = 0, renderCount = 0, resetCount = 0;
  int requestCount = 0, readCount = 0;
  int sampleResult = 0;
  int? syntheticPoints;
  int? pointBudget;
  int? colormap;
  double? pointSize;
  List<(int, int)> sizes = [];
  final List<double> viewScales = [];
  Map<String, double>? input;
  (double, double, double, double)? background;
  Future<ui.Image?> Function()? render;

  /// true 时 request 不产生新帧，用于验证「渲染中不重复读、不报错」。
  bool stallFrames = false;

  int _sequence = 0;
  int _sourcePointCount = 10000;
  int _drawnPointCount = 10000;
  int _lodLevel = 0;
  int _lodLevelCount = 1;

  /// 模拟 LOD 生效：源点远大于绘制量。
  void setLodScenario({
    required int source,
    required int drawn,
    int level = 2,
    int levelCount = 4,
  }) {
    _sourcePointCount = source;
    _drawnPointCount = drawn;
    _lodLevel = level;
    _lodLevelCount = levelCount;
  }

  /// 异步开包被调用时记录的路径（顺序与调用一致）。
  final List<String> openedPaths = [];
  Object? openError;

  @override
  Future<void> loadDatasetAsync(
    String path, {
    Duration interval = const Duration(milliseconds: 200),
    void Function(int progress, String stage)? onProgress,
  }) async {
    if (openError case final Object error) throw error;
    openedPaths.add(path);
    onProgress?.call(500, 'install');
    _sourcePointCount = openSourcePoints;
    _drawnPointCount = openDrawnPoints;
    onProgress?.call(1000, 'done');
  }

  @override
  bool get isDisposed => disposeCount > 0;
  @override
  int get nativeHandle => 0;
  @override
  int get pointCount => _drawnPointCount;
  @override
  int get sourcePointCount => _sourcePointCount;
  @override
  int get drawnPointCount => _drawnPointCount;
  @override
  String get lastError => 'fixture native failure';
  @override
  Map<String, dynamic> get diagnostics => {
    'gpu': {'name': 'fixture GPU'},
    'fps': 58.5,
    'frame_time_ms': 17.1,
    'lod': {
      'source_points': _sourcePointCount,
      'drawn_points': _drawnPointCount,
      'level_index': _lodLevel,
      'level_count': _lodLevelCount,
    },
  };

  @override
  int loadSynthetic(int pointCount) {
    syntheticPoints = pointCount;
    loadCount++;
    return sampleResult;
  }

  @override
  void setPointBudget(int maxPoints) {
    pointBudget = maxPoints;
  }

  /// 导出记录：(路径, 格式)。`writeExportFile` 为 true 时真的落一个文件。
  final List<(String, String)> exports = [];
  int exportResult = 0;
  bool writeExportFile = false;
  Uint8List? framePng = Uint8List.fromList([137, 80, 78, 71]);

  @override
  int exportDataset(String path, {String format = 'ply'}) {
    exports.add((path, format));
    if (writeExportFile) File(path).writeAsStringSync('fake-points');
    return exportResult;
  }

  @override
  Uint8List? readFramePng({int initialBytes = 4 << 20, int maxBytes = 64 << 20}) {
    return framePng;
  }

  /// 打开工程包时报告的点数（默认样例规模）。
  int openSourcePoints = 4242;
  int openDrawnPoints = 4242;

  /// 拾取命中结果；null 表示未命中。
  PickedPoint? pickResult;
  final List<(double, double)> pickCalls = [];

  @override
  PickedPoint? pickPoint(
    double screenX,
    double screenY, {
    double tolerancePx = 12,
  }) {
    pickCalls.add((screenX, screenY));
    return pickResult;
  }

  /// 异步提交路径收到的参数（顺序与调用一致）。
  final List<int> submittedColormaps = [];
  final List<double> submittedPointSizes = [];

  @override
  int submitColormap(int index) {
    submittedColormaps.add(index);
    colormap = index;
    return 0;
  }

  @override
  int submitPointSize(double size) {
    submittedPointSizes.add(size);
    pointSize = size;
    return 0;
  }

  @override
  void setColormap(int index) {
    colormap = index;
  }

  @override
  void setPointSize(double size) {
    pointSize = size;
  }

  @override
  int get frameSequence => _sequence;

  @override
  double get lastRenderMs => 4.5;

  @override
  void requestFrame() {
    requestCount++;
    if (!stallFrames) _sequence++;
  }

  @override
  Future<ui.Image?> readFrame() {
    readCount++;
    return render?.call() ?? Future.value(null);
  }

  @override
  int loadSample() {
    loadCount++;
    return sampleResult;
  }

  @override
  void dispose() {
    disposeCount++;
  }

  @override
  void setViewScale(double scale) {
    viewScales.add(scale);
  }

  @override
  void setBackground(double r, double g, double b, [double a = 1.0]) {
    background = (r, g, b, a);
  }

  @override
  void resize(int width, int height) {
    sizes.add((width, height));
  }

  @override
  Future<ui.Image?> renderFrame() {
    renderCount++;
    return render?.call() ?? Future.value(null);
  }

  @override
  void resetCamera() {
    resetCount++;
  }

  @override
  void cameraInput({
    double rotDx = 0,
    double rotDy = 0,
    double panDx = 0,
    double panDy = 0,
    double scrollY = 0,
  }) {
    input = {
      'rotDx': rotDx,
      'rotDy': rotDy,
      'panDx': panDx,
      'panDy': panDy,
      'scrollY': scrollY,
    };
  }

  @override
  dynamic noSuchMethod(Invocation invocation) =>
      throw UnsupportedError('$invocation');
}
