import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/widgets/pipeline_panel.dart';

void main() {
  testWidgets('validates input and fills selected paths', (tester) async {
    final source = TextEditingController();
    final bundle = TextEditingController();
    const channel = MethodChannel('com.geoscatter3d.ui_flutter/documents');
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          return call.method == 'pickFile' ? '/tmp/input.csv' : '/tmp/bundle';
        });
    addTearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null);
      source.dispose();
      bundle.dispose();
    });
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: PipelinePanel(
            source: source,
            bundle: bundle,
            pipeline: null,
            getPipeline: () => throw StateError('unexpected pipeline creation'),
          ),
        ),
      ),
    );
    await tester.tap(find.text('创建工程包'));
    await tester.pump();
    expect(find.text('请选择输入文件和工程包目录。'), findsOneWidget);
    await tester.tap(find.byTooltip('选择点云文件'));
    await tester.pumpAndSettle();
    expect(source.text, '/tmp/input.csv');
    expect(bundle.text, '/tmp/input.csv.gs3d.bundle');
    await tester.tap(find.byTooltip('选择工程目录'));
    await tester.pumpAndSettle();
    expect(bundle.text, '/tmp/bundle');
  }, skip: Platform.isWindows);
}
