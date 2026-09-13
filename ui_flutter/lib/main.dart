import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'src/layouts/main_shell.dart';
import 'src/theme/app_tokens.dart';

const _tokens = AppTokens.dark;

void main() {
  WidgetsFlutterBinding.ensureInitialized();

  // 浅色主界面需要深色系统图标保持可读。
  SystemChrome.setSystemUIOverlayStyle(
    SystemUiOverlayStyle(
      statusBarColor: _tokens.colors.shellBackground,
      statusBarIconBrightness: Brightness.dark,
      systemNavigationBarColor: _tokens.colors.shellBackground,
      systemNavigationBarIconBrightness: Brightness.dark,
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
        scaffoldBackgroundColor: _tokens.colors.shellBackground,
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
