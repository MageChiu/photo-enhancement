import 'dart:convert';
import 'dart:io';
import 'package:crypto/crypto.dart';
import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';
import '../models/edit.dart';

class PhotoPreset {
  const PhotoPreset(this.name, this.description, this.recipe);
  final String name, description;
  final EditRecipe recipe;
}

class PresetStore {
  late Directory directory;
  final List<PhotoPreset> presets = [];

  Future<void> initialize() async {
    final support = await getApplicationSupportDirectory();
    directory = await Directory('${support.path}/presets').create(recursive: true);
    final manifest = jsonDecode(await rootBundle.loadString('assets/presets/builtin.json')) as List;
    for (final dynamic item in manifest) {
      final spec = Map<String, dynamic>.from(item as Map);
      final lut = spec['lut'] as String;
      final file = File('${directory.path}/builtin-$lut');
      await file.writeAsString(await rootBundle.loadString('assets/luts/$lut'), flush: true);
      final recipe = EditRecipe.fromJson(Map<String, dynamic>.from(spec['recipe'] as Map));
      presets.add(PhotoPreset(spec['name'] as String, spec['description'] as String,
        recipe.copyWith(lutPath: file.path, lutName: spec['name'] as String)));
    }
    await for (final entry in directory.list()) {
      if (entry is File && entry.path.endsWith('.preset.json')) {
        try { presets.add(await read(entry.path)); } on FormatException { /* Skip damaged user presets. */ }
      }
    }
  }

  Future<PhotoPreset> read(String path) async {
    final file = File(path);
    if (await file.length() > 16 * 1024 * 1024) throw const FormatException('预设文件不能超过 16MB');
    final decoded = jsonDecode(await file.readAsString());
    if (decoded is! Map<String, dynamic> || decoded['version'] != 1 ||
        decoded['name'] is! String || (decoded['name'] as String).trim().isEmpty ||
        (decoded['name'] as String).length > 100 || decoded['recipe'] is! Map) {
      throw const FormatException('预设文件结构无效');
    }
    final cube = decoded['cube'];
    if (cube != null && (cube is! String || utf8.encode(cube).length > 15 * 1024 * 1024)) {
      throw const FormatException('预设 LUT 数据无效');
    }
    // Imported absolute paths are never trusted or used. A portable preset owns
    // its LUT data; fonts are chosen locally on the destination device.
    var recipe = EditRecipe.fromJson(Map<String, dynamic>.from(decoded['recipe'] as Map))
      .copyWith(clearLut: true, fontPath: '');
    if (cube is String) {
      final hash = sha256.convert(utf8.encode(cube));
      // The filename is an internal cache key, never an imported filesystem path.
      final lut = File('${directory.path}/import-$hash.cube');
      await lut.writeAsString(cube, flush: true);
      recipe = recipe.copyWith(lutPath: lut.path, lutName: decoded['name'] as String);
    }
    return PhotoPreset(decoded['name'] as String, '本地预设', recipe);
  }

  Future<String> serialize(String name, EditRecipe recipe) async {
    if (name.trim().isEmpty || name.length > 100) throw const FormatException('预设名称须为 1–100 个字符');
    final portable = recipe.copyWith(clearLut: true, fontPath: '');
    String? cube;
    if (recipe.lutPath != null) {
      final file = File(recipe.lutPath!);
      if (await file.length() > 15 * 1024 * 1024) throw const FormatException('LUT 过大');
      cube = await file.readAsString();
    }
    return jsonEncode({'version': 1, 'name': name.trim(), 'recipe': portable.toJson(), 'cube': cube});
  }

  Future<PhotoPreset> save(String name, EditRecipe recipe) async {
    final file = File('${directory.path}/user-${DateTime.now().microsecondsSinceEpoch}.preset.json');
    await file.writeAsString(await serialize(name, recipe), flush: true);
    final preset = PhotoPreset(name.trim(), '本地预设', recipe);
    presets.add(preset); return preset;
  }
}
