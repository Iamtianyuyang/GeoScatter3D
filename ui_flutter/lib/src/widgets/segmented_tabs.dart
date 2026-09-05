import 'package:flutter/material.dart';
import '../theme/app_theme.dart';

class ModernSegmentedTabs extends StatelessWidget {
  final List<String> tabs;
  final int selectedIndex;
  final ValueChanged<int> onTabSelected;
  final List<Key?>? tabKeys;

  const ModernSegmentedTabs({
    super.key,
    required this.tabs,
    required this.selectedIndex,
    required this.onTabSelected,
    this.tabKeys,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 36,
      padding: const EdgeInsets.all(2.5),
      decoration: BoxDecoration(
        color: AppTheme.surfaceMuted,
        borderRadius: BorderRadius.circular(AppTheme.cardRadius),
        border: Border.all(color: AppTheme.border, width: 1),
      ),
      child: Row(
        children: List.generate(tabs.length, (index) {
          final isSelected = (index == selectedIndex);
          final isLast = (index == tabs.length - 1);
          final isNextSelected = (index + 1 == selectedIndex);
          final key = tabKeys != null && index < tabKeys!.length ? tabKeys![index] : null;

          return Expanded(
            child: Row(
              children: [
                Expanded(
                  child: GestureDetector(
                    key: key,
                    onTap: () => onTabSelected(index),
                    child: AnimatedContainer(
                      duration: const Duration(milliseconds: 150),
                      curve: Curves.easeOut,
                      alignment: Alignment.center,
                      decoration: BoxDecoration(
                        color: isSelected ? AppTheme.surface : Colors.transparent,
                        borderRadius: BorderRadius.circular(AppTheme.controlRadius),
                        border: isSelected
                            ? Border.all(color: AppTheme.primaryBlue, width: 1.2)
                            : null,
                        boxShadow: isSelected ? AppTheme.cardShadow : null,
                      ),
                      child: Text(
                        tabs[index],
                        style: TextStyle(
                          fontSize: 13,
                          fontWeight: isSelected ? FontWeight.w600 : FontWeight.w500,
                          color: isSelected ? AppTheme.primaryBlue : AppTheme.textDim,
                        ),
                      ),
                    ),
                  ),
                ),
                if (!isLast && !isSelected && !isNextSelected)
                  Container(
                    width: 1,
                    height: 16,
                    color: AppTheme.border,
                  ),
              ],
            ),
          );
        }),
      ),
    );
  }
}
