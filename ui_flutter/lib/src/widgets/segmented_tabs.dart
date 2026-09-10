import 'package:flutter/material.dart';
import '../theme/gs3d_tokens.dart';

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
        color: Gs3dTokens.surfaceSunken,
        borderRadius: BorderRadius.circular(Gs3dTokens.radiusLg),
        border: Border.all(color: Gs3dTokens.border, width: 1),
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
                        color: isSelected ? Gs3dTokens.surface : Colors.transparent,
                        borderRadius: BorderRadius.circular(Gs3dTokens.radiusSm),
                        border: isSelected
                            ? Border.all(color: Gs3dTokens.accent, width: 1.2)
                            : null,
                        boxShadow: isSelected ? Gs3dTokens.elevation1 : null,
                      ),
                      child: Text(
                        tabs[index],
                        style: TextStyle(
                          fontSize: 13,
                          fontWeight: isSelected ? FontWeight.w600 : FontWeight.w500,
                          color: isSelected ? Gs3dTokens.accent : Gs3dTokens.textSecondary,
                        ),
                      ),
                    ),
                  ),
                ),
                if (!isLast && !isSelected && !isNextSelected)
                  Container(
                    width: 1,
                    height: 16,
                    color: Gs3dTokens.border,
                  ),
              ],
            ),
          );
        }),
      ),
    );
  }
}
