import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_theme.dart';

class BottomStatusBar extends StatelessWidget {
  final GeoScatter3dService service;

  const BottomStatusBar({
    super.key,
    required this.service,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 28,
      padding: const EdgeInsets.symmetric(horizontal: 14),
      decoration: const BoxDecoration(
        color: AppTheme.surface,
        border: Border(
          top: BorderSide(color: AppTheme.border, width: 1),
        ),
      ),
      child: ListenableBuilder(
        listenable: service,
        builder: (context, _) {
          final count = service.summary.pointCount;
          return SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: Row(
              children: [
                const Icon(Icons.speed_rounded, size: 14, color: AppTheme.textDim),
                const SizedBox(width: 4),
                const Text('1892.3 FPS 0.52 ms', style: TextStyle(fontSize: 11.5, color: AppTheme.textDim, fontFamily: 'Consolas')),
                const SizedBox(width: 16),

                const Icon(Icons.grain_rounded, size: 14, color: AppTheme.textDim),
                const SizedBox(width: 4),
                Text('$count 可见点', style: const TextStyle(fontSize: 11.5, color: AppTheme.textDim)),
                const SizedBox(width: 16),

                const Icon(Icons.memory_rounded, size: 14, color: AppTheme.textDim),
                const SizedBox(width: 4),
                const Text('GPU 0.0 MB (1/0)', style: TextStyle(fontSize: 11.5, color: AppTheme.textDim)),
                const SizedBox(width: 16),

                const Icon(Icons.check_circle_rounded, size: 14, color: AppTheme.primaryBlue),
                const SizedBox(width: 4),
                const Text('就绪', style: TextStyle(fontSize: 11.5, color: AppTheme.textDim)),
                const SizedBox(width: 24),

                const Text('0.0, 0.0, 143.4', style: TextStyle(fontSize: 11.5, color: AppTheme.textDim, fontFamily: 'Consolas')),
                const SizedBox(width: 12),
                const Text('本地坐标 / 未知', style: TextStyle(fontSize: 11.5, color: AppTheme.textDim)),
              ],
            ),
          );
        },
      ),
    );
  }
}
