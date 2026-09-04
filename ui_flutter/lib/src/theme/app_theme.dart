import 'package:flutter/material.dart';

class AppTheme {
  static const Color background = Color(0xFFF8FAFC); // Slate-50
  static const Color surface = Color(0xFFFFFFFF);
  static const Color surfaceMuted = Color(0xFFF1F5F9); // Slate-100
  static const Color border = Color(0xFFE2E8F0); // Slate-200
  static const Color borderSubtle = Color(0xFFCBD5E1); // Slate-300

  static const Color textTitle = Color(0xFF0F172A); // Slate-900
  static const Color textBody = Color(0xFF334155); // Slate-700
  static const Color textDim = Color(0xFF64748B); // Slate-500

  static const Color primaryBlue = Color(0xFF2563EB); // Blue-600
  static const Color primaryBlueLight = Color(0xFF3B82F6); // Blue-500
  static const Color primaryBlueHover = Color(0xFF1D4ED8); // Blue-700
  static const Color primaryBlueBg = Color(0xFFEFF6FF); // Blue-50
  static const Color accentBlue = Color(0xFF3B82F6); // Accent blue
  static const Color statusGreen = Color(0xFF10B981); // Emerald-500

  static const double cardRadius = 8.0;
  static const double controlRadius = 6.0;

  static const List<BoxShadow> cardShadow = [
    BoxShadow(
      color: Color(0x06000000),
      blurRadius: 4,
      offset: Offset(0, 1),
    ),
    BoxShadow(
      color: Color(0x04000000),
      blurRadius: 1,
      offset: Offset(0, 0),
    ),
  ];

  static ThemeData lightTheme() {
    return ThemeData(
      useMaterial3: true,
      scaffoldBackgroundColor: background,
      colorScheme: ColorScheme.fromSeed(
        seedColor: primaryBlue,
        surface: surface,
        surfaceContainerLow: surfaceMuted,
      ),
      fontFamily: 'NotoSansCJKsc',
      fontFamilyFallback: const [
        'Segoe UI',
        'Microsoft YaHei',
        'PingFang SC',
        'sans-serif',
      ],
    );
  }
}
