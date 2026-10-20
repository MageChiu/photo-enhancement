import 'dart:io';
import 'package:file_selector/file_selector.dart';
import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';

/// Mobile providers expose document URIs, not writable filesystem paths.
/// Native hosts copy imports into private storage and publish completed exports.
class PlatformFiles {
  static const channel = MethodChannel('local.photo/files');
  static bool get isMobile =>
      Platform.isIOS ||
      Platform.isAndroid ||
      Platform.operatingSystem == 'ohos';

  static Future<List<String>> open(
      {required List<String> extensions, bool multiple = false}) async {
    if (isMobile) {
      return await channel.invokeListMethod<String>(
              'open', {'extensions': extensions, 'multiple': multiple}) ??
          [];
    }
    if (!multiple) {
      final file = await openFile(acceptedTypeGroups: [XTypeGroup(label: '文件', extensions: extensions)]);
      return file == null ? [] : [file.path];
    }
    return (await openFiles(acceptedTypeGroups: [
      XTypeGroup(label: '文件', extensions: extensions)
    ]))
        .map((file) => file.path)
        .toList();
  }

  static Future<Directory> supportDirectory() async {
    if (Platform.operatingSystem == 'ohos') {
      return Directory(
          (await channel.invokeMethod<String>('supportDirectory'))!);
    }
    return getApplicationSupportDirectory();
  }

  /// The producer always receives a new path, including when the picker cancels.
  static Future<bool> save(String name, List<String> extensions,
      Future<void> Function(String) produce) async {
    if (!isMobile) {
      final location = await getSaveLocation(
          suggestedName: name,
          acceptedTypeGroups: [
            XTypeGroup(
                label: extensions.first.toUpperCase(), extensions: extensions)
          ]);
      if (location == null) return false;
      await produce(location.path);
      return true;
    }
    final support = await supportDirectory();
    final staging =
        await Directory('${support.path}/exports').create(recursive: true);
    final job = await staging.createTemp('export-');
    try {
      final path = '${job.path}/$name';
      await produce(path);
      return await channel
              .invokeMethod<bool>('save', {'path': path, 'name': name}) ??
          false;
    } finally {
      await job.delete(recursive: true);
    }
  }
}
