import 'dart:convert';
import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:photo_enhancement/models/edit.dart';
import 'package:photo_enhancement/services/presets.dart';

void main() {
  test(
      'portable frame embeds logo, ignores supplied paths and excludes colour edits',
      () async {
    final directory =
        Directory.systemTemp.createTempSync('photo-frame-preset-');
    try {
      final store = PresetStore()..directory = directory;
      final png = base64Decode(
          'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jpZkAAAAASUVORK5CYII=');
      final logo = await store.cacheLogo(png);
      final recipe = EditRecipe(
              lutPath: '/must-not-read/filter.cube',
              fontPath: '/must-not-read/font.ttf',
              logoPath: logo,
              frameBrand: 3,
              frameFields: 5,
              frameLayout: 2,
              borderBottom: .2)
          .control(0, 1);
      final serialized =
          await store.serialize('My frame', recipe, frameOnly: true);
      final data = jsonDecode(serialized) as Map<String, dynamic>;
      expect(data['cube'], isNull);
      expect(data['logo'], base64Encode(png));
      final portable = data['recipe'] as Map;
      expect(portable['lutPath'], isNull);
      final border = portable['border'] as Map;
      expect(border['fontPath'], '');
      expect(border['logoPath'], '');
      border['logoPath'] = '/must-not-read/logo.png';
      final file = File('${directory.path}/portable.json');
      await file.writeAsString(jsonEncode(data));
      final imported = await store.read(file.path);
      expect(imported.frameOnly, true);
      expect(imported.recipe.values, List<double>.filled(10, 0));
      expect(imported.recipe.frameBrand, 3);
      expect(imported.recipe.frameFields, 5);
      expect(imported.recipe.frameLayout, 2);
      expect(imported.recipe.logoPath.startsWith(directory.path), true);
      expect(await File(imported.recipe.logoPath).readAsBytes(), png);
    } finally {
      directory.deleteSync(recursive: true);
    }
  });
}
