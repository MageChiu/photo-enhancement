import 'dart:convert';
import 'package:flutter_test/flutter_test.dart';
import 'package:photo_enhancement/models/edit.dart';
import 'package:photo_enhancement/editor_controller.dart';
import 'package:photo_enhancement/services/presets.dart';

void main() {
  test(
      'adaptive style controls roundtrip without affecting frame-only application',
      () {
    final recipe = EditRecipe(
        lookKind: 1,
        lookAdaptive: false,
        lookAdaptation: .4,
        lookTexture: .6,
        lookVignette: .5,
        lookGrain: .2,
        lutPath: '/owned/hanjun.cube');
    expect(EditRecipe.fromJson(recipe.toJson()), recipe);
    final framed =
        recipe.withFrame(EditRecipe(borderBottom: .2, frameLayout: 2));
    expect(framed.lookKind, 1);
    expect(framed.lookAdaptive, false);
    expect(framed.lookTexture, .6);
    expect(framed.lutPath, recipe.lutPath);
    final invalid = recipe.toJson();
    (invalid['look'] as Map)['grain'] = 1.1;
    expect(() => EditRecipe.fromJson(invalid), throwsFormatException);
    final legacy = recipe.toJson()..remove('look');
    expect(EditRecipe.fromJson(legacy).lookKind, 0);
  });

  test('built-in look changes preserve manual edits and frame settings', () {
    final controller = EditorController();
    try {
      controller.change(EditRecipe(
              frameLayout: 2,
              frameFields: 7,
              frameBrand: 3,
              borderBottom: .2,
              logoPath: '/owned/logo.png')
          .control(0, .75));
      final before = controller.recipe;
      controller.applyPreset(PhotoPreset(
          '蓝暮', '冷暖城市', EditRecipe(lutPath: '/owned/blue.cube', strength: .8),
          colorOnly: true));
      expect(controller.recipe.values, before.values);
      expect(controller.recipe.toJson()['border'], before.toJson()['border']);
      expect(controller.recipe.lutPath, '/owned/blue.cube');
      controller.undo();
      expect(controller.recipe, before);
      controller.redo();
      expect(controller.recipe.lutPath, '/owned/blue.cube');
    } finally {
      controller.dispose();
    }
  });

  test('frame options roundtrip and legacy custom captions stay visible', () {
    final recipe = EditRecipe(
        frameLayout: 2,
        frameFields: 127,
        frameBrand: 3,
        logoPath: '/owned/logo.png',
        caption: 'My photo',
        borderBottom: .2);
    expect(EditRecipe.fromJson(recipe.toJson()), recipe);
    final old = recipe.toJson();
    (old['border'] as Map)
      ..remove('fields')
      ..remove('layout')
      ..remove('brand')
      ..remove('logoPath');
    final restored = EditRecipe.fromJson(old);
    expect(restored.frameFields, 64);
    expect(restored.frameLayout, 0);
    expect(restored.logoPath, '');
    for (final entry in {'fields': 128, 'layout': 3, 'brand': 5}.entries) {
      final invalid = recipe.toJson();
      (invalid['border'] as Map)[entry.key] = entry.value;
      expect(() => EditRecipe.fromJson(invalid), throwsFormatException);
    }
  });

  test('advanced edits roundtrip and old presets default to identity', () {
    final recipe = EditRecipe().effect(2, .3).effect(26, .6).effect(34, .5);
    expect(recipe.hasEffects, true);
    expect(EditRecipe.fromJson(recipe.toJson()), recipe);
    final old = EditRecipe().toJson()..remove('effects');
    expect(EditRecipe.fromJson(old).hasEffects, false);
    final invalid = recipe.toJson()..['effects'] = [double.nan];
    expect(() => EditRecipe.fromJson(invalid), throwsFormatException);
    final history = EditHistory();
    history.commit(recipe);
    history.undo();
    expect(history.current.hasEffects, false);
    history.redo();
    expect(history.current, recipe);
  });
  test('recipe defaults and JSON roundtrip', () {
    final recipe = EditRecipe()
        .control(0, 1.25)
        .copyWith(borderWidth: .05, borderBottom: .1, caption: '现场');
    final decoded = EditRecipe.fromJson(
        jsonDecode(jsonEncode(recipe.toJson())) as Map<String, dynamic>);
    expect(decoded, recipe);
    expect(decoded.values.length, 10);
  });
  test('history groups a complete drag and invalidates redo', () {
    final history = EditHistory();
    final before = history.current;
    history.current = history.current.control(0, .1);
    history.current = history.current.control(0, .2);
    history.commit(history.current, previous: before);
    history.undo();
    expect(history.current, before);
    expect(history.canRedo, true);
    history.redo();
    expect(history.current.values[0], .2);
    history.undo();
    history.commit(history.current.control(1, .2));
    expect(history.canRedo, false);
  });
  test('history ignores no-op and reset clears both stacks', () {
    final history = EditHistory();
    history.commit(history.current);
    expect(history.canUndo, false);
    history.commit(history.current.control(0, 1));
    history.undo();
    history.reset();
    expect(history.canUndo, false);
    expect(history.canRedo, false);
  });
  test('preset rejects invalid values and versions', () {
    expect(() => EditRecipe.fromJson({'version': 2}), throwsFormatException);
    expect(
        () => EditRecipe.fromJson({
              'version': 1,
              'params': {'exposure': 6}
            }),
        throwsFormatException);
    expect(
        () => EditRecipe.fromJson({
              'version': 1,
              'params': {'contrast': double.nan}
            }),
        throwsFormatException);
    expect(
        () => EditRecipe.fromJson({
              'version': 1,
              'params': {},
              'border': {'color': -1}
            }),
        throwsFormatException);
    expect(
        () => EditRecipe.fromJson({
              'version': 1,
              'params': {},
              'border': {'caption': List.filled(200, '中').join()}
            }),
        throwsFormatException);
  });
  test('recipe values cannot be mutated', () {
    final original = [1.0, ...List.filled(9, 0.0)];
    final recipe = EditRecipe(values: original);
    original[0] = 2;
    expect(recipe.values[0], 1);
    expect(() => recipe.values[0] = 2, throwsUnsupportedError);
  });
  test('LUT removal is explicit', () {
    final recipe = EditRecipe(lutPath: '/local/example.cube', lutName: '测试');
    expect(recipe.copyWith(clearLut: true).lutPath, isNull);
    expect(recipe.copyWith(strength: .5).lutPath, recipe.lutPath);
  });
}
