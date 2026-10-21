import 'dart:io';
import 'package:flutter/services.dart';
import '../models/edit.dart';
import '../models/frame_marks.dart';

/// Resolve portable IDs into owned files shared by preview and export.
EditRecipe resolveFrameResources(
    EditRecipe recipe, String make, Map<String, String> paths,
    {String lensMake = '', String lensModel = '', String cameraModel = ''}) {
  if (recipe.borderBottom == 0) return recipe;
  var result = recipe;
  if (recipe.frameFont != 'custom') {
    final font = paths['font:${recipe.frameFont}'];
    if (font == null) throw StateError('内置边框字体尚未就绪');
    result = result.copyWith(fontPath: font);
  }
  final mark = recipe.frameMark == 'auto'
      ? markForMake(make, cameraModel)
      : recipe.frameMark;
  final accent = recipe.frameAccent == 'auto'
      ? accentForLens(lensMake, lensModel)
      : recipe.frameAccent;
  final tone = resolvedMarkTone(recipe.markTone, recipe.borderColor);
  // Accept old resource maps while callers migrate from combined PNGs.
  final combined = paths[frameAssetKey(mark, accent, tone)];
  if (!recipe.frameAdvanced &&
      combined != null &&
      (mark.isNotEmpty || accent.isNotEmpty)) {
    return result.copyWith(logoPath: combined, accentPath: '', frameBrand: 0);
  }
  String artwork(String id) {
    final path = paths['art:$id:$tone'];
    if (path == null) throw StateError('内置边框标识尚未就绪：$id');
    return path;
  }

  return result.copyWith(
      logoPath: mark.isEmpty ? recipe.logoPath : artwork(mark),
      accentPath: accent.isEmpty ? '' : artwork(accent),
      frameBrand:
          mark.isNotEmpty || accent.isNotEmpty || recipe.logoPath.isNotEmpty
              ? 0
              : recipe.frameBrand);
}

class FrameResources {
  final Map<String, String> paths = {};

  Future<void> initialize(Directory directory) async {
    final cache = await Directory('${directory.path}/frame-assets-v2')
        .create(recursive: true);
    for (final font in ['sans', 'mono', 'serif']) {
      final data = await rootBundle.load('assets/frame_fonts/$font.ttf');
      final file = File('${cache.path}/$font.ttf');
      await file.writeAsBytes(
          data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes),
          flush: true);
      paths['font:$font'] = file.path;
    }
    // Keep artwork separate: independent placement does not need pre-generated
    // combinations of every camera brand and every third-party lens brand.
    final ids = {...frameMarks.keys, ...frameAccents.keys}
      ..removeAll(['', 'auto']);
    for (final id in ids) {
      for (var tone = 1; tone <= 3; ++tone) {
        final data = await rootBundle.load('assets/frame_marks/$id-$tone.png');
        final file = File('${cache.path}/$id-$tone.png');
        await file.writeAsBytes(
            data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes),
            flush: true);
        paths['art:$id:$tone'] = file.path;
      }
    }
  }
}
