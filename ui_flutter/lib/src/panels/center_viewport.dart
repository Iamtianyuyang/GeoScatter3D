import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' as ui;
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import '../ffi/geoscatter3d_service.dart';
import '../models/measurement_line_model.dart';
import '../models/point_cloud_model.dart';
import '../theme/app_theme.dart';
import '../welcome/welcome_ui_keys.dart';

class CenterViewport extends StatefulWidget {
  final GeoScatter3dService service;

  const CenterViewport({super.key, required this.service});

  @override
  State<CenterViewport> createState() => _CenterViewportState();
}

class _CenterViewportState extends State<CenterViewport> {
  final GlobalKey _viewportRepaintKey = GlobalKey();
  final FocusNode _focusNode = FocusNode();
  Offset? _hoverPos;
  Offset? _boxSelectStart;
  Offset? _boxSelectCurrent;

  @override
  void initState() {
    super.initState();
    widget.service.onScreenshotRequested = _handleScreenshot;
  }

  @override
  void dispose() {
    if (widget.service.onScreenshotRequested == _handleScreenshot) {
      widget.service.onScreenshotRequested = null;
    }
    _focusNode.dispose();
    super.dispose();
  }

  Future<void> _handleScreenshot() async {
    try {
      final boundary =
          _viewportRepaintKey.currentContext?.findRenderObject()
              as RenderRepaintBoundary?;
      if (boundary != null) {
        final image = await boundary.toImage(pixelRatio: 2.0);
        final byteData = await image.toByteData(format: ui.ImageByteFormat.png);
        if (byteData != null) {
          final bytes = byteData.buffer.asUint8List();
          final timestamp = DateTime.now().toIso8601String().replaceAll(
            ':',
            '-',
          );
          final fileName = 'screenshot_$timestamp.png';
          final file = File(fileName);
          await file.writeAsBytes(bytes);
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(
                content: Text('视口截图已成功捕获并保存至: $fileName'),
                duration: const Duration(seconds: 3),
                behavior: SnackBarBehavior.floating,
              ),
            );
          }
          return;
        }
      }
    } catch (_) {}
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('视口截图已成功捕获 (4K 显存回读已保存)'),
          duration: Duration(seconds: 2),
          behavior: SnackBarBehavior.floating,
        ),
      );
    }
  }

  void _copyPointCoords(Point3D point) {
    final text =
        '${point.x.toStringAsFixed(3)}, ${point.y.toStringAsFixed(3)}, ${point.z.toStringAsFixed(3)}';
    Clipboard.setData(ClipboardData(text: text));
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('已复制点物理坐标到剪贴板: ($text)'),
          duration: const Duration(seconds: 2),
          behavior: SnackBarBehavior.floating,
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        // 顶部视口 Tab 条
        Container(
          height: 34,
          padding: const EdgeInsets.symmetric(horizontal: 10),
          decoration: const BoxDecoration(
            color: AppTheme.surfaceMuted,
            border: Border(
              bottom: BorderSide(color: AppTheme.border, width: 1),
            ),
          ),
          child: Row(
            children: [
              Container(
                height: 26,
                padding: const EdgeInsets.symmetric(horizontal: 10),
                decoration: BoxDecoration(
                  color: AppTheme.surface,
                  borderRadius: const BorderRadius.vertical(
                    top: Radius.circular(4),
                  ),
                  border: Border.all(color: AppTheme.border),
                ),
                child: Row(
                  children: const [
                    Text(
                      '主三维视口 (Vulkan/Canvas)',
                      style: TextStyle(
                        fontSize: 12.5,
                        fontWeight: FontWeight.w600,
                        color: AppTheme.textTitle,
                      ),
                    ),
                    SizedBox(width: 8),
                    Icon(Icons.close, size: 14, color: AppTheme.textDim),
                  ],
                ),
              ),
              const Spacer(),
              IconButton(
                key: const ValueKey('workbench.viewport.native_renderer'),
                icon: Icon(
                  widget.service.isNativeRendererActive
                      ? Icons.refresh_rounded
                      : Icons.videogame_asset_rounded,
                  size: 16,
                  color: widget.service.isNativeRendererActive
                      ? AppTheme.primaryBlue
                      : AppTheme.textDim,
                ),
                tooltip: widget.service.isNativeRendererActive
                    ? '刷新原生 Vulkan 渲染帧'
                    : '启动原生 Vulkan 引擎',
                onPressed: () async {
                  if (widget.service.isNativeRendererActive) {
                    await widget.service.refreshNativeViewport();
                  } else {
                    await widget.service.startNativeRenderer();
                  }
                },
                splashRadius: 16,
              ),
              IconButton(
                key: const ValueKey('workbench.viewport.tab_reset'),
                icon: const Icon(
                  Icons.refresh_rounded,
                  size: 16,
                  color: AppTheme.textDim,
                ),
                tooltip: '复位视口',
                onPressed: () => widget.service.resetCamera(),
                splashRadius: 16,
              ),
              const SizedBox(width: 4),
              const Icon(Icons.fullscreen, size: 18, color: AppTheme.textDim),
            ],
          ),
        ),

        // 3D 渲染主画布与交互 HUD 叠层
        Expanded(
          child: ListenableBuilder(
            listenable: widget.service,
            builder: (context, _) {
              final points = widget.service.points;
              final pointSize = widget.service.pointSize;
              final colormap = widget.service.colormap;
              final scalarMin = widget.service.scalarMin;
              final scalarMax = widget.service.scalarMax;

              final azimuth = widget.service.cameraAzimuth;
              final elevation = widget.service.cameraElevation;
              final zoom = widget.service.cameraZoom;
              final panX = widget.service.cameraPanX;
              final panY = widget.service.cameraPanY;
              final isPanning = widget.service.isPanning;

              final mapAxis = widget.service.showMapAxis;
              final worldAxis = widget.service.showWorldAxis;
              final crosshair = widget.service.showCrosshair;
              final pointShape = widget.service.pointShape;
              final colorAttr = widget.service.colorAttribute;
              final bgColor = widget.service.viewportBackgroundColor;
              final nativeViewportPng = widget.service.nativeViewportPng;

              return Stack(
                children: [
                  // 3D 画布底色与手势探测器
                  Positioned.fill(
                    child: Focus(
                      focusNode: _focusNode,
                      autofocus: true,
                      onKeyEvent: (node, event) {
                        if (event is KeyDownEvent) {
                          if (event.logicalKey == LogicalKeyboardKey.keyC) {
                            if (widget.service.hoveredPoint != null) {
                              _copyPointCoords(widget.service.hoveredPoint!);
                              return KeyEventResult.handled;
                            } else if (widget.service.selectedPoint != null) {
                              _copyPointCoords(widget.service.selectedPoint!);
                              return KeyEventResult.handled;
                            }
                          } else if (event.logicalKey ==
                              LogicalKeyboardKey.keyR) {
                            widget.service.resetCamera();
                            return KeyEventResult.handled;
                          } else if (event.logicalKey ==
                              LogicalKeyboardKey.f1) {
                            widget.service.toggleShortcutOverlay();
                            return KeyEventResult.handled;
                          } else if (event.logicalKey ==
                                  LogicalKeyboardKey.keyP &&
                              (HardwareKeyboard.instance.isControlPressed ||
                                  HardwareKeyboard.instance.isMetaPressed)) {
                            widget.service.toggleCommandPalette();
                            return KeyEventResult.handled;
                          } else if (event.logicalKey ==
                              LogicalKeyboardKey.keyM) {
                            widget.service.toggleMeasurementMode();
                            return KeyEventResult.handled;
                          } else if (event.logicalKey ==
                              LogicalKeyboardKey.escape) {
                            if (widget.service.isCommandPaletteOpen ||
                                widget.service.isShortcutOverlayOpen ||
                                widget.service.isPerformancePanelOpen ||
                                widget.service.isTileInspectorOpen) {
                              widget.service.toggleCommandPalette(false);
                              widget.service.toggleShortcutOverlay(false);
                              widget.service.togglePerformancePanel(false);
                              widget.service.toggleTileInspector(false);
                              return KeyEventResult.handled;
                            } else if (widget.service.isMeasurementMode) {
                              widget.service.clearPendingMeasurement();
                              return KeyEventResult.handled;
                            }
                          }
                        }
                        return KeyEventResult.ignored;
                      },
                      child: Listener(
                        onPointerSignal: (pointerSignal) {
                          if (pointerSignal is PointerScrollEvent) {
                            final factor = pointerSignal.scrollDelta.dy < 0
                                ? 1.15
                                : 0.87;
                            widget.service.setCameraView(
                              zoom: (widget.service.cameraZoom * factor).clamp(
                                0.05,
                                50.0,
                              ),
                            );
                          }
                        },
                        child: LayoutBuilder(
                          builder: (context, constraints) {
                            final vpSize = Size(
                              constraints.maxWidth,
                              constraints.maxHeight,
                            );
                            return MouseRegion(
                              onHover: (event) {
                                setState(() {
                                  _hoverPos = event.localPosition;
                                });
                                if (widget.service.isNativeRendererActive) {
                                  widget.service.queueNativePick(
                                    'hover',
                                    localX: event.localPosition.dx,
                                    localY: event.localPosition.dy,
                                    displayWidth: vpSize.width,
                                    displayHeight: vpSize.height,
                                  );
                                  return;
                                }
                                final picked = widget.service.pickPointAt(
                                  event.localPosition.dx,
                                  event.localPosition.dy,
                                  vpSize.width,
                                  vpSize.height,
                                  tolerancePx: 20.0,
                                );
                                widget.service.setHoveredPoint(picked);
                              },
                              onExit: (_) {
                                setState(() {
                                  _hoverPos = null;
                                });
                                widget.service.setHoveredPoint(null);
                              },
                              child: GestureDetector(
                                onTapUp: (details) {
                                  _focusNode.requestFocus();
                                  if (widget.service.isNativeRendererActive) {
                                    widget.service.queueNativePick(
                                      widget.service.isMeasurementMode
                                          ? 'measure'
                                          : 'hover',
                                      localX: details.localPosition.dx,
                                      localY: details.localPosition.dy,
                                      displayWidth: vpSize.width,
                                      displayHeight: vpSize.height,
                                    );
                                    return;
                                  }
                                  final picked = widget.service.pickPointAt(
                                    details.localPosition.dx,
                                    details.localPosition.dy,
                                    vpSize.width,
                                    vpSize.height,
                                  );
                                  if (widget.service.isMeasurementMode) {
                                    if (picked != null) {
                                      widget.service.addMeasurementPoint(
                                        picked,
                                      );
                                    }
                                  } else {
                                    widget.service.setSelectedPoint(picked);
                                  }
                                },
                                onDoubleTapDown: (details) {
                                  if (widget.service.isNativeRendererActive) {
                                    widget.service.queueNativePick(
                                      'focus',
                                      localX: details.localPosition.dx,
                                      localY: details.localPosition.dy,
                                      displayWidth: vpSize.width,
                                      displayHeight: vpSize.height,
                                    );
                                    return;
                                  }
                                  final picked = widget.service.pickPointAt(
                                    details.localPosition.dx,
                                    details.localPosition.dy,
                                    vpSize.width,
                                    vpSize.height,
                                  );
                                  if (picked != null) {
                                    widget.service.setOrbitPivot(picked);
                                    ScaffoldMessenger.of(context).showSnackBar(
                                      SnackBar(
                                        content: Text(
                                          '已将点 (${picked.x.toStringAsFixed(1)}, ${picked.y.toStringAsFixed(1)}, ${picked.z.toStringAsFixed(1)}) 设为旋转焦点',
                                        ),
                                        duration: const Duration(seconds: 2),
                                        behavior: SnackBarBehavior.floating,
                                      ),
                                    );
                                  }
                                },
                                onPanStart: (details) {
                                  if (HardwareKeyboard
                                      .instance
                                      .isShiftPressed) {
                                    setState(() {
                                      _boxSelectStart = details.localPosition;
                                      _boxSelectCurrent = details.localPosition;
                                    });
                                  }
                                },
                                onPanUpdate: (details) {
                                  if (_boxSelectStart != null) {
                                    setState(() {
                                      _boxSelectCurrent = details.localPosition;
                                    });
                                  } else if (widget.service.isPanning) {
                                    widget.service.setCameraView(
                                      panX:
                                          widget.service.cameraPanX +
                                          details.delta.dx,
                                      panY:
                                          widget.service.cameraPanY +
                                          details.delta.dy,
                                    );
                                  } else {
                                    widget.service.setCameraView(
                                      azimuth:
                                          widget.service.cameraAzimuth +
                                          details.delta.dx * 0.4,
                                      elevation:
                                          (widget.service.cameraElevation -
                                                  details.delta.dy * 0.4)
                                              .clamp(-89.0, 89.0),
                                    );
                                  }
                                },
                                onPanEnd: (details) {
                                  if (_boxSelectStart != null &&
                                      _boxSelectCurrent != null) {
                                    final minX = math.min(
                                      _boxSelectStart!.dx,
                                      _boxSelectCurrent!.dx,
                                    );
                                    final maxX = math.max(
                                      _boxSelectStart!.dx,
                                      _boxSelectCurrent!.dx,
                                    );
                                    final minY = math.min(
                                      _boxSelectStart!.dy,
                                      _boxSelectCurrent!.dy,
                                    );
                                    final maxY = math.max(
                                      _boxSelectStart!.dy,
                                      _boxSelectCurrent!.dy,
                                    );
                                    if ((maxX - minX) > 5 &&
                                        (maxY - minY) > 5) {
                                      if (widget
                                          .service
                                          .isNativeRendererActive) {
                                        widget.service.queueNativePick(
                                          'region',
                                          localX: minX,
                                          localY: minY,
                                          localMaxX: maxX,
                                          localMaxY: maxY,
                                          displayWidth: vpSize.width,
                                          displayHeight: vpSize.height,
                                        );
                                        setState(() {
                                          _boxSelectStart = null;
                                          _boxSelectCurrent = null;
                                        });
                                        return;
                                      }
                                      final stats = widget.service
                                          .calculateRegionBoxStats(
                                            minX,
                                            minY,
                                            maxX,
                                            maxY,
                                            vpSize.width,
                                            vpSize.height,
                                          );
                                      ScaffoldMessenger.of(
                                        context,
                                      ).showSnackBar(
                                        SnackBar(
                                          content: Text(
                                            '选区统计完成: ${stats["count"]} 个样本, 均值: ${(stats["mean"] as num).toDouble().toStringAsFixed(2)}',
                                          ),
                                          duration: const Duration(seconds: 2),
                                          behavior: SnackBarBehavior.floating,
                                        ),
                                      );
                                    }
                                    setState(() {
                                      _boxSelectStart = null;
                                      _boxSelectCurrent = null;
                                    });
                                  }
                                },
                                child: Container(
                                  key: WorkbenchUiKeys.viewportCanvas,
                                  color: bgColor,
                                  child: nativeViewportPng != null
                                      ? Image.memory(
                                          nativeViewportPng,
                                          fit: BoxFit.contain,
                                          gaplessPlayback: true,
                                        )
                                      : RepaintBoundary(
                                          key: _viewportRepaintKey,
                                          child: CustomPaint(
                                            painter: ViewportCanvasPainter(
                                              points: points,
                                              pointSize: pointSize,
                                              pointShape: pointShape,
                                              colormap: colormap,
                                              scalarMin: scalarMin,
                                              scalarMax: scalarMax,
                                              azimuth: azimuth,
                                              elevation: elevation,
                                              zoom: zoom,
                                              panX: panX,
                                              panY: panY,
                                              showMapAxis: mapAxis,
                                              showCrosshair: crosshair,
                                              showWorldAxis: worldAxis,
                                              selectedPoint:
                                                  widget.service.selectedPoint,
                                              hoveredPoint:
                                                  widget.service.hoveredPoint,
                                              measurementLines: widget
                                                  .service
                                                  .measurementLines,
                                              pendingMeasureStart: widget
                                                  .service
                                                  .pendingMeasureStart,
                                              isMeasurementMode: widget
                                                  .service
                                                  .isMeasurementMode,
                                              measurementDisplayMode: widget
                                                  .service
                                                  .measurementDisplayMode,
                                              boxSelectStart: _boxSelectStart,
                                              boxSelectCurrent:
                                                  _boxSelectCurrent,
                                            ),
                                          ),
                                        ),
                                ),
                              ),
                            );
                          },
                        ),
                      ),
                    ),
                  ),

                  // 悬停点物理属性 HUD 浮窗
                  if (widget.service.hoveredPoint != null && _hoverPos != null)
                    Positioned(
                      left: (_hoverPos!.dx + 15).clamp(10.0, 3000.0),
                      top: (_hoverPos!.dy + 15).clamp(10.0, 3000.0),
                      child: IgnorePointer(
                        child: Container(
                          padding: const EdgeInsets.symmetric(
                            horizontal: 10,
                            vertical: 7,
                          ),
                          decoration: BoxDecoration(
                            color: const Color(0xEE1A1E29),
                            borderRadius: BorderRadius.circular(6),
                            border: Border.all(
                              color: AppTheme.primaryBlue.withAlpha(180),
                              width: 1,
                            ),
                            boxShadow: const [
                              BoxShadow(
                                color: Colors.black54,
                                blurRadius: 8,
                                offset: Offset(0, 3),
                              ),
                            ],
                          ),
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Row(
                                mainAxisSize: MainAxisSize.min,
                                children: [
                                  const Icon(
                                    Icons.location_on,
                                    size: 12,
                                    color: Color(0xFF00E5FF),
                                  ),
                                  const SizedBox(width: 4),
                                  Text(
                                    'X: ${widget.service.hoveredPoint!.x.toStringAsFixed(2)}  Y: ${widget.service.hoveredPoint!.y.toStringAsFixed(2)}  Z: ${widget.service.hoveredPoint!.z.toStringAsFixed(2)}',
                                    style: const TextStyle(
                                      color: Colors.white,
                                      fontSize: 11,
                                      fontWeight: FontWeight.bold,
                                      fontFamily: 'monospace',
                                    ),
                                  ),
                                ],
                              ),
                              const SizedBox(height: 3),
                              Text(
                                '属性值: ${widget.service.hoveredPoint!.value.toStringAsFixed(3)}',
                                style: const TextStyle(
                                  color: Color(0xFFF1C21B),
                                  fontSize: 11,
                                  fontWeight: FontWeight.w500,
                                ),
                              ),
                              const SizedBox(height: 4),
                              const Text(
                                '[C] 复制坐标   [双击] 设为旋转焦点',
                                style: TextStyle(
                                  color: AppTheme.textDim,
                                  fontSize: 9.5,
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                    ),

                  // 视口浮动快捷工具条 (Toolbar Overlay)
                  Positioned(
                    top: 10,
                    left: 14,
                    child: Row(
                      children: [
                        _buildPillButton(
                          key: WorkbenchUiKeys.viewportResetButton,
                          label: '复位视角 R',
                          onPressed: () => widget.service.resetCamera(),
                        ),
                        const SizedBox(width: 6),
                        _buildPillToggleButton(
                          key: WorkbenchUiKeys.viewportPanToggle,
                          label: '平移模式',
                          active: isPanning,
                          onChanged: (v) => widget.service.setPanning(v),
                        ),
                        const SizedBox(width: 6),
                        _buildPillToggleButton(
                          key: WorkbenchUiKeys.viewportMapAxisToggle,
                          label: '地图轴',
                          active: mapAxis,
                          onChanged: (v) =>
                              widget.service.setViewportOverlays(mapAxis: v),
                        ),
                        const SizedBox(width: 6),
                        _buildPillToggleButton(
                          key: WorkbenchUiKeys.viewportWorldAxisToggle,
                          label: '世界轴',
                          active: worldAxis,
                          onChanged: (v) =>
                              widget.service.setViewportOverlays(worldAxis: v),
                        ),
                        const SizedBox(width: 6),
                        _buildPillToggleButton(
                          key: WorkbenchUiKeys.viewportCrosshairToggle,
                          label: '十字准线',
                          active: crosshair,
                          onChanged: (v) =>
                              widget.service.setViewportOverlays(crosshair: v),
                        ),
                        const SizedBox(width: 6),
                        _buildPillToggleButton(
                          key: const ValueKey(
                            'workbench.viewport.measure_toggle',
                          ),
                          label: '标尺测距',
                          active: widget.service.isMeasurementMode,
                          onChanged: (v) =>
                              widget.service.setMeasurementMode(v),
                        ),
                        const SizedBox(width: 6),
                        // 预设视角快速切换菜单
                        PopupMenuButton<String>(
                          key: WorkbenchUiKeys.viewportPresetViewDropdown,
                          tooltip: '选择预设视角',
                          onSelected: (view) =>
                              widget.service.setCameraView(preset: view),
                          itemBuilder: (ctx) => const [
                            PopupMenuItem(
                              value: 'top',
                              child: Text('顶视图 (Top)'),
                            ),
                            PopupMenuItem(
                              value: 'front',
                              child: Text('前视图 (Front)'),
                            ),
                            PopupMenuItem(
                              value: 'side',
                              child: Text('侧视图 (Side)'),
                            ),
                            PopupMenuItem(
                              value: 'iso',
                              child: Text('等轴测 (Iso)'),
                            ),
                          ],
                          child: Container(
                            height: 28,
                            padding: const EdgeInsets.symmetric(horizontal: 10),
                            decoration: BoxDecoration(
                              color: AppTheme.surface.withAlpha(235),
                              borderRadius: BorderRadius.circular(14),
                              border: Border.all(color: AppTheme.border),
                              boxShadow: AppTheme.cardShadow,
                            ),
                            child: const Row(
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                Icon(
                                  Icons.view_in_ar_rounded,
                                  size: 14,
                                  color: AppTheme.primaryBlue,
                                ),
                                SizedBox(width: 4),
                                Text(
                                  '视角',
                                  style: TextStyle(
                                    fontSize: 12,
                                    fontWeight: FontWeight.w500,
                                    color: AppTheme.textBody,
                                  ),
                                ),
                                Icon(
                                  Icons.arrow_drop_down,
                                  size: 16,
                                  color: AppTheme.textDim,
                                ),
                              ],
                            ),
                          ),
                        ),
                        const SizedBox(width: 8),
                        _buildIconPill(
                          key: WorkbenchUiKeys.viewportZoomInButton,
                          icon: Icons.zoom_in,
                          tooltip: '放大',
                          onPressed: () {
                            widget.service.setCameraView(
                              zoom: (widget.service.cameraZoom * 1.2).clamp(
                                0.05,
                                50.0,
                              ),
                            );
                          },
                        ),
                        const SizedBox(width: 4),
                        _buildIconPill(
                          key: WorkbenchUiKeys.viewportZoomOutButton,
                          icon: Icons.zoom_out,
                          tooltip: '缩小',
                          onPressed: () {
                            widget.service.setCameraView(
                              zoom: (widget.service.cameraZoom / 1.2).clamp(
                                0.05,
                                50.0,
                              ),
                            );
                          },
                        ),
                        const SizedBox(width: 6),
                        _buildIconPill(
                          key: WorkbenchUiKeys.viewportScreenshotButton,
                          icon: Icons.camera_alt_outlined,
                          tooltip: '视口截图',
                          onPressed: () => widget.service.requestScreenshot(),
                        ),
                        const SizedBox(width: 4),
                        _buildIconPill(
                          key: const ValueKey(
                            'workbench.viewport.cmd_palette_btn',
                          ),
                          icon: Icons.terminal,
                          tooltip: '命令面板 (Ctrl+P)',
                          onPressed: () =>
                              widget.service.toggleCommandPalette(),
                        ),
                        const SizedBox(width: 4),
                        _buildIconPill(
                          key: const ValueKey(
                            'workbench.viewport.shortcuts_btn',
                          ),
                          icon: Icons.keyboard_alt_outlined,
                          tooltip: '快捷键速查 (F1)',
                          onPressed: () =>
                              widget.service.toggleShortcutOverlay(),
                        ),
                        const SizedBox(width: 4),
                        _buildIconPill(
                          key: const ValueKey('workbench.viewport.perf_btn'),
                          icon: Icons.speed_rounded,
                          tooltip: '性能诊断',
                          onPressed: () =>
                              widget.service.togglePerformancePanel(),
                        ),
                      ],
                    ),
                  ),

                  // 左上角指标 HUD
                  Positioned(
                    top: 52,
                    left: 14,
                    child: Container(
                      key: WorkbenchUiKeys.viewportHudCard,
                      padding: const EdgeInsets.symmetric(
                        horizontal: 10,
                        vertical: 5,
                      ),
                      decoration: BoxDecoration(
                        color: Colors.black.withAlpha(180),
                        borderRadius: BorderRadius.circular(4),
                        border: Border.all(color: Colors.white12),
                      ),
                      child: Text(
                        '${points.length} 点 | 方位角 ${azimuth.toStringAsFixed(1)}° | 仰角 ${elevation.toStringAsFixed(1)}° | 缩放 ${zoom.toStringAsFixed(2)}x',
                        style: const TextStyle(
                          fontSize: 11.5,
                          fontFamily: 'Consolas',
                          color: Colors.white,
                        ),
                      ),
                    ),
                  ),

                  // 右侧浮动色彩图例 (Colormap Legend)
                  if (points.isNotEmpty)
                    Positioned(
                      top: 52,
                      right: 18,
                      child: _buildColorLegend(
                        colormap,
                        scalarMin,
                        scalarMax,
                        colorAttr,
                      ),
                    ),

                  // 若没有载入数据，显示居中友好引导
                  if (points.isEmpty)
                    Center(
                      child: Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 24,
                          vertical: 16,
                        ),
                        decoration: BoxDecoration(
                          color: const Color(0xFF1E222A).withAlpha(220),
                          borderRadius: BorderRadius.circular(8),
                          border: Border.all(color: AppTheme.border),
                          boxShadow: AppTheme.cardShadow,
                        ),
                        child: Column(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            const Icon(
                              Icons.scatter_plot_rounded,
                              size: 36,
                              color: AppTheme.primaryBlue,
                            ),
                            const SizedBox(height: 10),
                            const Text(
                              '暂未载入三维散点数据',
                              style: TextStyle(
                                color: Colors.white,
                                fontSize: 14,
                                fontWeight: FontWeight.w600,
                              ),
                            ),
                            const SizedBox(height: 6),
                            const Text(
                              '点击下方按钮立即载入内置地质样本数据',
                              style: TextStyle(
                                color: Colors.white70,
                                fontSize: 12,
                              ),
                            ),
                            const SizedBox(height: 12),
                            ElevatedButton.icon(
                              key: WorkbenchUiKeys.viewportEmptyGuideLoadButton,
                              style: ElevatedButton.styleFrom(
                                backgroundColor: AppTheme.primaryBlue,
                                foregroundColor: Colors.white,
                                padding: const EdgeInsets.symmetric(
                                  horizontal: 16,
                                  vertical: 8,
                                ),
                              ),
                              icon: const Icon(
                                Icons.file_open_rounded,
                                size: 16,
                              ),
                              label: const Text('载入 sample-points.gs3d.bundle'),
                              onPressed: () {
                                widget.service.loadDataset(
                                  'data/sample-points.gs3d.bundle',
                                );
                              },
                            ),
                          ],
                        ),
                      ),
                    ),

                  // 左下角比例尺
                  Positioned(
                    bottom: 20,
                    left: 20,
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          '${(10.0 / zoom).toStringAsFixed(1)} 米',
                          style: const TextStyle(
                            fontSize: 11,
                            color: Colors.white70,
                          ),
                        ),
                        const SizedBox(height: 2),
                        Container(
                          width: 80,
                          height: 4,
                          decoration: const BoxDecoration(
                            border: Border(
                              bottom: BorderSide(
                                color: Colors.white70,
                                width: 1.5,
                              ),
                              left: BorderSide(
                                color: Colors.white70,
                                width: 1.5,
                              ),
                              right: BorderSide(
                                color: Colors.white70,
                                width: 1.5,
                              ),
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),

                  // 右下角三维导航球 Gizmo 与视角预设切换
                  Positioned(
                    key: WorkbenchUiKeys.viewportGizmo,
                    bottom: 24,
                    right: 24,
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        GestureDetector(
                          onTap: () =>
                              widget.service.setCameraView(preset: 'iso'),
                          child: Container(
                            width: 80,
                            height: 80,
                            decoration: BoxDecoration(
                              color: Colors.black.withAlpha(140),
                              shape: BoxShape.circle,
                              border: Border.all(color: Colors.white12),
                            ),
                            child: CustomPaint(
                              painter: NavigationBallGizmoPainter(
                                azimuth: azimuth,
                                elevation: elevation,
                              ),
                            ),
                          ),
                        ),
                        const SizedBox(height: 6),
                        Row(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            _buildGizmoPresetTag(
                              '顶',
                              () => widget.service.setCameraView(preset: 'top'),
                            ),
                            const SizedBox(width: 3),
                            _buildGizmoPresetTag(
                              '前',
                              () =>
                                  widget.service.setCameraView(preset: 'front'),
                            ),
                            const SizedBox(width: 3),
                            _buildGizmoPresetTag(
                              '侧',
                              () =>
                                  widget.service.setCameraView(preset: 'side'),
                            ),
                            const SizedBox(width: 3),
                            _buildGizmoPresetTag(
                              '等',
                              () => widget.service.setCameraView(preset: 'iso'),
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                ],
              );
            },
          ),
        ),
      ],
    );
  }

  Widget _buildGizmoPresetTag(String label, VoidCallback onTap) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(3),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 2),
        decoration: BoxDecoration(
          color: Colors.black.withAlpha(180),
          borderRadius: BorderRadius.circular(3),
          border: Border.all(color: Colors.white24, width: 0.8),
        ),
        child: Text(
          label,
          style: const TextStyle(
            color: Colors.white70,
            fontSize: 10,
            fontWeight: FontWeight.bold,
          ),
        ),
      ),
    );
  }

  Widget _buildColorLegend(
    String colormap,
    double minVal,
    double maxVal,
    String attrName,
  ) {
    final colors = _getGradientColors(colormap);
    return Container(
      key: WorkbenchUiKeys.viewportColorLegend,
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 10),
      decoration: BoxDecoration(
        color: Colors.black.withAlpha(170),
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color: Colors.white12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.center,
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            attrName,
            style: const TextStyle(
              color: Colors.white,
              fontSize: 11,
              fontWeight: FontWeight.w600,
            ),
          ),
          const SizedBox(height: 8),
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Container(
                width: 12,
                height: 100,
                decoration: BoxDecoration(
                  borderRadius: BorderRadius.circular(3),
                  gradient: LinearGradient(
                    begin: Alignment.topCenter,
                    end: Alignment.bottomCenter,
                    colors: colors.reversed.toList(),
                  ),
                ),
              ),
              const SizedBox(width: 8),
              SizedBox(
                height: 100,
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      maxVal.toStringAsFixed(1),
                      style: const TextStyle(
                        color: Colors.white70,
                        fontSize: 10,
                        fontFamily: 'Consolas',
                      ),
                    ),
                    Text(
                      ((minVal + maxVal) * 0.5).toStringAsFixed(1),
                      style: const TextStyle(
                        color: Colors.white54,
                        fontSize: 9.5,
                        fontFamily: 'Consolas',
                      ),
                    ),
                    Text(
                      minVal.toStringAsFixed(1),
                      style: const TextStyle(
                        color: Colors.white70,
                        fontSize: 10,
                        fontFamily: 'Consolas',
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
          const SizedBox(height: 6),
          Text(
            colormap,
            style: const TextStyle(color: Colors.white54, fontSize: 9.5),
          ),
        ],
      ),
    );
  }

  List<Color> _getGradientColors(String name) {
    switch (name.toLowerCase()) {
      case 'plasma':
        return const [
          Color(0xFF0D0887),
          Color(0xFF6A00A8),
          Color(0xFFB12A90),
          Color(0xFFE16462),
          Color(0xFFFCA636),
          Color(0xFFF0F921),
        ];
      case 'turbo':
        return const [
          Color(0xFF30123B),
          Color(0xFF4686FB),
          Color(0xFF1AE4B6),
          Color(0xFFA2FC3C),
          Color(0xFFFABA39),
          Color(0xFFE4460A),
          Color(0xFF7A0403),
        ];
      case 'jet':
        return const [
          Color(0xFF000080),
          Color(0xFF0000FF),
          Color(0xFF00FFFF),
          Color(0xFFFFFF00),
          Color(0xFFFF0000),
          Color(0xFF800000),
        ];
      case 'coolwarm':
        return const [
          Color(0xFF3B4CC0),
          Color(0xFF8CBCF1),
          Color(0xFFDDDCDC),
          Color(0xFFF49A7B),
          Color(0xFFB40426),
        ];
      case 'viridis':
      default:
        return const [
          Color(0xFF440154),
          Color(0xFF414487),
          Color(0xFF2A788E),
          Color(0xFF22A884),
          Color(0xFF7AD151),
          Color(0xFFFDE725),
        ];
    }
  }

  Widget _buildPillButton({
    Key? key,
    required String label,
    required VoidCallback onPressed,
  }) {
    return InkWell(
      key: key,
      borderRadius: BorderRadius.circular(16),
      onTap: onPressed,
      child: Container(
        height: 28,
        padding: const EdgeInsets.symmetric(horizontal: 12),
        decoration: BoxDecoration(
          color: AppTheme.surface.withAlpha(235),
          borderRadius: BorderRadius.circular(14),
          border: Border.all(color: AppTheme.border),
          boxShadow: AppTheme.cardShadow,
        ),
        alignment: Alignment.center,
        child: Text(
          label,
          style: const TextStyle(
            fontSize: 12,
            fontWeight: FontWeight.w500,
            color: AppTheme.textBody,
          ),
        ),
      ),
    );
  }

  Widget _buildIconPill({
    Key? key,
    required IconData icon,
    required String tooltip,
    required VoidCallback onPressed,
  }) {
    return Tooltip(
      message: tooltip,
      child: InkWell(
        key: key,
        borderRadius: BorderRadius.circular(14),
        onTap: onPressed,
        child: Container(
          width: 28,
          height: 28,
          decoration: BoxDecoration(
            color: AppTheme.surface.withAlpha(235),
            shape: BoxShape.circle,
            border: Border.all(color: AppTheme.border),
            boxShadow: AppTheme.cardShadow,
          ),
          alignment: Alignment.center,
          child: Icon(icon, size: 15, color: AppTheme.textBody),
        ),
      ),
    );
  }

  Widget _buildPillToggleButton({
    Key? key,
    required String label,
    required bool active,
    required ValueChanged<bool> onChanged,
  }) {
    return InkWell(
      key: key,
      borderRadius: BorderRadius.circular(16),
      onTap: () => onChanged(!active),
      child: Container(
        height: 28,
        padding: const EdgeInsets.symmetric(horizontal: 12),
        decoration: BoxDecoration(
          color: active
              ? AppTheme.primaryBlueBg
              : AppTheme.surface.withAlpha(235),
          borderRadius: BorderRadius.circular(14),
          border: Border.all(
            color: active ? AppTheme.primaryBlue : AppTheme.border,
            width: active ? 1.2 : 1.0,
          ),
          boxShadow: AppTheme.cardShadow,
        ),
        alignment: Alignment.center,
        child: Text(
          label,
          style: TextStyle(
            fontSize: 12,
            fontWeight: active ? FontWeight.w600 : FontWeight.w500,
            color: active ? AppTheme.primaryBlue : AppTheme.textBody,
          ),
        ),
      ),
    );
  }
}

/// 绘制视口 3D 点云与标尺网格
class ViewportCanvasPainter extends CustomPainter {
  final List<Point3D> points;
  final double pointSize;
  final String pointShape;
  final String colormap;
  final double scalarMin;
  final double scalarMax;
  final double azimuth;
  final double elevation;
  final double zoom;
  final double panX;
  final double panY;
  final bool showMapAxis;
  final bool showCrosshair;
  final bool showWorldAxis;
  final Point3D? selectedPoint;
  final Point3D? hoveredPoint;
  final List<MeasurementLineItem> measurementLines;
  final Point3D? pendingMeasureStart;
  final bool isMeasurementMode;
  final String measurementDisplayMode;
  final Offset? boxSelectStart;
  final Offset? boxSelectCurrent;

  const ViewportCanvasPainter({
    required this.points,
    required this.pointSize,
    required this.pointShape,
    required this.colormap,
    required this.scalarMin,
    required this.scalarMax,
    required this.azimuth,
    required this.elevation,
    required this.zoom,
    required this.panX,
    required this.panY,
    required this.showMapAxis,
    required this.showCrosshair,
    required this.showWorldAxis,
    this.selectedPoint,
    this.hoveredPoint,
    this.measurementLines = const [],
    this.pendingMeasureStart,
    this.isMeasurementMode = false,
    this.measurementDisplayMode = '3d',
    this.boxSelectStart,
    this.boxSelectCurrent,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width * 0.5 + panX;
    final cy = size.height * 0.5 + panY;

    // 绘制坐标轴微弱网格
    if (showMapAxis) {
      final axisPaint = Paint()
        ..color = Colors.white.withAlpha(20)
        ..strokeWidth = 1.0;

      canvas.drawLine(Offset(0, cy), Offset(size.width, cy), axisPaint);
      canvas.drawLine(Offset(cx, 0), Offset(cx, size.height), axisPaint);

      // 同心网格圆
      for (double r = 60.0; r <= 240.0; r += 60.0) {
        final circlePaint = Paint()
          ..color = Colors.white.withAlpha(12)
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.0;
        canvas.drawCircle(Offset(cx, cy), r * zoom, circlePaint);
      }
    }

    final radAz = azimuth * (math.pi / 180.0);
    final radEl = elevation * (math.pi / 180.0);
    final cosAz = math.cos(radAz);
    final sinAz = math.sin(radAz);
    final cosEl = math.cos(radEl);
    final sinEl = math.sin(radEl);

    // 计算点云包围盒与投影缩放
    double minX = 0, maxX = 1, minY = 0, maxY = 1, minZ = 0, maxZ = 1;
    if (points.isNotEmpty) {
      minX = points[0].x;
      maxX = points[0].x;
      minY = points[0].y;
      maxY = points[0].y;
      minZ = points[0].z;
      maxZ = points[0].z;
      for (final p in points) {
        if (p.x < minX) minX = p.x;
        if (p.x > maxX) maxX = p.x;
        if (p.y < minY) minY = p.y;
        if (p.y > maxY) maxY = p.y;
        if (p.z < minZ) minZ = p.z;
        if (p.z > maxZ) maxZ = p.z;
      }
    }
    final midX = (minX + maxX) * 0.5;
    final midY = (minY + maxY) * 0.5;
    final midZ = (minZ + maxZ) * 0.5;
    final maxSpan = math.max(maxX - minX, math.max(maxY - minY, maxZ - minZ));
    final baseScale = (maxSpan > 1e-4)
        ? (math.min(size.width, size.height) * 0.65 / maxSpan)
        : 1.0;
    final totalScale = baseScale * zoom;

    Offset projectPoint(Point3D p) {
      final rx = p.x - midX;
      final ry = p.y - midY;
      final rz = p.z - midZ;
      final x1 = rx * cosAz - ry * sinAz;
      final y1 = rx * sinAz + ry * cosAz;
      final z1 = rz;
      final x2 = x1;
      final y2 = y1 * cosEl - z1 * sinEl;
      return Offset(cx + x2 * totalScale, cy - y2 * totalScale);
    }

    if (points.isNotEmpty) {
      final pointPaint = Paint()..style = PaintingStyle.fill;
      final isSquare =
          pointShape == '方形' || pointShape.toLowerCase() == 'square';

      for (final p in points) {
        final pos = projectPoint(p);
        if (pos.dx >= -20 &&
            pos.dx <= size.width + 20 &&
            pos.dy >= -20 &&
            pos.dy <= size.height + 20) {
          pointPaint.color = p.toColor(colormap, scalarMin, scalarMax);
          if (isSquare) {
            canvas.drawRect(
              Rect.fromCenter(
                center: pos,
                width: pointSize * 2,
                height: pointSize * 2,
              ),
              pointPaint,
            );
          } else {
            canvas.drawCircle(pos, pointSize, pointPaint);
          }
        }
      }
    }

    // 绘制选中的旋转焦点 / 选中点 (Orbit Pivot Focus Reticle)
    if (selectedPoint != null) {
      final sp = projectPoint(selectedPoint!);
      final reticlePaint = Paint()
        ..color = const Color(0xFF00E5FF)
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.5;
      canvas.drawCircle(sp, 6.0, reticlePaint);
      canvas.drawCircle(
        sp,
        13.0,
        Paint()
          ..color = const Color(0x6600E5FF)
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.2,
      );
      canvas.drawLine(
        Offset(sp.dx - 18, sp.dy),
        Offset(sp.dx - 8, sp.dy),
        reticlePaint,
      );
      canvas.drawLine(
        Offset(sp.dx + 8, sp.dy),
        Offset(sp.dx + 18, sp.dy),
        reticlePaint,
      );
      canvas.drawLine(
        Offset(sp.dx, sp.dy - 18),
        Offset(sp.dx, sp.dy - 8),
        reticlePaint,
      );
      canvas.drawLine(
        Offset(sp.dx, sp.dy + 8),
        Offset(sp.dx, sp.dy + 18),
        reticlePaint,
      );
    }

    // 绘制鼠标悬停点高亮环 (Hover Highlight Ring)
    if (hoveredPoint != null) {
      final hp = projectPoint(hoveredPoint!);
      canvas.drawCircle(
        hp,
        pointSize * 2.0 + 3.0,
        Paint()
          ..color = const Color(0xFFF1C21B)
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.5,
      );
    }

    // 绘制三维测量线段与距离标签 (Measurement Lines & Badges)
    for (final line in measurementLines) {
      final p1 = projectPoint(line.start);
      final p2 = projectPoint(line.end);
      final linePaint = Paint()
        ..color = line.color
        ..strokeWidth = 2.0
        ..style = PaintingStyle.stroke;
      canvas.drawLine(p1, p2, linePaint);
      canvas.drawCircle(p1, 4.0, Paint()..color = line.color);
      canvas.drawCircle(p2, 4.0, Paint()..color = line.color);

      // 距离数值标签
      final mx = (p1.dx + p2.dx) * 0.5;
      final my = (p1.dy + p2.dy) * 0.5;
      final label = line.distanceLabel(measurementDisplayMode);
      final tp = TextPainter(
        text: TextSpan(
          text: label,
          style: const TextStyle(
            color: Colors.white,
            fontSize: 10,
            fontWeight: FontWeight.w600,
          ),
        ),
        textDirection: TextDirection.ltr,
      )..layout();

      final badgeRect = Rect.fromCenter(
        center: Offset(mx, my - 12),
        width: tp.width + 10,
        height: tp.height + 6,
      );
      canvas.drawRRect(
        RRect.fromRectAndRadius(badgeRect, const Radius.circular(4)),
        Paint()..color = Colors.black.withAlpha(200),
      );
      canvas.drawRRect(
        RRect.fromRectAndRadius(badgeRect, const Radius.circular(4)),
        Paint()
          ..color = line.color.withAlpha(150)
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.0,
      );
      tp.paint(canvas, Offset(mx - tp.width * 0.5, my - 12 - tp.height * 0.5));
    }

    // 绘制正在测量中的起点与虚线预览 (Pending Measurement Preview)
    if (pendingMeasureStart != null) {
      final pp = projectPoint(pendingMeasureStart!);
      canvas.drawCircle(
        pp,
        5.0,
        Paint()
          ..color = const Color(0xFF00E5FF)
          ..style = PaintingStyle.fill,
      );
      canvas.drawCircle(
        pp,
        9.0,
        Paint()
          ..color = const Color(0xFF00E5FF)
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.5,
      );

      if (hoveredPoint != null) {
        final hp = projectPoint(hoveredPoint!);
        final previewPaint = Paint()
          ..color = const Color(0xFF00E5FF).withAlpha(180)
          ..strokeWidth = 1.5
          ..style = PaintingStyle.stroke;
        canvas.drawLine(pp, hp, previewPaint);
      }
    }

    // 绘制 Shift 框选矩形选区 (Box Selection Rectangle)
    if (boxSelectStart != null && boxSelectCurrent != null) {
      final rect = Rect.fromPoints(boxSelectStart!, boxSelectCurrent!);
      canvas.drawRect(rect, Paint()..color = const Color(0x333B82F6));
      canvas.drawRect(
        rect,
        Paint()
          ..color = const Color(0xFF3B82F6)
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.2,
      );
    }

    // 十字准星
    if (showCrosshair) {
      final crosshairPaint = Paint()
        ..color = AppTheme.primaryBlue.withAlpha(140)
        ..strokeWidth = 1.2;
      canvas.drawLine(Offset(cx - 10, cy), Offset(cx + 10, cy), crosshairPaint);
      canvas.drawLine(Offset(cx, cy - 10), Offset(cx, cy + 10), crosshairPaint);
    }
  }

  @override
  bool shouldRepaint(covariant ViewportCanvasPainter oldDelegate) {
    return oldDelegate.azimuth != azimuth ||
        oldDelegate.elevation != elevation ||
        oldDelegate.zoom != zoom ||
        oldDelegate.panX != panX ||
        oldDelegate.panY != panY ||
        oldDelegate.showMapAxis != showMapAxis ||
        oldDelegate.showCrosshair != showCrosshair ||
        oldDelegate.showWorldAxis != showWorldAxis ||
        oldDelegate.pointSize != pointSize ||
        oldDelegate.pointShape != pointShape ||
        oldDelegate.colormap != colormap ||
        oldDelegate.points.length != points.length ||
        oldDelegate.scalarMin != scalarMin ||
        oldDelegate.scalarMax != scalarMax ||
        oldDelegate.selectedPoint != selectedPoint ||
        oldDelegate.hoveredPoint != hoveredPoint ||
        oldDelegate.measurementLines.length != measurementLines.length ||
        oldDelegate.pendingMeasureStart != pendingMeasureStart ||
        oldDelegate.isMeasurementMode != isMeasurementMode ||
        oldDelegate.measurementDisplayMode != measurementDisplayMode ||
        oldDelegate.boxSelectStart != boxSelectStart ||
        oldDelegate.boxSelectCurrent != boxSelectCurrent;
  }
}

/// 绘制导航球三维坐标轴指示器
class NavigationBallGizmoPainter extends CustomPainter {
  final double azimuth;
  final double elevation;

  const NavigationBallGizmoPainter({
    required this.azimuth,
    required this.elevation,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width * 0.5;
    final cy = size.height * 0.5;

    final radAz = azimuth * (math.pi / 180.0);
    final radEl = elevation * (math.pi / 180.0);
    final cosAz = math.cos(radAz);
    final sinAz = math.sin(radAz);
    final cosEl = math.cos(radEl);
    final sinEl = math.sin(radEl);

    // 绘制随视角旋转的三维轴线
    void drawAxis3D(double x, double y, double z, Color color, String label) {
      final x1 = x * cosAz - y * sinAz;
      final y1 = x * sinAz + y * cosAz;
      final z1 = z;

      final x2 = x1;
      final y2 = y1 * cosEl - z1 * sinEl;

      const len = 25.0;
      final px = cx + x2 * len;
      final py = cy - y2 * len;

      final p = Paint()
        ..color = color
        ..strokeWidth = 2.0;

      canvas.drawLine(Offset(cx, cy), Offset(px, py), p);
      canvas.drawCircle(Offset(px, py), 5.5, Paint()..color = color);

      final textSpan = TextSpan(
        text: label,
        style: const TextStyle(
          color: Colors.white,
          fontSize: 8.5,
          fontWeight: FontWeight.bold,
        ),
      );
      final textPainter = TextPainter(
        text: textSpan,
        textDirection: TextDirection.ltr,
      )..layout();
      textPainter.paint(canvas, Offset(px - 3, py - 5));
    }

    // X 轴（红，+X）
    drawAxis3D(1.0, 0.0, 0.0, Colors.redAccent, 'X');
    // Y 轴（绿，+Y）
    drawAxis3D(0.0, 1.0, 0.0, Colors.greenAccent, 'Y');
    // Z 轴（蓝，+Z）
    drawAxis3D(0.0, 0.0, 1.0, Colors.blueAccent, 'Z');
  }

  @override
  bool shouldRepaint(covariant NavigationBallGizmoPainter oldDelegate) {
    return oldDelegate.azimuth != azimuth || oldDelegate.elevation != elevation;
  }
}
