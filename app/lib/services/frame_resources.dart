import 'dart:io';
import 'dart:convert';
import 'bundled_assets.dart';
import '../models/edit.dart';
import '../models/frame_marks.dart';

/// Resolve portable IDs into owned files shared by preview and export.
EditRecipe resolveFrameResources(
    EditRecipe recipe, String make, Map<String, String> paths,
    {String lensMake = '', String lensModel = '', String cameraModel = ''}) {
  if (recipe.template.category == 'plain') {
    return recipe.copyWith(
        borderBottom: 0,
        frameFields: 0,
        frameBrand: 0,
        logoPath: '',
        accentPath: '',
        frameMark: '',
        frameAccent: '',
        frameAdvanced: false);
  }
  if (recipe.borderBottom == 0 && !recipe.template.decorated) return recipe;
  var result = recipe;
  if (recipe.frameFont != 'custom') {
    final t = recipe.template;
    final text = t.category == 'film'
        ? '${t.filmLabel}${t.frameNumber}${t.timestamp}'
        : '${t.title}${t.account}${t.location}${t.body}${t.timestamp}${t.stats}';
    final useCjk = t.decorated &&
        recipe.frameFont == 'sans' &&
        text.runes.any((c) => c > 255);
    final font = paths['font:${useCjk ? 'cjk' : recipe.frameFont}'];
    if (font == null) throw StateError('内置边框字体尚未就绪');
    result = result.copyWith(fontPath: font);
  }
  if (recipe.template.decorated) {
    return result.copyWith(logoPath: '', accentPath: '', frameBrand: 0);
  }
  final mark = recipe.frameMark == 'auto'
      ? markForMake(make, cameraModel)
      : recipe.frameMark;
  final accent = accentForCombination(
      mark,
      recipe.frameAccent == 'auto'
          ? accentForLens(lensMake, lensModel)
          : recipe.frameAccent,
      automatic: recipe.frameAccent == 'auto');
  // Resolve the optional lens text only for rendering. Keep the saved user
  // caption and its visibility switch untouched.
  if (recipe.frameLensName && lensModel.trim().isNotEmpty) {
    final text = [
      lensModel.trim(),
      if ((recipe.frameFields & 64) != 0 && recipe.caption.isNotEmpty)
        recipe.caption
    ].join(' · ');
    final runes = <int>[];
    var bytes = 0;
    for (final rune in text.runes) {
      final size = utf8.encode(String.fromCharCode(rune)).length;
      if (bytes + size > 511) break;
      runes.add(rune);
      bytes += size;
    }
    result = result.copyWith(
        caption: String.fromCharCodes(runes),
        frameFields: recipe.frameFields | 64);
  }
  final customLogo = recipe.frameMark.isEmpty ? recipe.logoPath : '';

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
      logoPath: mark.isEmpty ? customLogo : artwork(mark),
      accentPath: accent.isEmpty ? '' : artwork(accent),
      frameBrand: mark.isNotEmpty || accent.isNotEmpty || customLogo.isNotEmpty
          ? 0
          : recipe.frameMark == 'auto'
              ? 0
              : recipe.frameBrand);
}

class FrameResources {
  final Map<String, String> paths = {};

  Future<void> initialize(Directory directory) async {
    final cache = await Directory('${directory.path}/frame-assets-v2')
        .create(recursive: true);
    final assets = <String, File>{};
    for (final font in ['sans', 'mono', 'serif', 'cjk']) {
      final file = File('${cache.path}/$font.ttf');
      assets['assets/frame_fonts/$font.ttf'] = file;
      paths['font:$font'] = file.path;
    }
    // Keep artwork separate: independent placement does not need pre-generated
    // combinations of every camera brand and every third-party lens brand.
    final ids = {...frameMarks.keys, ...frameAccents.keys}
      ..removeAll(['', 'auto']);
    for (final id in ids) {
      for (var tone = 1; tone <= 3; ++tone) {
        final file = File('${cache.path}/$id-$tone.png');
        assets['assets/frame_marks/$id-$tone.png'] = file;
        paths['art:$id:$tone'] = file.path;
      }
    }
    await installBundledAssets(assets);
  }
}
