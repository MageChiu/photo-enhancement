import 'dart:convert';
import 'dart:io';
import 'package:crypto/crypto.dart';
import 'package:flutter/services.dart';
import 'platform_files.dart';
import '../models/edit.dart';

class PhotoPreset {
  const PhotoPreset(this.name, this.description, this.recipe,
      {this.group = '本地预设', this.colorOnly = false, this.frameOnly = false});
  final String name, description, group;
  final bool colorOnly, frameOnly;
  final EditRecipe recipe;
}

class PresetStore {
  late Directory directory;
  final List<PhotoPreset> presets = [];

  Future<void> initialize() async {
    final support = await PlatformFiles.supportDirectory();
    directory =
        await Directory('${support.path}/presets').create(recursive: true);
    final manifest =
        jsonDecode(await rootBundle.loadString('assets/presets/builtin.json'))
            as List;
    for (final dynamic item in manifest) {
      final spec = Map<String, dynamic>.from(item as Map);
      final lut = spec['lut'] as String;
      final file = File('${directory.path}/builtin-$lut');
      await file.writeAsString(await rootBundle.loadString('assets/luts/$lut'),
          flush: true);
      final recipe =
          EditRecipe.fromJson(Map<String, dynamic>.from(spec['recipe'] as Map));
      presets.add(PhotoPreset(
          spec['name'] as String,
          spec['description'] as String,
          recipe.copyWith(lutPath: file.path, lutName: spec['name'] as String),
          group: spec['group'] as String? ?? '基础纪实',
          colorOnly: true));
    }
    await for (final entry in directory.list()) {
      if (entry is File && entry.path.endsWith('.preset.json')) {
        try {
          presets.add(await read(entry.path));
        } on FormatException {/* Skip damaged user presets. */}
      }
    }
  }

  static void validateLogo(List<int> bytes) {
    if (bytes.length > 4 * 1024 * 1024 ||
        bytes.length < 24 ||
        bytes.take(8).join(',') != '137,80,78,71,13,10,26,10') {
      throw const FormatException('请选择不超过 4MB 的 PNG 图标');
    }
    int integer(int offset) =>
        bytes[offset] << 24 |
        bytes[offset + 1] << 16 |
        bytes[offset + 2] << 8 |
        bytes[offset + 3];
    final width = integer(16), height = integer(20);
    if (width <= 0 ||
        height <= 0 ||
        width > 32768 ||
        height > 32768 ||
        width * height > 1024 * 1024) {
      throw const FormatException('标识不能超过 100 万像素，请先缩小 PNG 图标');
    }
  }

  Future<String> cacheLogo(List<int> bytes) async {
    validateLogo(bytes);
    final hash = sha256.convert(bytes);
    final file = File('${directory.path}/logo-$hash.png');
    await file.writeAsBytes(bytes, flush: true);
    return file.path;
  }

  Future<PhotoPreset> read(String path) async {
    final file = File(path);
    if (await file.length() > 24 * 1024 * 1024) {
      throw const FormatException('预设文件不能超过 24MB');
    }
    final decoded = jsonDecode(await file.readAsString());
    if (decoded is! Map<String, dynamic> ||
        decoded['version'] != 1 ||
        decoded['name'] is! String ||
        (decoded['name'] as String).trim().isEmpty ||
        (decoded['name'] as String).length > 100 ||
        decoded['recipe'] is! Map) {
      throw const FormatException('预设文件结构无效');
    }
    final cube = decoded['cube'];
    final logo = decoded['logo'];
    if (logo != null && (logo is! String || logo.length > 6 * 1024 * 1024)) {
      throw const FormatException('预设标识数据无效');
    }
    if (cube != null &&
        (cube is! String || utf8.encode(cube).length > 15 * 1024 * 1024)) {
      throw const FormatException('预设 LUT 数据无效');
    }
    // Imported absolute paths are never trusted or used. A portable preset owns
    // its LUT data; fonts are chosen locally on the destination device.
    var recipe =
        EditRecipe.fromJson(Map<String, dynamic>.from(decoded['recipe'] as Map))
            .copyWith(
                clearLut: true, fontPath: '', logoPath: '', accentPath: '');
    if (recipe.frameFont == 'custom') {
      recipe = recipe.copyWith(frameFont: 'sans');
    }
    if (cube is String) {
      final hash = sha256.convert(utf8.encode(cube));
      // The filename is an internal cache key, never an imported filesystem path.
      final lut = File('${directory.path}/import-$hash.cube');
      await lut.writeAsString(cube, flush: true);
      recipe = recipe.copyWith(
          lutPath: lut.path, lutName: decoded['name'] as String);
    }
    if (logo is String) {
      final bytes = base64Decode(logo);
      recipe = recipe.copyWith(logoPath: await cacheLogo(bytes));
    }
    final kind = decoded['kind'] ?? 'edit';
    if (kind != 'edit' && kind != 'frame') {
      throw const FormatException('预设类型无效');
    }
    return PhotoPreset(
        decoded['name'] as String, kind == 'frame' ? '仅边框' : '本地预设', recipe,
        group: kind == 'frame' ? '边框预设' : '本地预设', frameOnly: kind == 'frame');
  }

  Future<String> serialize(String name, EditRecipe recipe,
      {bool frameOnly = false}) async {
    if (name.trim().isEmpty || name.length > 100) {
      throw const FormatException('预设名称须为 1–100 个字符');
    }
    if (frameOnly) recipe = EditRecipe().withFrame(recipe);
    final portable = recipe.copyWith(
        clearLut: true, fontPath: '', logoPath: '', accentPath: '');
    String? cube;
    if (recipe.lutPath != null) {
      final file = File(recipe.lutPath!);
      if (await file.length() > 15 * 1024 * 1024) {
        throw const FormatException('LUT 过大');
      }
      cube = await file.readAsString();
    }
    String? logo;
    if (recipe.logoPath.isNotEmpty) {
      final file = File(recipe.logoPath);
      if (await file.length() > 4 * 1024 * 1024) {
        throw const FormatException('标识不能超过 4MB');
      }
      logo = base64Encode(await file.readAsBytes());
    }
    final serialized = jsonEncode({
      'version': 1,
      'name': name.trim(),
      'kind': frameOnly ? 'frame' : 'edit',
      'recipe': portable.toJson(),
      'cube': cube,
      'logo': logo
    });
    if (utf8.encode(serialized).length > 24 * 1024 * 1024) {
      throw const FormatException('完整预设不能超过 24MB');
    }
    return serialized;
  }

  Future<PhotoPreset> save(String name, EditRecipe recipe,
      {bool frameOnly = false}) async {
    final file = File(
        '${directory.path}/user-${DateTime.now().microsecondsSinceEpoch}.preset.json');
    if (frameOnly) recipe = EditRecipe().withFrame(recipe);
    await file.writeAsString(
        await serialize(name, recipe, frameOnly: frameOnly),
        flush: true);
    final preset = PhotoPreset(name.trim(), frameOnly ? '仅边框' : '本地预设', recipe,
        group: frameOnly ? '边框预设' : '本地预设', frameOnly: frameOnly);
    presets.add(preset);
    return preset;
  }
}
