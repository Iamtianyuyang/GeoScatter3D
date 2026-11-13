import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'src/layouts/main_shell.dart';
import 'src/theme/app_tokens.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();

  const tokens = AppTokens.dark;
  // 三维视口贴边：系统栏也用视口底色，避免顶部/底部出现第二条色带。
  SystemChrome.setSystemUIOverlayStyle(
    SystemUiOverlayStyle(
      statusBarColor: tokens.colors.viewport,
      statusBarIconBrightness: Brightness.light,
      systemNavigationBarColor: tokens.colors.viewport,
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
      theme: buildAppTheme(),
      home: const MainShell(),
    );
  }
}
