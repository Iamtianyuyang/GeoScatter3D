import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/pipeline_service.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  const channel = MethodChannel('com.geoscatter3d.ui_flutter/pipeline');
  final messenger =
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;

  test(
    'reconnects to Android job and leaves it running when disposed',
    () async {
      var state = 1;
      var cancellations = 0;
      messenger.setMockMethodCallHandler(channel, (call) async {
        switch (call.method) {
          case 'poll':
            return {
              'id': 9,
              'state': state,
              'stage': 'points',
              'error': '',
              'points': state == 2 ? 12 : 0,
              'chunks': state == 2 ? 1 : 0,
            };
          case 'cancel':
            cancellations++;
            return 0;
          default:
            return 9;
        }
      });
      final service = PipelineService(useAndroidService: true);
      try {
        final observed = Completer<void>();
        service.addListener(() {
          if (service.status.state == PipelineState.running &&
              !observed.isCompleted) {
            observed.complete();
          }
        });
        await observed.future.timeout(const Duration(seconds: 2));
        expect(service.status.id, 9);
        state = 2;
        final finished = Completer<void>();
        service.addListener(() {
          if (service.status.state == PipelineState.done &&
              !finished.isCompleted) {
            finished.complete();
          }
        });
        await finished.future.timeout(const Duration(seconds: 2));
        expect(service.status.points, 12);
      } finally {
        service.dispose();
        await service.closed;
        messenger.setMockMethodCallHandler(channel, null);
      }
      expect(cancellations, 0);
    },
  );

  test('Android polling recovers from a temporary channel error', () async {
    var polls = 0;
    messenger.setMockMethodCallHandler(channel, (call) async {
      if (call.method == 'poll') {
        polls++;
        if (polls == 2) throw PlatformException(code: 'temporary');
        return {
          'id': 3,
          'state': polls >= 3 ? 2 : 1,
          'stage': 'points',
          'error': '',
          'points': 1,
          'chunks': 1,
        };
      }
      return 0;
    });
    final service = PipelineService(useAndroidService: true);
    try {
      final complete = Completer<void>();
      var sawRunningError = false;
      service.addListener(() {
        if (service.status.state == PipelineState.running &&
            service.status.error.isNotEmpty) {
          sawRunningError = true;
        }
        if (service.status.state == PipelineState.done &&
            !complete.isCompleted) {
          complete.complete();
        }
      });
      await complete.future.timeout(const Duration(seconds: 5));
      expect(sawRunningError, isTrue);
    } finally {
      service.dispose();
      messenger.setMockMethodCallHandler(channel, null);
    }
  });

  test(
    'Android start waits for the foreground service and polls its status',
    () async {
      final startResult = Completer<int>();
      var state = 0;
      messenger.setMockMethodCallHandler(channel, (call) async {
        if (call.method == 'start') {
          final result = await startResult.future;
          state = 1;
          return result;
        }
        if (call.method == 'poll') {
          return {
            'id': state == 0 ? 0 : 2,
            'state': state,
            'stage': '',
            'error': '',
            'points': 0,
            'chunks': 0,
          };
        }
        return 0;
      });
      final service = PipelineService(useAndroidService: true);
      try {
        var completed = false;
        final start = service
            .start('/input.csv', '/bundle')
            .then((_) => completed = true);
        await Future<void>.delayed(Duration.zero);
        expect(completed, isFalse);
        startResult.complete(2);
        await start;
        expect(service.status.state, PipelineState.running);
        expect(service.status.id, 2);
      } finally {
        service.dispose();
        messenger.setMockMethodCallHandler(channel, null);
      }
    },
  );
}
