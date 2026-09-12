import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'src/widgets/viewport_canvas.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();

  // 移动端/桌面端统一深色沉浸式系统边框
  SystemChrome.setSystemUIOverlayStyle(
    const SystemUiOverlayStyle(
      statusBarColor: Colors.transparent,
      statusBarIconBrightness: Brightness.light,
      systemNavigationBarColor: Colors.transparent,
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
      theme: ThemeData.dark().copyWith(
        scaffoldBackgroundColor: const Color(0xFF0B0F19),
        colorScheme: const ColorScheme.dark(
          primary: Color(0xFF38BDF8),
          surface: Color(0xFF0F172A),
        ),
      ),
      home: const Scaffold(body: ViewportCanvas()),
    );
  }
}
