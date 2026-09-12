part of 'geoscatter3d_service.dart';

/// 欢迎页所有耗时操作在状态中枢串行执行，成功以数据和原生首帧为准。
extension WelcomeBackend on GeoScatter3dService {
  Future<Map<String, dynamic>> _executeWelcomeAction(
    String action, [
    Map<String, dynamic> params = const {},
  ]) async {
    if (action == 'welcome.browse_file') {
      final type = (params['type'] as String?) ?? 'point_cloud';
      final picked = await pickFileAsync(type);
      final success = picked != null && picked.isNotEmpty;
      return {
        'success': success,
        'message': success ? '已选择文件: $picked' : '已取消或未选择文件',
        'data': {'path': picked},
      };
    }
    if (action == 'welcome.browse_folder') {
      final picked = await pickFolderAsync();
      final success = picked != null && picked.isNotEmpty;
      return {
        'success': success,
        'message': success ? '已选择目录: $picked' : '已取消或未选择目录',
        'data': {'path': picked},
      };
    }
    if (!const {
      'welcome.quick_demo',
      'welcome.new_project',
      'welcome.open_project',
      'welcome.recent.open',
    }.contains(action)) {
      return executeAction(action, params);
    }
    // 工程动作必须在持久化沙盒准备完毕后再解析 projectsDirectory；
    // bootstrap 已在后台启动同一个 Future，这里只等待，不重复执行。
    await GeoScatter3dService.prepareStorage();
    if (_welcomeBusy) return {'success': false, 'message': '请等待当前工程操作完成'};
    _welcomeBusy = true;
    _welcomeError = null;
    _latestProgress = null;
    _preprocessCancelled = false;
    _notifyWelcome();
    try {
      Map<String, dynamic> result;
      if (action == 'welcome.quick_demo') {
        final bundle = resolveDemoBundle();
        result = bundle != null
            ? await _openWelcomeDataset(bundle)
            : await _buildWelcomeProject(
                'examples/sample-points.csv',
                'sample-points',
                2,
                'xyz',
                50000,
              );
      } else {
        final recentIndex = params['index'];
        final indexedPath =
            action == 'welcome.recent.open' &&
                recentIndex is int &&
                recentIndex >= 0 &&
                recentIndex < _recentProjects.length
            ? _recentProjects[recentIndex].path
            : null;
        final path = (params['path'] as String? ?? indexedPath ?? '').trim();
        if (path.isEmpty) throw const FormatException('请输入数据文件或工程目录路径');
        if (action == 'welcome.new_project' ||
            path.toLowerCase().endsWith('.csv') ||
            path.toLowerCase().endsWith('.dat')) {
          result = await _buildWelcomeProject(
            path,
            params['name'] as String?,
            (params['threads'] as num?)?.toInt() ?? 8,
            params['voxel_mode'] as String? ?? 'xyz',
            (params['finest_target_points'] as num?)?.toInt() ?? 50000,
          );
        } else {
          result = await _openWelcomeDataset(path);
        }
      }
      if (result['success'] != true) {
        _welcomeError = result['message'] as String?;
      }
      return result;
    } catch (error) {
      _welcomeError = error is FormatException
          ? error.message.toString()
          : error.toString();
      return {'success': false, 'message': _welcomeError};
    } finally {
      _welcomeBusy = false;
      _notifyWelcome();
    }
  }

  static String? resolveDemoBundle() => GeoScatter3dService.resolveDatasetPath(
    '${GeoScatter3dService.projectsDirectory}/sample-points.gs3d.bundle',
  );

  Future<Map<String, dynamic>> _openWelcomeDataset(String input) async {
    final path = GeoScatter3dService.resolveDatasetPath(input);
    if (path == null) throw FormatException('工程不存在或已被移动: $input');
    final lower = path.toLowerCase();
    if (!lower.endsWith('.gs3d') && !lower.endsWith('.gs3d.bundle')) {
      throw const FormatException('请选择 .gs3d 文件或 .gs3d.bundle 工程目录');
    }
    await stopNativeRenderer();
    // 先刷新欢迎页的忙碌状态，再执行有界的 FFI 元数据与预览读取。
    await Future<void>.delayed(Duration.zero);
    if (_preprocessCancelled) throw const FormatException('工程操作已取消');
    if (!loadDataset(path, activate: false)) {
      throw FormatException(
        _initialized
            ? 'GS3D 数据校验失败，请检查文件头、长度及工程包内容: $path'
            : '无法加载 gs3d_ffi 动态库，请使用 VS Code F5 构建并启动完整程序',
      );
    }
    if ((Platform.isWindows || Platform.isAndroid) &&
        !await startNativeRenderer()) {
      throw StateError(_nativeViewerError ?? '原生渲染器未能启动');
    }
    if (_preprocessCancelled) {
      await stopNativeRenderer();
      throw const FormatException('工程操作已取消');
    }
    _isWorkbenchActive = true;
    _notifyWelcome();
    return {'success': true, 'message': '工程已载入', 'data': _summary.toJson()};
  }

  Future<Map<String, dynamic>> _buildWelcomeProject(
    String input,
    String? name,
    int threads,
    String voxelMode,
    int finestTargetPoints,
  ) async {
    // Android：从 Flutter assets 提取示例 CSV 到临时目录。
    final isAndroidBundledSample =
        Platform.isAndroid &&
        (input == 'examples/sample-points.csv' ||
            input.endsWith('sample-points.csv'));
    String source;
    if (isAndroidBundledSample) {
      // 先用稳定的逻辑路径完成工程名与冲突校验，避免校验失败时产生孤儿临时目录。
      source = 'sample-points.csv';
    } else {
      final resolved = GeoScatter3dService.resolveDatasetPath(input);
      if (resolved == null || !await File(resolved).exists()) {
        throw FormatException('数据源文件不存在: $input');
      }
      source = resolved;
    }
    final lower = source.toLowerCase();
    if (!lower.endsWith('.csv') && !lower.endsWith('.dat')) {
      throw const FormatException('新建工程需要 CSV 或 DAT 原始数据文件');
    }
    final base = File(source).uri.pathSegments.last;
    final projectName = (name?.trim().isNotEmpty == true)
        ? name!.trim()
        : base.substring(0, base.lastIndexOf('.'));
    if (projectName == '.' ||
        projectName == '..' ||
        RegExp(r'[<>:"/\\|?*\x00-\x1f]').hasMatch(projectName) ||
        projectName.endsWith('.') ||
        projectName.length > 120 ||
        RegExp(
          r'^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)',
          caseSensitive: false,
        ).hasMatch(projectName)) {
      throw const FormatException('工程名称不能包含路径、保留名称或非法字符');
    }
    if (threads < 1 ||
        threads > 256 ||
        !['xy', 'xyz'].contains(voxelMode.toLowerCase()) ||
        finestTargetPoints < 1) {
      throw const FormatException('无效的线程数、体素模式或 LOD 目标点数');
    }
    final root = GeoScatter3dService.resolveRepoRoot();
    final target = Directory(
      '${GeoScatter3dService.projectsDirectory}/$projectName.gs3d.bundle',
    );
    if (await target.exists()) {
      throw FormatException('同名工程已存在，请修改工程名称: ${target.path}');
    }
    final temporaryRoot = Directory('${target.parent.parent.path}/tmp');
    await temporaryRoot.create(recursive: true);
    final temporary = await temporaryRoot.createTemp('welcome-build-');
    final stagedBundle = '${temporary.path}/$projectName.gs3d.bundle';
    String? extractedAssetPath;
    final watch = Stopwatch()..start();
    void progress(
      String stage,
      String detail, {
      bool finished = false,
      bool failed = false,
    }) {
      _latestProgress = PreprocessProgressInfo(
        stage: stage,
        progress: finished ? 1 : 0,
        detail: detail,
        elapsedSeconds: watch.elapsedMilliseconds / 1000,
        isRunning: !finished && !failed,
        isFinished: finished,
        isFailed: failed,
        errorMessage: failed ? detail : null,
      );
      _notifyWelcome();
    }

    try {
      if (isAndroidBundledSample) {
        extractedAssetPath = await _extractAssetToTemp(
          'assets/data/sample-points.csv',
        );
        source = extractedAssetPath;
      }
      progress('正在构建工程', '解析原始数据、生成 LOD 与空间分块…');
      if (Platform.isAndroid) {
        // Android：走 .so 内置预处理器（FFI），无需外部可执行文件。
        await Directory(stagedBundle).create(recursive: true);
        final gs3dPath = '$stagedBundle/source.gs3d';
        final rc = preprocessCsv(
          source,
          gs3dPath,
          threads: threads,
          finestTargetPoints: finestTargetPoints,
        );
        if (rc != 0) {
          throw FormatException('FFI 预处理失败(code $rc): $lastPreprocessError');
        }
        await target.parent.create(recursive: true);
        if (await target.exists()) {
          throw const FormatException('同名工程已存在，请重新命名');
        }
        await Directory(stagedBundle).rename(target.path);
        progress('正在启动原生渲染器', target.path);
        final result = await _openWelcomeDataset(target.path);
        progress('完成', '工程构建完成，原生视口已载入', finished: true);
        return result;
      }
      final executable = GeoScatter3dService.resolvePreprocessExecutable();
      final config = GeoScatter3dService.resolveDatasetPath(
        'config/sample-viewer.toml',
      );
      if (executable == null || config == null) {
        throw const FormatException('缺少预处理程序或配置，请使用 F5 构建完整运行环境');
      }
      final process = await Process.start(executable, [
        '--config',
        config,
        lower.endsWith('.dat') ? '--dat' : '--csv',
        source,
        '--bundle',
        stagedBundle,
        '--threads',
        '$threads',
        '--voxel-mode',
        voxelMode.toUpperCase(),
        '--finest-target-points',
        '$finestTargetPoints',
      ], workingDirectory: root);
      _activePreprocessProcess = process;
      if (_preprocessCancelled) process.kill();
      String lastError = '';
      // 等待两条管道读完，不能在 exitCode 返回时丢失末尾错误输出。
      final stdoutDone = process.stdout
          .transform(const Utf8Decoder(allowMalformed: true))
          .transform(const LineSplitter())
          .forEach((line) {
            if (!_preprocessCancelled && line.trim().isNotEmpty) {
              progress('正在构建工程', line.trim());
            }
          });
      final stderrDone = process.stderr
          .transform(const Utf8Decoder(allowMalformed: true))
          .transform(const LineSplitter())
          .forEach((line) {
            if (line.trim().isNotEmpty) lastError = line.trim();
          });
      final exit = await process.exitCode;
      await Future.wait([stdoutDone, stderrDone]);
      _activePreprocessProcess = null;
      if (_preprocessCancelled) throw const FormatException('构建已取消，临时产物已清理');
      if (exit != 0) throw FormatException('预处理失败（退出码 $exit）: $lastError');
      if (!await File('$stagedBundle/manifest.toml').exists() ||
          !await File('$stagedBundle/source.gs3d').exists()) {
        throw const FormatException('预处理器未生成完整的工程包');
      }
      await target.parent.create(recursive: true);
      // 只发布本次新建的工程，同名工程绝不覆盖。
      if (await target.exists()) throw const FormatException('同名工程已存在，请重新命名');
      await Directory(stagedBundle).rename(target.path);
      progress('正在启动原生渲染器', target.path);
      final result = await _openWelcomeDataset(target.path);
      progress('完成', '工程构建完成，原生视口已载入', finished: true);
      return result;
    } catch (error) {
      progress('未完成', error.toString(), failed: true);
      rethrow;
    } finally {
      final process = _activePreprocessProcess;
      if (process != null) {
        process.kill();
        await process.exitCode;
        _activePreprocessProcess = null;
      }
      // createTemp 返回的专属目录，始终位于项目 tmp 下。
      if (await temporary.exists()) await temporary.delete(recursive: true);
      if (extractedAssetPath != null) {
        final extractedFile = File(extractedAssetPath);
        final extractedDirectory = extractedFile.parent;
        if (await extractedFile.exists()) await extractedFile.delete();
        if (await extractedDirectory.exists()) {
          await extractedDirectory.delete(recursive: true);
        }
      }
    }
  }

  /// 从 Flutter assets 提取文件到应用临时目录，返回绝对路径。
  static Future<String> _extractAssetToTemp(String assetPath) async {
    final data = await rootBundle.load(assetPath);
    final tempDir = Directory.systemTemp.createTempSync('gs3d_asset_');
    final file = File('${tempDir.path}/${assetPath.split('/').last}');
    await file.writeAsBytes(
      data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes),
    );
    return file.path;
  }
}
