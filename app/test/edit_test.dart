import 'dart:convert';
import 'package:flutter_test/flutter_test.dart';
import 'package:photo_enhancement/models/edit.dart';

void main() {
  test('recipe defaults and JSON roundtrip', () {
    final recipe = EditRecipe().control(0, 1.25).copyWith(borderWidth: .05, borderBottom: .1, caption: '现场');
    final decoded = EditRecipe.fromJson(jsonDecode(jsonEncode(recipe.toJson())) as Map<String, dynamic>);
    expect(decoded, recipe); expect(decoded.values.length, 10);
  });
  test('history groups a complete drag and invalidates redo', () {
    final history = EditHistory(); final before = history.current;
    history.current = history.current.control(0, .1);
    history.current = history.current.control(0, .2);
    history.commit(history.current, previous: before);
    history.undo(); expect(history.current, before); expect(history.canRedo, true);
    history.redo(); expect(history.current.values[0], .2);
    history.undo(); history.commit(history.current.control(1, .2)); expect(history.canRedo, false);
  });
  test('history ignores no-op and reset clears both stacks', () {
    final history = EditHistory(); history.commit(history.current); expect(history.canUndo, false);
    history.commit(history.current.control(0, 1)); history.undo(); history.reset();
    expect(history.canUndo, false); expect(history.canRedo, false);
  });
  test('preset rejects invalid values and versions', () {
    expect(() => EditRecipe.fromJson({'version': 2}), throwsFormatException);
    expect(() => EditRecipe.fromJson({'version': 1, 'params': {'exposure': 6}}), throwsFormatException);
    expect(() => EditRecipe.fromJson({'version': 1, 'params': {'contrast': double.nan}}), throwsFormatException);
    expect(() => EditRecipe.fromJson({'version': 1, 'params': {}, 'border': {'color': -1}}), throwsFormatException);
    expect(() => EditRecipe.fromJson({'version': 1, 'params': {}, 'border': {'caption': List.filled(200, '中').join()}}), throwsFormatException);
  });
  test('recipe values cannot be mutated', () {
    final original = [1.0, ...List.filled(9, 0.0)]; final recipe = EditRecipe(values: original);
    original[0] = 2; expect(recipe.values[0], 1);
    expect(() => recipe.values[0] = 2, throwsUnsupportedError);
  });
  test('LUT removal is explicit', () {
    final recipe = EditRecipe(lutPath: '/local/example.cube', lutName: '测试');
    expect(recipe.copyWith(clearLut: true).lutPath, isNull);
    expect(recipe.copyWith(strength: .5).lutPath, recipe.lutPath);
  });
}
