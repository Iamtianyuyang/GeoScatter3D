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
          // Logo & 返回欢迎页
          InkWell(
            borderRadius: BorderRadius.circular(6),
            onTap: () => service.closeWorkbench(),
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
              child: Row(
                children: [
                  const Icon(Icons.home_outlined, size: 17, color: AppTheme.primaryBlue),
                  const SizedBox(width: 6),
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
                ],
              ),
            ),
          ),
          const SizedBox(width: 14),

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
                service.summary.name.isEmpty ? '未载入工程' : service.summary.name,
                style: const TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w500,
                  color: AppTheme.textDim,
                ),
              );
            },
          ),

          const Spacer(),

          // 返回欢迎页按钮
          TextButton.icon(
            style: TextButton.styleFrom(
              foregroundColor: AppTheme.textDim,
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
            ),
            icon: const Icon(Icons.arrow_back_rounded, size: 15),
            label: const Text('欢迎页', style: TextStyle(fontSize: 12.5)),
            onPressed: () => service.closeWorkbench(),
          ),
          const SizedBox(width: 6),

          // 布局切换选择器
          PopupMenuButton<WorkbenchLayoutMode>(
            tooltip: '切换工作区布局模式',
            initialValue: service.layoutMode,
            onSelected: (mode) => service.setLayoutMode(mode),
            itemBuilder: (context) => [
              const PopupMenuItem(
                value: WorkbenchLayoutMode.standard,
                child: Row(
                  children: [
                    Icon(Icons.dashboard_customize_outlined, size: 16, color: AppTheme.primaryBlue),
                    SizedBox(width: 8),
                    Text('标准工作台 (Workbench)'),
                  ],
                ),
              ),
              const PopupMenuItem(
                value: WorkbenchLayoutMode.floatingDock,
                child: Row(
                  children: [
                    Icon(Icons.layers_outlined, size: 16, color: AppTheme.accentBlue),
                    SizedBox(width: 8),
                    Text('悬浮胶囊 Dock (Floating Dock)'),
                  ],
                ),
              ),
              const PopupMenuItem(
                value: WorkbenchLayoutMode.analysisRail,
                child: Row(
                  children: [
                    Icon(Icons.view_sidebar_outlined, size: 16, color: AppTheme.statusGreen),
                    SizedBox(width: 8),
                    Text('暗色分析舱 (Analysis Rail)'),
                  ],
                ),
              ),
            ],
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
              decoration: BoxDecoration(
                border: Border.all(color: AppTheme.border),
                borderRadius: BorderRadius.circular(6),
                color: Colors.white,
              ),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const Icon(Icons.view_quilt_outlined, size: 14, color: AppTheme.primaryBlue),
                  const SizedBox(width: 5),
                  Text(
                    service.layoutMode.displayName,
                    style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w600, color: AppTheme.textTitle),
                  ),
                  const SizedBox(width: 4),
                  const Icon(Icons.arrow_drop_down_rounded, size: 16, color: AppTheme.textDim),
                ],
              ),
            ),
          ),
          const SizedBox(width: 10),

          // 快速打开数据按钮
          TextButton.icon(
            style: TextButton.styleFrom(
              foregroundColor: AppTheme.primaryBlue,
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
            ),
            icon: const Icon(Icons.folder_open_rounded, size: 16),
            label: const Text('载入示例', style: TextStyle(fontSize: 12.5, fontWeight: FontWeight.w600)),
            onPressed: () {
              service.loadDataset('data/sample-points.gs3d.bundle');
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
