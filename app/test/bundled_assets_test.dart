import 'dart:io';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:photo_enhancement/services/bundled_assets.dart';

class _MemoryBundle extends CachingAssetBundle {
  _MemoryBundle(this.files);
  final Map<String, List<int>> files;
  @override
  Future<ByteData> load(String key) async =>
      ByteData.sublistView(Uint8List.fromList(files[key]!));
}

void main() {
  test(
      'startup assets reuse identical files and repair changed or truncated content',
      () async {
    final directory = await Directory.systemTemp.createTemp('startup-assets-');
    try {
      final font = File('${directory.path}/font.ttf');
      final lut = File('${directory.path}/look.cube');
      final source = _MemoryBundle({
        'font': [1, 2, 3, 4],
        'lut': [5, 6, 7]
      });
      final assets = {'font': font, 'lut': lut};
      await installBundledAssets(assets, bundle: source);
      expect(await font.readAsBytes(), [1, 2, 3, 4]);
      final unchanged = DateTime(2001, 1, 1);
      await font.setLastModified(unchanged);
      await lut.setLastModified(unchanged);
      final fontTime = await font.lastModified();
      final lutTime = await lut.lastModified();
      await installBundledAssets(assets, bundle: source);
      expect(await font.lastModified(), fontTime);
      expect(await lut.lastModified(), lutTime);
      // Same-sized upgrades must not be mistaken for a valid cache.
      source.files['font'] = [4, 3, 2, 1];
      await lut.writeAsBytes([0]);
      await installBundledAssets(assets, bundle: source);
      expect(await font.readAsBytes(), [4, 3, 2, 1]);
      expect(await lut.readAsBytes(), [5, 6, 7]);
      expect(await font.lastModified(), isNot(fontTime));
      // Corruption can preserve length (as happened with the bundled LUT).
      await lut.writeAsBytes([5, 0, 7]);
      await installBundledAssets(assets, bundle: source);
      expect(await lut.readAsBytes(), [5, 6, 7]);
      expect(
          directory.listSync().whereType<Directory>().where((entry) => entry
              .path
              .split(Platform.pathSeparator)
              .last
              .startsWith('.photo-assets-')),
          isEmpty);
    } finally {
      await directory.delete(recursive: true);
    }
  });
}
