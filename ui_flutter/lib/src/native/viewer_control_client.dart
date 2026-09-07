import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

/// Flutter 侧原生查看器控制客户端。
///
/// 该客户端只连接由当前进程启动、且仅监听 127.0.0.1 的 GeoScatter3D 实例。
/// 所有渲染与业务命令仍由原生主线程的控制面执行，避免在 Dart 侧复制 Vulkan、
/// LOD 与 tile 流式逻辑。
class NativeViewerControlClient {
  NativeViewerControlClient({this.port = 12735});

  final int port;
  final Map<int, Completer<Map<String, dynamic>>> _pending = {};
  Socket? _socket;
  Process? _process;
  StreamSubscription<String>? _lines;
  int _nextRequestId = 1;

  bool get isConnected => _socket != null;

  Future<bool> start({
    required String executablePath,
    required String datasetPath,
  }) async {
    if (isConnected) return true;

    final executable = File(executablePath);
    if (!await executable.exists()) return false;

    final path = datasetPath.toLowerCase();
    final inputFlag = path.endsWith('.gs3d.bundle') ? '--bundle' : '--input';
    _process = await Process.start(
      executable.path,
      [
        '--no-welcome',
        '--headless',
        '--control-plane=$port',
        inputFlag,
        datasetPath,
      ],
      mode: ProcessStartMode.detachedWithStdio,
      workingDirectory: executable.parent.path,
    );

    final deadline = DateTime.now().add(const Duration(seconds: 10));
    while (DateTime.now().isBefore(deadline)) {
      try {
        final socket = await Socket.connect(
          InternetAddress.loopbackIPv4,
          port,
          timeout: const Duration(milliseconds: 500),
        );
        _attach(socket);
        await getViewportState();
        return true;
      } on SocketException {
        await Future<void>.delayed(const Duration(milliseconds: 100));
      } on TimeoutException {
        await Future<void>.delayed(const Duration(milliseconds: 100));
      }
    }

    await stop(force: true);
    return false;
  }

  Future<Map<String, dynamic>> getViewportState() {
    return request('get_state', {'component': 'viewport.main'});
  }

  Future<Uint8List> capturePng() async {
    final response = await request('screenshot', const {});
    final result = response['result'];
    if (result is! Map<String, dynamic>) {
      throw StateError('原生查看器未返回截图结果');
    }
    final data = result['data'];
    if (data is! String || data.isEmpty) {
      throw StateError('原生查看器截图为空');
    }
    return base64Decode(data);
  }

  Future<Map<String, dynamic>> request(
    String method,
    Map<String, dynamic> params,
  ) {
    final socket = _socket;
    if (socket == null) {
      return Future<Map<String, dynamic>>.error(StateError('原生查看器控制面尚未连接'));
    }

    final id = _nextRequestId++;
    final completer = Completer<Map<String, dynamic>>();
    _pending[id] = completer;
    socket.write(
      jsonEncode({
        'jsonrpc': '2.0',
        'id': id,
        'method': method,
        'params': params,
      }),
    );
    socket.write('\n');
    return completer.future.timeout(
      const Duration(seconds: 5),
      onTimeout: () {
        _pending.remove(id);
        throw TimeoutException('原生查看器命令超时: $method');
      },
    );
  }

  Future<void> stop({bool force = false}) async {
    try {
      if (!force && isConnected) {
        await request('quit', const {}).timeout(const Duration(seconds: 2));
      }
    } catch (_) {
      force = true;
    }

    await _lines?.cancel();
    _lines = null;
    await _socket?.close();
    _socket = null;
    for (final pending in _pending.values) {
      if (!pending.isCompleted) {
        pending.completeError(StateError('原生查看器已停止'));
      }
    }
    _pending.clear();

    final process = _process;
    _process = null;
    if (process == null) return;

    if (!force) {
      final exitCode = await process.exitCode.timeout(
        const Duration(seconds: 3),
        onTimeout: () => -1,
      );
      if (exitCode != -1) return;
      force = true;
    }
    if (force) {
      process.kill(ProcessSignal.sigterm);
      await process.exitCode.timeout(
        const Duration(seconds: 3),
        onTimeout: () => -1,
      );
    }
  }

  void _attach(Socket socket) {
    _socket = socket;
    _lines = socket
        .cast<List<int>>()
        .transform(utf8.decoder)
        .transform(const LineSplitter())
        .listen(
          _handleLine,
          onDone: _handleDisconnect,
          onError: (Object error, StackTrace stackTrace) => _handleDisconnect(),
        );
  }

  void _handleLine(String line) {
    try {
      final decoded = jsonDecode(line);
      if (decoded is! Map<String, dynamic>) return;
      final id = decoded['id'];
      if (id is! int) return;
      final pending = _pending.remove(id);
      if (pending == null || pending.isCompleted) return;
      if (decoded['error'] != null) {
        pending.completeError(StateError(decoded['error'].toString()));
      } else {
        pending.complete(decoded);
      }
    } catch (_) {
      // 无法解析的控制面消息不应终止现有命令序列。
    }
  }

  void _handleDisconnect() {
    _socket = null;
  }
}
