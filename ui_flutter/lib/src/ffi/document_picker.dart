import 'dart:ffi' as ffi;
import 'dart:io';
import 'dart:isolate';

import 'package:ffi/ffi.dart';
import 'package:flutter/services.dart';

import 'gs3d_bindings.dart';

const _documents = MethodChannel('com.geoscatter3d.ui_flutter/documents');

Future<String?> pickPointFile() => _pickDocument(false);
Future<String?> pickBundleDirectory() => _pickDocument(true);

Future<String?> _pickDocument(bool directory) async {
  if (!Platform.isWindows) {
    return _documents.invokeMethod<String>(
      directory ? 'pickDirectory' : 'pickFile',
    );
  }
  return Isolate.run(() {
    final library = Gs3dBindings.openLibrary();
    if (directory) {
      final pick = library
          .lookupFunction<
            ffi.Pointer<Utf8> Function(),
            ffi.Pointer<Utf8> Function()
          >('gs3d_ffi_pick_folder');
      final result = pick();
      if (result == ffi.nullptr) return null;
      final path = result.toDartString();
      return path.isEmpty ? null : path;
    }
    final pick = library
        .lookupFunction<
          ffi.Pointer<Utf8> Function(ffi.Pointer<Utf8>),
          ffi.Pointer<Utf8> Function(ffi.Pointer<Utf8>)
        >('gs3d_ffi_pick_file');
    final filter = 'points'.toNativeUtf8();
    try {
      final result = pick(filter);
      if (result == ffi.nullptr) return null;
      final path = result.toDartString();
      return path.isEmpty ? null : path;
    } finally {
      calloc.free(filter);
    }
  });
}
