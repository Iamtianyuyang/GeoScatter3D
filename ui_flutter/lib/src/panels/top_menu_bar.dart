import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_theme.dart';

class TopMenuBar extends StatelessWidget {
  final GeoScatter3dService service;

  const TopMenuBar({
    super.key,
    required this.service,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 38,
      padding: const EdgeInsets.symmetric(horizontal: 14),
      decoration: const BoxDecoration(
        color: AppTheme.surface,
        border: Border(
          bottom: BorderSide(color: AppTheme.border, width: 1),
        ),
      ),
      child: Row(
        children: [
          // Logo
          RichText(
            text: const TextSpan(
              children: [
                TextSpan(
                  text: 'GeoScatter ',
                  style: TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w800,
                    color: AppTheme.textTitle,
                  ),
                ),
                TextSpan(
                  text: '3D',
                  style: TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w800,
                    color: AppTheme.primaryBlue,
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(width: 20),

          // 菜单项
          _buildMenuItem('文件'),
          _buildMenuItem('视图'),
          _buildMenuItem('窗口'),
          _buildMenuItem('帮助'),

          const Spacer(),

          // 居中数据集名称
          ListenableBuilder(
            listenable: service,
            builder: (context, _) {
              return Text(
                service.summary.name,
                style: const TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w500,
                  color: AppTheme.textDim,
                ),
              );
            },
          ),

          const Spacer(),

          // 快速打开数据按钮
          TextButton.icon(
            style: TextButton.styleFrom(
              foregroundColor: AppTheme.primaryBlue,
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
            ),
            icon: const Icon(Icons.folder_open_rounded, size: 16),
            label: const Text('载入示例', style: TextStyle(fontSize: 12.5, fontWeight: FontWeight.w600)),
            onPressed: () {
              service.loadDataset('../data/sample-points.gs3d.bundle');
            },
          ),
        ],
      ),
    );
  }

  Widget _buildMenuItem(String title) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 10),
      child: InkWell(
        borderRadius: BorderRadius.circular(4),
        onTap: () {},
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
          child: Text(
            title,
            style: const TextStyle(
              fontSize: 13,
              fontWeight: FontWeight.w500,
              color: AppTheme.textTitle,
            ),
          ),
        ),
      ),
    );
  }
}
