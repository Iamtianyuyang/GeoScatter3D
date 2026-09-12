import 'dart:ui' as ui;
import 'package:ui_flutter/src/ffi/gs3d_engine.dart';

/// 测试替身：把「渲染线程异步出帧」建模为 request → publish → read 三步，
/// 默认 request 立即产生新帧（与渲染线程空闲时的真实行为一致）。
class FakeEngine implements Gs3dEngine {
  int disposeCount = 0, loadCount = 0, renderCount = 0, resetCount = 0;
  int requestCount = 0, readCount = 0;
  int sampleResult = 0;
  List<(int, int)> sizes = [];
  Map<String, double>? input;
  Future<ui.Image?> Function()? render;

  /// true 时 request 不产生新帧，用于验证「渲染中不重复读、不报错」。
  bool stallFrames = false;

  int _sequence = 0;

  @override
  bool get isDisposed => disposeCount > 0;
  @override
  int get pointCount => 10000;
  @override
  String get lastError => 'fixture native failure';
  @override
  Map<String, dynamic> get diagnostics => {
    'gpu': {'name': 'fixture GPU'},
  };

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
