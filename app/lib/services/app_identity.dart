import 'dart:io';
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

abstract final class AppIdentity {
  static const name = '光屿';
  static const description = '照片调色与风格编辑';
  static const windowChannel = MethodChannel('local.photo/window');

  static String windowTitle(String? sourcePath) {
    if (sourcePath == null || sourcePath.isEmpty) return name;
    final filename = sourcePath.split(RegExp(r'[/\\]')).last;
    return filename.isEmpty ? name : '$filename — $name';
  }

  static Future<void> updateWindowTitle(String? sourcePath) async {
    if (!Platform.isMacOS) return;
    try {
      await windowChannel
          .invokeMethod<void>('setTitle', {'title': windowTitle(sourcePath)});
    } on MissingPluginException {
      // Widget tests and hosts without the macOS window channel remain usable.
    } on PlatformException catch (error) {
      debugPrint('Window title update failed: ${error.message}');
    }
  }
}
