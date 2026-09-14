import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'src/layouts/main_shell.dart';
import 'src/theme/app_tokens.dart';

const _tokens = AppTokens.dark;

void main() {
  WidgetsFlutterBinding.ensureInitialized();

  // 全屏点云画布延伸到系统栏，使用浅色图标保持可读。
  SystemChrome.setSystemUIOverlayStyle(
    SystemUiOverlayStyle(
      statusBarColor: _tokens.colors.viewport,
      statusBarIconBrightness: Brightness.light,
      systemNavigationBarColor: _tokens.colors.viewport,
      systemNavigationBarIconBrightness: Brightness.light,
    ),
  );

  runApp(const GeoScatter3dApp());
}

class GeoScatter3dApp extends StatelessWidget {
  const GeoScatter3dApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'GeoScatter3D',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        brightness: Brightness.dark,
        scaffoldBackgroundColor: _tokens.colors.viewport,
        colorScheme: ColorScheme.dark(
          primary: _tokens.colors.accent,
          surface: _tokens.colors.hudSurfaceRaised,
          error: _tokens.colors.error,
        ),
        extensions: const [_tokens],
      ),
      home: const MainShell(),
    );
  }
}
