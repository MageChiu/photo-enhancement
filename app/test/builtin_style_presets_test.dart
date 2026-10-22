import 'dart:convert';
import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:photo_enhancement/editor_controller.dart';
import 'package:photo_enhancement/models/edit.dart';
import 'package:photo_enhancement/models/frame_template.dart';
import 'package:photo_enhancement/services/presets.dart';

void main() {
  final manifest =
      (jsonDecode(File('assets/presets/builtin.json').readAsStringSync())
              as List)
          .map((entry) => Map<String, dynamic>.from(entry as Map))
          .toList();

  test('built-in style identifiers and LUT references are unique', () {
    expect(manifest.map((e) => e['id']).toSet().length, manifest.length);
    expect(manifest.map((e) => e['lut']).toSet().length, manifest.length);
  });

  const trialGroups = {
    'cold_steel': '冷峻纪实',
    'dark_red': '冷峻纪实',
    'ash': '冷峻纪实',
    'classic_negative': '胶片灵感',
    'classic_cinema': '经典影调',
    'natural_colour': '自然色彩',
    'classic_chrome': '胶片灵感',
    'silver_mono': '胶片灵感',
    'hard_mono': '经典影调',
  };
  for (final id in trialGroups.keys) {
    test('$id is a complete bundled 33-step trial LUT', () {
      final spec = manifest.singleWhere((entry) => entry['id'] == id);
      expect(spec['group'], trialGroups[id]);
      expect(spec['name'], contains('试用'));
      final lines = File('assets/luts/${spec['lut']}')
          .readAsLinesSync()
          .map((line) => line.trim())
          .where((line) => line.isNotEmpty && !line.startsWith('#'))
          .toList();
      expect(lines, contains('LUT_3D_SIZE 33'));
      final rows =
          lines.where((line) => !line.startsWith(RegExp(r'[A-Z]'))).toList();
      expect(rows.length, 33 * 33 * 33);
      for (final row in rows) {
        final rgb = row.split(RegExp(r'\s+')).map(double.parse).toList();
        expect(rgb.length, 3);
        for (final value in rgb) {
          expect(value.isFinite && value >= 0 && value <= 1, isTrue);
        }
      }
    });

    test('$id switches only the look and preserves new frame template settings',
        () {
      final spec = manifest.singleWhere((entry) => entry['id'] == id);
      final preset =
          EditRecipe.fromJson(Map<String, dynamic>.from(spec['recipe'] as Map))
              .copyWith(
                  lutPath: '/owned/${spec['lut']}',
                  lutName: spec['name'] as String);
      expect(preset.lookKind, 1);
      expect(EditRecipe.fromJson(preset.toJson()), preset);
      final controller = EditorController();
      try {
        controller.change(EditRecipe(
          frameMark: 'sony',
          frameAdvanced: true,
          frameInfoAlign: 1,
          caption: 'Existing frame',
          template: const FrameTemplate(category: 'social', title: 'Keep me'),
        ).control(0, .4));
        final before = controller.recipe;
        controller.applyPreset(PhotoPreset(
            spec['name'] as String, spec['description'] as String, preset,
            colorOnly: true));
        final after = controller.recipe;
        for (final field in ['params', 'effects', 'border', 'frameTemplate']) {
          expect(after.toJson()[field], before.toJson()[field]);
        }
        expect(after.toJson()['look'], preset.toJson()['look']);
        expect(after.lutPath, preset.lutPath);
        expect(after.strength, preset.strength);
        final zero = after.copyWith(strength: 0);
        expect(zero.values, before.values);
        expect(zero.toJson()['border'], before.toJson()['border']);
        controller.undo();
        expect(controller.recipe, before);
        controller.redo();
        expect(controller.recipe, after);
      } finally {
        controller.dispose();
      }
    });
  }

  test('trial LUTs have distinct palette behavior and retain tonal order', () {
    List<List<double>> cube(String id) => File('assets/luts/$id.cube')
        .readAsLinesSync()
        .where((line) => RegExp(r'^[0-9]').hasMatch(line))
        .map((line) => line.split(RegExp(r'\s+')).map(double.parse).toList())
        .toList();
    List<double> at(List<List<double>> lut, int r, int g, int b) =>
        lut[(b * 33 + g) * 33 + r];
    double chroma(List<double> rgb) {
      final sorted = List<double>.of(rgb)..sort();
      return sorted.last - sorted.first;
    }

    final steel = cube('cold_steel'), red = cube('dark_red'), ash = cube('ash');
    final steelMid = at(steel, 16, 16, 16);
    expect(steelMid[2], greaterThan(steelMid[0] + .02));
    expect(chroma(at(steel, 32, 32, 32)), lessThan(.001));
    expect(chroma(at(red, 32, 0, 0)), greaterThan(chroma(at(red, 0, 32, 0))));
    for (final color in ash) {
      expect(chroma(color), lessThanOrEqualTo(.025001));
    }
    for (final lut in [steel, red, ash]) {
      double previous = -1;
      for (var step = 0; step < 33; ++step) {
        final rgb = at(lut, step, step, step);
        final luminance = .2126 * rgb[0] + .7152 * rgb[1] + .0722 * rgb[2];
        expect(luminance, greaterThan(previous));
        previous = luminance;
      }
    }
  });

  test('heritage palettes have separate colour and monochrome directions', () {
    List<List<double>> cube(String id) => File('assets/luts/$id.cube')
        .readAsLinesSync()
        .where((line) => RegExp(r'^[0-9]').hasMatch(line))
        .map((line) => line.split(RegExp(r'\s+')).map(double.parse).toList())
        .toList();
    List<double> grey(List<List<double>> lut, int step) =>
        lut[(step * 33 + step) * 33 + step];
    const ids = [
      'classic_negative',
      'classic_cinema',
      'natural_colour',
      'classic_chrome',
      'silver_mono',
      'hard_mono'
    ];
    final luts = {for (final id in ids) id: cube(id)};
    final negativeShadow = grey(luts['classic_negative']!, 8);
    final negativeLight = grey(luts['classic_negative']!, 24);
    expect(negativeShadow[1], greaterThan(negativeShadow[0]));
    expect(negativeLight[0], greaterThan(negativeLight[1]));
    final cinema = grey(luts['classic_cinema']!, 16);
    expect(cinema[0], greaterThan(cinema[2]));
    final chrome = grey(luts['classic_chrome']!, 8);
    expect(chrome[2], greaterThan(chrome[0]));
    for (final lut in luts.values) {
      double previous = -1;
      for (var step = 0; step <= 32; ++step) {
        final rgb = grey(lut, step);
        final light = .2126 * rgb[0] + .7152 * rgb[1] + .0722 * rgb[2];
        expect(light, greaterThan(previous));
        previous = light;
      }
    }
    final natural = luts['natural_colour']!;
    expect(grey(natural, 0), [0, 0, 0]);
    expect(grey(natural, 32), [1, 1, 1]);
    for (var step = 0; step <= 32; ++step) {
      final rgb = grey(natural, step);
      expect(rgb[0], rgb[1]);
      expect(rgb[1], rgb[2]);
      expect((rgb[0] - step / 32).abs(), lessThan(.015));
    }
    for (final id in ['silver_mono', 'hard_mono']) {
      for (final rgb in luts[id]!) {
        expect(rgb[0] == rgb[1] && rgb[1] == rgb[2], isTrue,
            reason: '$id must remain neutral at full LUT strength');
      }
      final recipe = EditRecipe.fromJson(Map<String, dynamic>.from(
          manifest.singleWhere((entry) => entry['id'] == id)['recipe'] as Map));
      expect(recipe.strength, 1);
      expect(recipe.lookAdaptive, isFalse);
    }
    expect(grey(luts['hard_mono']!, 8)[0],
        lessThan(grey(luts['silver_mono']!, 8)[0]));
    expect(grey(luts['hard_mono']!, 24)[0],
        greaterThan(grey(luts['silver_mono']!, 24)[0]));
    final naturalRecipe = EditRecipe.fromJson(Map<String, dynamic>.from(manifest
            .singleWhere((entry) => entry['id'] == 'natural_colour')['recipe']
        as Map));
    expect(naturalRecipe.lookVignette, 0);
    expect(naturalRecipe.lookGrain, 0);
  });

  test('trial defaults keep warm light optional and ash texture distinct', () {
    EditRecipe recipe(String id) =>
        EditRecipe.fromJson(Map<String, dynamic>.from(manifest
            .singleWhere((entry) => entry['id'] == id)['recipe'] as Map));
    expect(recipe('cold_steel').lookAdaptive, isTrue);
    expect(recipe('dark_red').lookAdaptive, isFalse);
    expect(recipe('ash').lookAdaptive, isFalse);
    expect(
        recipe('ash').lookGrain, greaterThan(recipe('cold_steel').lookGrain));
    expect(
        recipe('ash').lookTexture, greaterThan(recipe('dark_red').lookTexture));
  });
}
