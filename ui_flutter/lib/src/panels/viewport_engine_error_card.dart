import 'package:flutter/material.dart';

import '../theme/gs3d_tokens.dart';

/// R3 显式降级卡片：引擎在当前平台不可用时，视口区渲染本卡片。
///
/// 硬性约束（架构红线 R3）：
///   * 不渲染任何 Dart 绘制的点云替代物（禁止假视口）；
///   * 文案必须包含「渲染内核在本平台不可用」并提供引导（回桌面版）；
///   * 入口不隐藏——依赖引擎的动作保持可达，执行时显式报错。
class ViewportEngineErrorCard extends StatelessWidget {
  const ViewportEngineErrorCard({
    super.key,
    this.reason,
    this.compact = false,
  });

  /// 引擎不可用原因（来自 `service.initializationError`）。
  final String? reason;

  /// 紧凑模式（抽屉 / 小空间下缩小字号与内边距）。
  final bool compact;

  @override
  Widget build(BuildContext context) {
    final titleStyle = TextStyle(
      // 显式指定设计字体族（本卡片可能出现在无 DefaultTextStyle 继承的场景）。
      fontFamily: Gs3dType.fontFamily,
      fontSize: compact ? 14 : 16,
      fontWeight: FontWeight.w700,
      color: Gs3dTokens.textPrimary,
      height: 1.35,
    );
    final bodyStyle = TextStyle(
      fontFamily: Gs3dType.fontFamily,
      fontSize: compact ? 11.5 : 12.5,
      color: Gs3dTokens.textSecondary,
      height: 1.55,
    );

    return Container(
      key: const ValueKey('workbench.viewport.engine_error_card'),
      margin: const EdgeInsets.all(Gs3dTokens.space24),
      padding: EdgeInsets.all(compact ? Gs3dTokens.space16 : Gs3dTokens.space24),
      constraints: const BoxConstraints(maxWidth: 420),
      decoration: BoxDecoration(
        color: Gs3dTokens.surface.withAlpha(245),
        borderRadius: BorderRadius.circular(Gs3dTokens.radiusLg),
        border: Border.all(color: Gs3dTokens.dangerBorder),
        boxShadow: Gs3dTokens.elevation1,
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Icon(
                Icons.report_gmailerrorred_rounded,
                size: 22,
                color: Gs3dTokens.danger,
              ),
              const SizedBox(width: Gs3dTokens.space8),
              Flexible(
                child: Text('渲染内核在本平台不可用', style: titleStyle),
              ),
            ],
          ),
          const SizedBox(height: Gs3dTokens.space8),
          Text(
            '三维视口由 gs3d_engine 渲染内核驱动；当前平台未部署该内核，'
            '因此无法显示点云与执行视口渲染类动作。',
            style: bodyStyle,
          ),
          if (reason != null && reason!.isNotEmpty) ...[
            const SizedBox(height: Gs3dTokens.space8),
            Text(
              '原因: $reason',
              style: bodyStyle.copyWith(
                fontSize: compact ? 10.5 : 11.5,
                color: Gs3dTokens.textTertiary,
                fontFamily: Gs3dType.mono.fontFamily,
              ),
            ),
          ],
          const SizedBox(height: Gs3dTokens.space12),
          Text(
            '引导：请在桌面版（Windows / Vulkan）打开本工程以获得完整渲染体验；'
            '其余界面与数据管理功能仍可正常使用。',
            style: bodyStyle,
          ),
        ],
      ),
    );
  }
}
