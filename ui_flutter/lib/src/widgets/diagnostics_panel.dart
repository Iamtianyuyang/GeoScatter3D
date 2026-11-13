import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/app_tokens.dart';
import '../ui/ui_kit.dart';

/// 诊断面板：GPU 选择、缓存、引擎自述。
///
/// 面向「出问题时想知道到底发生了什么」的人：所有值来自引擎快照与诊断
/// JSON，不做二次加工；切换 GPU 需要重启，所以直接写在按钮下面。
class DiagnosticsPanel extends StatefulWidget {
  const DiagnosticsPanel({super.key, required this.service});

  final GeoScatter3dService service;

  @override
  State<DiagnosticsPanel> createState() => _DiagnosticsPanelState();
}

class _DiagnosticsPanelState extends State<DiagnosticsPanel> {
  @override
  void initState() {
    super.initState();
    // 首帧后再拉，避免在 build 期间触发通知。
    WidgetsBinding.instance.addPostFrameCallback((_) {
      widget.service.refreshGpus();
      widget.service.refreshDiagnostics();
    });
  }

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return ListenableBuilder(
      listenable: widget.service,
      builder: (context, _) {
        final service = widget.service;
        final gpus = service.gpus;
        final diagnostics = service.diagnosticsJson;
        return ListView(
          padding: const EdgeInsets.only(bottom: 24),
          children: [
            PanelSection(
              title: '引擎',
              child: Column(
                children: [
                  KeyValueRow(label: '渲染通路', value: service.renderPath),
                  KeyValueRow(
                    label: 'ABI',
                    numeric: true,
                    value: service.abiVersion?.toString() ?? '—',
                  ),
                  KeyValueRow(
                    label: 'GPU',
                    value: service.gpuName.isEmpty ? '—' : service.gpuName,
                    maxLines: 3,
                  ),
                  KeyValueRow(
                    label: 'LOD',
                    value: service.lodSummary.isEmpty
                        ? '—'
                        : service.lodSummary,
                    maxLines: 3,
                  ),
                ],
              ),
            ),
            const Hairline(),
            PanelSection(
              title: '显卡',
              trailing: IconButton(
                tooltip: '重新枚举',
                onPressed: service.refreshGpus,
                visualDensity: VisualDensity.compact,
                iconSize: 15,
                color: tokens.colors.textLow,
                icon: const Icon(Icons.refresh),
              ),
              child: gpus.isEmpty
                  ? const EmptyHint(
                      text: '没有读到可用显卡',
                      hint: '引擎启动失败或本机无 Vulkan 设备时会为空。',
                      icon: Icons.videocam_off_outlined,
                    )
                  : Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        for (final gpu in gpus)
                          _GpuTile(
                            name: gpu.name,
                            type: gpu.type,
                            active: gpu.active,
                            preferred: gpu.preferred,
                            onTap: () => service.selectGpu(
                              gpu.preferred ? -1 : gpu.index,
                            ),
                          ),
                        const SizedBox(height: 8),
                        Text(
                          service.preferredGpuIndex == null
                              ? '当前：自动选择（优先独显）。点一下可固定到某块卡，重启后生效。'
                              : '已固定到所选显卡，重启应用后生效。再点一次可恢复自动。',
                          style: tokens.text.meta.copyWith(
                            color: tokens.colors.textLow,
                            fontSize: 10.5,
                          ),
                        ),
                      ],
                    ),
            ),
            const Hairline(),
            PanelSection(
              title: '缓存',
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Text(
                    '瓦片按需上传；外部替换工程包后清一次缓存可以避免读到旧块。',
                    style: tokens.text.meta.copyWith(
                      color: tokens.colors.textMid,
                    ),
                  ),
                  const SizedBox(height: 10),
                  AppButton(
                    label: '清除瓦片缓存',
                    icon: Icons.delete_sweep_outlined,
                    onPressed: service.ready ? service.clearTileCache : null,
                  ),
                ],
              ),
            ),
            const Hairline(),
            PanelSection(
              title: '诊断快照',
              trailing: IconButton(
                tooltip: '刷新诊断',
                onPressed: service.refreshDiagnostics,
                visualDensity: VisualDensity.compact,
                iconSize: 15,
                color: tokens.colors.textLow,
                icon: const Icon(Icons.sync),
              ),
              child: Container(
                constraints: const BoxConstraints(maxHeight: 260),
                padding: const EdgeInsets.all(10),
                decoration: BoxDecoration(
                  color: tokens.colors.raised,
                  border: Border.all(color: tokens.colors.line),
                  borderRadius: tokens.radii.smAll,
                ),
                child: SingleChildScrollView(
                  child: SelectableText(
                    diagnostics.isEmpty ? '暂无诊断数据' : diagnostics,
                    key: const ValueKey('diagnostics_json'),
                    style: tokens.text.numeric.copyWith(
                      fontSize: 10.5,
                      height: 1.45,
                      color: tokens.colors.textMid,
                    ),
                  ),
                ),
              ),
            ),
            if (service.error != null)
              PanelSection(
                child: NoticeBanner(
                  text: service.error!,
                  tone: ButtonTone.danger,
                  icon: Icons.error_outline,
                ),
              ),
          ],
        );
      },
    );
  }
}

class _GpuTile extends StatelessWidget {
  const _GpuTile({
    required this.name,
    required this.type,
    required this.active,
    required this.preferred,
    required this.onTap,
  });

  final String name;
  final String type;
  final bool active;
  final bool preferred;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return InkWell(
      onTap: onTap,
      borderRadius: tokens.radii.smAll,
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 7, horizontal: 2),
        child: Row(
          children: [
            Icon(
              preferred ? Icons.check_circle : Icons.circle_outlined,
              size: 15,
              color: preferred ? tokens.colors.signal : tokens.colors.textLow,
            ),
            const SizedBox(width: 8),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(
                    name,
                    maxLines: 2,
                    overflow: TextOverflow.ellipsis,
                    style: tokens.text.body.copyWith(
                      fontSize: 12,
                      color: tokens.colors.textHigh,
                    ),
                  ),
                  Text(
                    '${type.isEmpty ? 'GPU' : type}'
                    '${active ? ' · 当前使用' : ''}',
                    style: tokens.text.meta.copyWith(
                      fontSize: 10.5,
                      color: active
                          ? tokens.colors.signal
                          : tokens.colors.textLow,
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
