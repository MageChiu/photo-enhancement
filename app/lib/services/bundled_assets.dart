import 'dart:io';
import 'dart:isolate';
import 'package:flutter/services.dart';

/// Compare and install a batch off the UI isolate. Equal files keep their bytes
/// and timestamps; changed or incomplete files are repaired, including upgrades.
Future<void> installBundledAssets(Map<String, File> assets,
    {AssetBundle? bundle}) async {
  final source = bundle ?? rootBundle;
  final files = <(String, Uint8List)>[];
  for (final entry in assets.entries) {
    final data = await source.load(entry.key);
    files.add((
      entry.value.path,
      data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes)
    ));
  }
  await Isolate.run(() => _installFiles(files));
}

void _installFiles(List<(String, Uint8List)> files) {
  for (final (path, bytes) in files) {
    final file = File(path);
    if (file.existsSync() && file.lengthSync() == bytes.length) {
      final previous = file.readAsBytesSync();
      var equal = true;
      for (var i = 0; i < bytes.length; ++i) {
        if (previous[i] != bytes[i]) {
          equal = false;
          break;
        }
      }
      if (equal) continue;
    }
    file.parent.createSync(recursive: true);
    final staging = file.parent.createTempSync('.photo-assets-');
    try {
      final temporary = File('${staging.path}/asset');
      temporary.writeAsBytesSync(bytes, flush: true);
      temporary.renameSync(file.path);
    } finally {
      staging.deleteSync(recursive: true);
    }
  }
}
