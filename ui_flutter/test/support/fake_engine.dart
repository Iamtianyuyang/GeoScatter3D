import 'dart:ui' as ui;
import 'package:ui_flutter/src/ffi/gs3d_engine.dart';

class FakeEngine implements Gs3dEngine {
  int disposeCount = 0, loadCount = 0, renderCount = 0, resetCount = 0;
  int sampleResult = 0;
  List<(int, int)> sizes = [];
  Map<String, double>? input;
  Future<ui.Image?> Function()? render;
  @override
  bool get isDisposed => disposeCount > 0;
  @override
  int get pointCount => 10000;
  @override
  String get lastError => 'fixture native failure';
  @override
  Map<String, dynamic> get diagnostics => {'gpu': 'fixture GPU'};
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
