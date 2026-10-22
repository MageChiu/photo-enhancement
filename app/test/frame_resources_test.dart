import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'dart:ui' as ui;
import 'package:ffi/ffi.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:photo_enhancement/models/edit.dart';
import 'package:photo_enhancement/models/frame_template.dart';
import 'package:photo_enhancement/models/frame_marks.dart';
import 'package:photo_enhancement/native/bindings.dart';
import 'package:photo_enhancement/native/worker.dart';
import 'package:photo_enhancement/services/frame_resources.dart';
import 'package:photo_enhancement/services/presets.dart';

void main() {
  test('portable marks, tone and fonts survive frame application and undo', () {
    final recipe = EditRecipe(
        frameMark: 'sony-alpha',
        frameAccent: 'sony-gm',
        markTone: 3,
        frameFont: 'mono',
        borderBottom: .18);
    expect(EditRecipe.fromJson(recipe.toJson()), recipe);
    final history = EditHistory();
    history.commit(EditRecipe().control(0, 1).withFrame(recipe));
    expect(history.current.frameAccent, 'sony-gm');
    expect(history.current.values[0], 1);
    history.undo();
    expect(history.current.frameMark, '');
    history.redo();
    expect(history.current.frameFont, 'mono');
    for (final field in ['mark', 'accent', 'font']) {
      final invalid = recipe.toJson();
      (invalid['border'] as Map)[field] = '../../outside';
      expect(() => EditRecipe.fromJson(invalid), throwsFormatException);
    }
    final old = recipe.toJson();
    (old['border'] as Map)
      ..remove('mark')
      ..remove('accent')
      ..remove('markTone')
      ..remove('font');
    expect(EditRecipe.fromJson(old).frameFont, 'custom');
    expect(EditRecipe.fromJson(old).frameMark, '');
  });

  test('automatic selection uses make and contrast, never invents a GM lens',
      () {
    final recipe = EditRecipe(frameMark: 'auto', borderBottom: .2);
    final assets = {
      'font:sans': '/font.ttf',
      frameAssetKey('sony', '', 2): '/sony-black.png',
      frameAssetKey('sony', '', 3): '/sony-white.png'
    };
    final light = resolveFrameResources(recipe, 'SONY Corporation', assets);
    expect(light.logoPath, '/sony-black.png');
    expect(light.frameAccent, '');
    expect(light.frameBrand, 0);
    expect(
        resolveFrameResources(
                recipe.copyWith(borderColor: 0x151515), 'Sony', assets)
            .logoPath,
        '/sony-white.png');
    expect(resolveFrameResources(recipe, 'Unknown', assets).logoPath, '');
    expect(markForMake('Hasselblad'), 'hasselblad');
    expect(markForMake('FUJIFILM'), 'fujifilm');
  });

  test(
      'manual marks ignore missing or conflicting EXIF without changing fields',
      () {
    final recipe = EditRecipe(
        frameMark: 'hasselblad',
        borderBottom: .2,
        frameFields: 31,
        caption: 'My photograph',
        frameFont: 'mono');
    final assets = {
      'font:mono': '/mono.ttf',
      frameAssetKey('hasselblad', '', 2): '/hasselblad.png'
    };
    for (final make in ['', 'SONY']) {
      final resolved = resolveFrameResources(recipe, make, assets);
      expect(resolved.logoPath, '/hasselblad.png');
      expect(resolved.frameFields, recipe.frameFields);
      expect(resolved.caption, recipe.caption);
      expect(resolved.frameFont, recipe.frameFont);
    }
  });

  test('common camera makes and lens models resolve independently', () {
    for (final entry in <String, String>{
      'Leica Camera AG': 'leica',
      'NIKON CORPORATION': 'nikon',
      'Canon': 'canon',
      'SIGMA': 'sigma',
      'Panasonic': 'lumix',
      'OLYMPUS CORPORATION': 'olympus',
      'OM Digital Solutions': 'om-system',
      'PENTAX': 'pentax',
      'RICOH IMAGING COMPANY, LTD.': 'ricoh',
      'DJI': 'dji',
      'GoPro': 'gopro',
      'EASTMAN KODAK COMPANY': 'kodak',
    }.entries) {
      expect(markForMake(entry.key), entry.value);
    }
    expect(markForMake('RICOH IMAGING COMPANY, LTD.', 'PENTAX K-1'), 'pentax');
    for (final entry in <String, String>{
      'FE 24-70mm F2.8 GM II': 'sony-gm',
      'SONY FE 20mm F1.8 G': 'sony-g',
      'RF24-70mm F2.8 L IS USM': 'canon-l',
      'NIKKOR Z 24-70mm f/2.8 S': 'nikon-s',
      'SIGMA 35mm F1.4 DG DN Art': 'sigma-art',
      'SIGMA Contemporary': 'sigma-contemporary',
      'SIGMA Sports': 'sigma-sports',
      'TAMRON 28-75mm F/2.8 Di III VXD': 'tamron',
      'ZEISS Batis 85mm': 'zeiss',
      'XF16-55mmF2.8 R LM WR': 'fujifilm',
      'XCD 55V': 'hasselblad',
      'Unknown 24-70mm': '',
    }.entries) {
      expect(accentForLens('', entry.key), entry.value, reason: entry.key);
    }
  });

  test(
      'auto lens combinations deduplicate brands and retain series and manual choices',
      () {
    final assets = <String, String>{
      'font:sans': '/font.ttf',
      for (final id in [
        'hasselblad',
        'hasselblad-h',
        'sony',
        'sony-alpha',
        'sony-gm',
        'sigma',
        'sigma-art',
        'nikon',
        'nikon-s',
        'fujifilm',
        'canon',
        'canon-l'
      ])
        'art:$id:2': '/$id.png'
    };
    for (final advanced in [false, true]) {
      for (final camera in ['auto', 'hasselblad', 'hasselblad-h']) {
        final recipe = EditRecipe(
            frameMark: camera,
            frameAccent: 'auto',
            borderBottom: .2,
            frameAdvanced: advanced);
        final resolved = resolveFrameResources(recipe, 'Hasselblad', assets,
            lensModel: 'XCD 55V');
        expect(resolved.accentPath, '');
        expect(resolved.logoPath,
            camera == 'hasselblad-h' ? '/hasselblad-h.png' : '/hasselblad.png');
        expect(resolved.framePositions, recipe.framePositions);
        expect(resolved.frameAdvanced, advanced);
      }
    }
    for (final pair in [
      ('Canon', 'Canon RF 50mm F1.8', ''),
      ('Canon', 'Canon RF 50mm F1.2 L', '/canon-l.png'),
      ('Nikon', 'NIKKOR 50mm', ''),
      ('Nikon', 'NIKKOR Z 50mm S', '/nikon-s.png'),
      ('Sony', 'FE 24-70mm F2.8 GM', '/sony-gm.png'),
      ('Sony', 'SIGMA 35mm Art', '/sigma-art.png')
    ]) {
      final resolved = resolveFrameResources(
          EditRecipe(frameMark: 'auto', frameAccent: 'auto', borderBottom: .2),
          pair.$1,
          assets,
          lensModel: pair.$2);
      expect(resolved.accentPath, pair.$3);
    }
    final manual = resolveFrameResources(
        EditRecipe(
            frameMark: 'hasselblad',
            frameAccent: 'hasselblad',
            borderBottom: .2),
        '',
        assets);
    expect(manual.accentPath, '/hasselblad.png');
    final lensOnly = resolveFrameResources(
        EditRecipe(frameMark: '', frameAccent: 'auto', borderBottom: .2),
        'Hasselblad',
        assets,
        lensModel: 'XCD 55V');
    expect(lensOnly.logoPath, '');
    expect(lensOnly.accentPath, '/hasselblad.png');
    final unknown = resolveFrameResources(
        EditRecipe(
            frameMark: 'auto',
            frameAccent: 'auto',
            logoPath: '/old.png',
            accentPath: '/old-accent.png',
            borderBottom: .2),
        'Unknown',
        assets);
    expect(unknown.logoPath, '');
    expect(unknown.accentPath, '');
  });

  test('optional lens name is portable and does not mutate the user caption',
      () {
    final recipe = EditRecipe(
        frameLensName: true,
        borderBottom: .2,
        caption: 'My caption',
        frameFields: 31);
    expect(EditRecipe.fromJson(recipe.toJson()), recipe);
    expect(EditRecipe().withFrame(recipe).frameLensName, isTrue);
    final assets = {'font:sans': '/font.ttf'};
    final resolved =
        resolveFrameResources(recipe, '', assets, lensModel: 'XCD 55V');
    expect(resolved.caption, 'XCD 55V');
    expect(resolved.frameFields & 64, 64);
    expect(recipe.caption, 'My caption');
    expect(
        resolveFrameResources(recipe.copyWith(frameFields: 95), '', assets,
                lensModel: 'XCD 55V')
            .caption,
        'XCD 55V · My caption');
    expect(resolveFrameResources(recipe, '', assets).caption, 'My caption');
    final old = recipe.toJson();
    (old['border'] as Map).remove('lensName');
    expect(EditRecipe.fromJson(old).frameLensName, isFalse);
    (old['border'] as Map)['lensName'] = 'yes';
    expect(() => EditRecipe.fromJson(old), throwsFormatException);
  });

  test('advanced placement is portable, bounded and preserves colour edits',
      () {
    final positions = List<double>.of(defaultFramePositions)..[0] = .8;
    final recipe = EditRecipe(
        frameAdvanced: true,
        framePositions: positions,
        frameInfoAlign: 1,
        frameCaptionAlign: 0,
        frameMark: 'leica',
        frameAccent: 'auto');
    expect(EditRecipe.fromJson(recipe.toJson()), recipe);
    final framed = EditRecipe().control(0, 1).withFrame(recipe);
    expect(framed.framePositions, positions);
    expect(framed.values[0], 1);
    final invalid = recipe.toJson();
    (invalid['border'] as Map)['positions'] = [...positions]..[0] = double.nan;
    expect(() => EditRecipe.fromJson(invalid), throwsFormatException);
  });

  testWidgets(
      'bundled resources compose, export portably, and render through FFI',
      (tester) async {
    await tester.runAsync(() async {
      final directory =
          Directory.systemTemp.createTempSync('photo-frame-assets-');
      final worker = ImageWorker();
      try {
        final resources = FrameResources();
        await resources.initialize(directory);
        final leica = resolveFrameResources(
            EditRecipe(frameMark: 'leica', markTone: 1, borderBottom: .2),
            '',
            resources.paths);
        final leicaCodec = await ui
            .instantiateImageCodec(await File(leica.logoPath).readAsBytes());
        final leicaImage = (await leicaCodec.getNextFrame()).image;
        final pixels = (await leicaImage.toByteData())!.buffer.asUint8List();
        var redPixels = 0;
        for (var i = 0; i < pixels.length; i += 4) {
          if (pixels[i] > 180 &&
              pixels[i + 1] < 80 &&
              pixels[i + 2] < 80 &&
              pixels[i + 3] > 200) {
            redPixels++;
          }
        }
        expect(redPixels, greaterThan(100));
        leicaImage.dispose();
        leicaCodec.dispose();
        final store = PresetStore()..directory = directory;
        final recipe = EditRecipe(
            frameMark: 'hasselblad',
            frameAccent: 'sony-gm',
            frameFont: 'mono',
            frameAdvanced: true,
            framePositions: [...defaultFramePositions]..[0] = .8,
            accentPath: '/untrusted/accent.png',
            borderWidth: .04,
            borderBottom: .25,
            caption: 'LIGHT AND SHADOW',
            frameFields: 64);
        final serialized =
            await store.serialize('Gallery', recipe, frameOnly: true);
        final portable = File('${directory.path}/portable.json');
        await portable.writeAsString(serialized);
        final imported = (await store.read(portable.path)).recipe;
        expect(imported.frameMark, 'hasselblad');
        expect(imported.frameAccent, 'sony-gm');
        expect(imported.frameFont, 'mono');
        expect(imported.logoPath, '');
        expect(imported.accentPath, '');
        expect(imported.frameAdvanced, isTrue);
        expect(imported.framePositions[0], .8);
        final resolved = resolveFrameResources(imported, '', resources.paths);
        final codec = await ui
            .instantiateImageCodec(await File(resolved.logoPath).readAsBytes());
        final image = (await codec.getNextFrame()).image;
        expect(image.width, greaterThan(image.height));
        image.dispose();
        codec.dispose();

        final library = Platform.environment['PHOTO_CORE_LIBRARY'];
        if (library == null) return;
        final native = Bindings(library);
        final input = '${directory.path}/source.jpg';
        using((arena) {
          const width = 480, height = 360;
          final rgba = arena<Uint16>(width * height * 4);
          for (var i = 0; i < width * height; ++i) {
            rgba[i * 4] = 10000 + i % width * 60;
            rgba[i * 4 + 1] = 18000;
            rgba[i * 4 + 2] = 24000;
            rgba[i * 4 + 3] = 65535;
          }
          final out = arena<Pointer<NativeImage>>();
          native.check(native.createImage(width, height, rgba, out));
          try {
            native.check(native.encode(
                out.value, input.toNativeUtf8(allocator: arena), 1, 95, 1));
          } finally {
            native.freeImage(out.value);
          }
        });
        for (var layout = 0; layout < 13; ++layout) {
          final template = layout < 6
              ? const FrameTemplate()
              : layout == 12
                  ? const FrameTemplate(category: 'plain')
                  : FrameTemplate(
                      category: layout < 8 ? 'film' : 'social',
                      filmVariant: layout == 7 ? 1 : 0,
                      variant: layout < 8 ? 0 : layout - 8,
                      title: '光屿摄影',
                      body: '午后的海边，留住风和光。\n第二段配文。',
                      location: '海边',
                      timestamp: '2026-10-21',
                      stats: '128 likes',
                      avatarPath: resources.paths['art:leica:1']!);
          final selected = recipe.copyWith(
              template: template,
              frameFont: layout >= 6 ? 'sans' : 'mono',
              frameLayout: layout % 3,
              frameAdvanced: layout >= 3,
              framePositions: defaultFramePositions,
              borderColor: layout == 1 ? 0x151515 : 0xffffff);
          final result = await worker.call('preview', {
            'path': input,
            'recipe': selected.toJson(),
            'frameAssets': resources.paths
          });
          expect(
              (result['originalPixels'] as TransferableTypedData)
                  .materialize()
                  .lengthInBytes,
              (result['width'] as int) * (result['height'] as int) * 4);
          final pixels = (result['pixels'] as TransferableTypedData)
              .materialize()
              .asUint8List();
          final output = '${directory.path}/frame-$layout.png';
          await worker.call('export', {
            'path': input,
            'recipe': selected.toJson(),
            'frameAssets': resources.paths,
            'destination': output,
            'format': 2,
            'quality': 95,
            'metadata': false
          });
          final exported = native.openImage(output, 0);
          try {
            expect(native.rgba(exported, pixels.length), pixels,
                reason:
                    'preview and JPEG strip export must agree for layout $layout');
          } finally {
            native.freeImage(exported);
          }
          if (Platform.environment['FRAME_QA_DIRECTORY'] case final String qa) {
            await File(output).copy('$qa/frame-$layout.png');
          }
        }
      } finally {
        worker.dispose();
        directory.deleteSync(recursive: true);
      }
    });
  });
}
