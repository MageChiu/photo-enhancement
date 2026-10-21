import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:photo_enhancement/main.dart';
import 'package:photo_enhancement/editor_controller.dart';
import 'package:photo_enhancement/models/edit.dart';

void main() {
  testWidgets('editor opens without loading a native image library',
      (tester) async {
    await tester.pumpWidget(const PhotoApp());
    await tester.pump();
    expect(find.text('光屿'), findsOneWidget);
    expect(find.text('选择照片'), findsOneWidget);
    expect(find.byType(SegmentedButton<int>), findsOneWidget);
    await tester.pumpWidget(const SizedBox.shrink());
  });

  testWidgets(
      'layout changes preserve branding and hidden mode clears all marks',
      (tester) async {
    await tester.pumpWidget(const PhotoApp());
    final dynamic state = tester.state(find.byType(EditorPage));
    final editor = state.editor as EditorController;
    editor.change(EditRecipe(
        frameMark: 'hasselblad',
        frameAccent: 'sony-gm',
        frameFont: 'mono',
        frameFields: 64,
        caption: 'My photograph',
        borderColor: 0x151515,
        borderBottom: .15));
    await tester.tap(find.text('边框'));
    await tester.pump();
    await tester.ensureVisible(find.text('画廊双行'));
    await tester.tap(find.text('画廊双行'));
    await tester.pump();
    expect(editor.recipe.frameLayout, 2);
    expect(editor.recipe.frameMark, 'hasselblad');
    expect(editor.recipe.frameAccent, 'sony-gm');
    expect(editor.recipe.frameFont, 'mono');
    expect(editor.recipe.frameFields, 64);
    expect(editor.recipe.caption, 'My photograph');
    expect(editor.recipe.borderColor, 0x151515);
    for (final name in ['信息栏', '画廊', '留白']) {
      await tester.ensureVisible(find.text(name));
      await tester.tap(find.text(name));
      await tester.pump();
      expect(editor.recipe.frameMark, 'hasselblad');
      expect(editor.recipe.frameAccent, 'sony-gm');
      expect(editor.recipe.frameFont, 'mono');
      expect(editor.recipe.frameFields, 64);
      expect(editor.recipe.caption, 'My photograph');
      expect(editor.recipe.borderColor, 0x151515);
    }
    await tester.scrollUntilVisible(find.text('标识来源'), 250,
        scrollable: find
            .descendant(
                of: find.byType(ListView), matching: find.byType(Scrollable))
            .last);
    await tester.ensureVisible(find.text('隐藏'));
    await tester.tap(find.text('隐藏'));
    await tester.pump();
    expect(editor.recipe.frameMark, '');
    expect(editor.recipe.frameAccent, '');
    expect(editor.recipe.logoPath, '');
    expect(editor.recipe.frameBrand, 0);
    expect(editor.recipe.frameFields, 64);
    await tester.tap(find.text('手动'));
    await tester.pumpAndSettle();
    expect(editor.recipe.frameMark, 'sony');
    expect(editor.metadata, isEmpty);
    expect(find.text('选择标识 · 无需 EXIF，不改变照片的机型与拍摄参数'), findsOneWidget);
    await tester.scrollUntilVisible(find.text('高级排版'), 250,
        scrollable: find
            .descendant(
                of: find.byType(ListView), matching: find.byType(Scrollable))
            .last);
    await tester.tap(find.text('高级排版'));
    await tester.pumpAndSettle();
    await Scrollable.ensureVisible(tester.element(find.byType(SwitchListTile)),
        alignment: .5);
    await tester.pumpAndSettle();
    await tester.tap(find.byType(SwitchListTile));
    await tester.pumpAndSettle();
    expect(editor.recipe.frameAdvanced, isTrue);
    expect(find.text('镜头辅助标识'), findsOneWidget);
    await Scrollable.ensureVisible(tester.element(find.text('恢复当前样式排版')),
        alignment: .5);
    await tester.pumpAndSettle();
    await tester.tap(find.text('恢复当前样式排版'));
    await tester.pumpAndSettle();
    expect(editor.recipe.frameAdvanced, isFalse);
    expect(editor.recipe.framePositions, defaultFramePositions);
    expect(editor.recipe.frameMark, 'sony');
    await tester.pumpWidget(const SizedBox.shrink());
  });
}
