import 'dart:ffi' as ffi;

import 'package:ffi/ffi.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/gs3d_bindings.dart';

void main() {
  test('engine state ABI layout matches the native 64-bit structs', () {
    expect(ffi.sizeOf<ffi.IntPtr>(), 8);
    expect(ffi.sizeOf<Gs3dTaskStateNative>(), 48);
    expect(ffi.sizeOf<Gs3dStateNative>(), 480);
  });

  test('command struct layout matches the native union', () {
    expect(ffi.sizeOf<Gs3dCommandNative>(), 32);
    final command = calloc<Gs3dCommandNative>();
    try {
      command.ref.structSize = ffi.sizeOf<Gs3dCommandNative>();
      command.ref.op = gs3dCmdCameraInput;
      command.ref.flags = 0;
      const camera = [0.25, -0.5, 1.0, 2.0, 3.5];
      for (var i = 0; i < camera.length; ++i) {
        command.ref.args[i] = camera[i];
      }
      expect(command.ref.structSize, 32);
      expect(command.ref.op, gs3dCmdCameraInput);
      expect(command.ref.args[4], 3.5);
    } finally {
      calloc.free(command);
    }
  });

  test('state snapshot fields are read back from the native layout', () {
    final state = calloc<Gs3dStateNative>();
    try {
      state.ref.structSize = ffi.sizeOf<Gs3dStateNative>();
      state.ref.generation = 7;
      state.ref.pointCount = 50000000;
      state.ref.datasetLoaded = 1;
      state.ref.taskCount = 2;

      // error 是定长 C 字符串：写入时必须 NUL 结尾。
      const message = 'disk read failed';
      for (var i = 0; i < message.length; ++i) {
        state.ref.error[i] = message.codeUnitAt(i);
      }
      state.ref.error[message.length] = 0;

      state.ref.tasks[0].id = 42;
      state.ref.tasks[0].state = gs3dTaskRunning;
      state.ref.tasks[0].progress = 250;
      const stage = 'install';
      for (var i = 0; i < stage.length; ++i) {
        state.ref.tasks[0].stage[i] = stage.codeUnitAt(i);
      }
      state.ref.tasks[0].stage[stage.length] = 0;

      expect(state.ref.generation, 7);
      expect(state.ref.pointCount, 50000000);
      expect(state.ref.datasetLoaded, 1);
      expect(state.ref.taskCount, 2);
      expect(state.ref.errorText, message);
      expect(state.ref.tasks[0].id, 42);
      expect(state.ref.tasks[0].state, gs3dTaskRunning);
      expect(state.ref.stageAt(0), stage);
      expect(state.ref.stageAt(1), '');
    } finally {
      calloc.free(state);
    }
  });
}
