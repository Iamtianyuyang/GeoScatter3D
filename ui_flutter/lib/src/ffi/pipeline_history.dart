import 'dart:convert';
import 'dart:io';

import 'app_state.dart';

Future<File> pipelineHistoryFile() async {
  final directory = await appStateDirectory();
  return File('${directory.path}${Platform.pathSeparator}last-job.json');
}

Future<void> savePipelineHistory(File file, String source, String bundle) {
  return writeJsonAtomic(file, {'source': source, 'bundle': bundle});
}

Future<(String, String)?> readPipelineHistory(File file) async {
  if (!await file.exists()) return null;
  final value = jsonDecode(await file.readAsString());
  if (value is! Map ||
      value['source'] is! String ||
      value['bundle'] is! String) {
    throw const FormatException('任务记录格式无效');
  }
  return (value['source'] as String, value['bundle'] as String);
}
