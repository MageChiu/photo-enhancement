import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:photo_enhancement/main.dart';
import 'package:photo_enhancement/editor_controller.dart';
import 'package:photo_enhancement/models/edit.dart';

Future<void> tapVisible(WidgetTester tester, Finder finder) async {
  await Scrollable.ensureVisible(tester.element(finder), alignment: .5);
  await tester.pumpAndSettle();
  await tester.tap(finder);
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('frame text dialogs survive save cancel and dismissal animations',
      (tester) async {
    await tester.pumpWidget(const PhotoApp());
    final dynamic state = tester.state(find.byType(EditorPage));
    final editor = state.editor as EditorController;
    await tester.tap(find.text('相框'));
    await tester.pump();
    for (final entry in [
      (category: 'social', label: '昵称', value: '新的昵称'),
      (category: 'social', label: '配文', value: '第一行\n第二行'),
      (category: 'film', label: '胶片文字', value: 'FILM 200'),
      (category: 'photo', label: '设置自定义文字', value: '新的题注'),
    ]) {
      editor.change(editor.recipe.copyWith(
          template: editor.recipe.template.copyWith(category: entry.category)));
      await tester.pump();
      final panel = find
          .descendant(
              of: find.byType(ListView), matching: find.byType(Scrollable))
          .last;
      tester.state<ScrollableState>(panel).position.jumpTo(0);
      await tester.pump();
      await tester.scrollUntilVisible(find.text(entry.label), 180,
          scrollable: panel);
      await tapVisible(tester, find.text(entry.label));
      await tester.enterText(find.byType(TextFormField), entry.value);
      await tester.tap(find.widgetWithText(FilledButton, '保存'));
      // Rebuild the editor while the closing dialog is still mounted.
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 50));
      expect(tester.takeException(), isNull);
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull);
      final recipe = editor.recipe;
      if (entry.label == '昵称') expect(recipe.template.title, entry.value);
      if (entry.label == '配文') expect(recipe.template.body, entry.value);
      if (entry.label == '胶片文字') expect(recipe.template.filmLabel, entry.value);
      if (entry.label == '设置自定义文字') expect(recipe.caption, entry.value);
      for (final dismissWithBarrier in [false, true]) {
        await tapVisible(tester, find.text(entry.label));
        await tester.enterText(find.byType(TextFormField), 'discard me');
        if (dismissWithBarrier) {
          await tester.tapAt(const Offset(5, 5));
        } else {
          await tester.tap(find.widgetWithText(TextButton, '取消'));
        }
        await tester.pump();
        await tester.pump(const Duration(milliseconds: 50));
        expect(tester.takeException(), isNull);
        await tester.pumpAndSettle();
        expect(tester.takeException(), isNull);
        expect(editor.recipe, recipe);
      }
    }
    await tester.pumpWidget(const SizedBox.shrink());
  });

  testWidgets(
      'cancel frame is available for every category and preserves edits',
      (tester) async {
    await tester.pumpWidget(const PhotoApp());
    final dynamic state = tester.state(find.byType(EditorPage));
    final editor = state.editor as EditorController;
    await tester.tap(find.text('相框'));
    await tester.pump();
    for (final category in ['photo', 'film', 'social', 'plain']) {
      editor.change(EditRecipe(
              borderWidth: .05,
              borderBottom: .2,
              frameMark: 'sony',
              caption: 'Keep this')
          .control(0, .7)
          .copyWith(
              template: editor.recipe.template
                  .copyWith(category: category, body: 'Keep my caption')));
      await tester.pump();
      final before = editor.recipe;
      await Scrollable.ensureVisible(
          tester.element(find.byKey(const ValueKey('remove-frame'))),
          alignment: .5);
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('remove-frame')));
      await tester.pump();
      expect(editor.recipe.template.category, 'plain');
      expect(editor.recipe.borderWidth, 0);
      expect(editor.recipe.borderBottom, 0);
      expect(editor.recipe.values[0], .7);
      expect(editor.recipe.frameMark, 'sony');
      expect(editor.recipe.caption, 'Keep this');
      expect(editor.recipe.template.body, 'Keep my caption');
      editor.undo();
      await tester.pump();
      expect(editor.recipe, before);
    }
    await tester.pumpWidget(const SizedBox.shrink());
  });

  testWidgets('frame categories show independent forms and preserve content',
      (tester) async {
    await tester.pumpWidget(const PhotoApp());
    final dynamic state = tester.state(find.byType(EditorPage));
    final editor = state.editor as EditorController;
    editor.change(
        EditRecipe(frameMark: 'sony', borderBottom: .15).control(0, .6));
    await tapVisible(tester, find.text('相框'));
    await tester.pump();
    await tapVisible(tester, find.text('社交卡片'));
    await tester.pump();
    expect(find.text('Instagram'), findsOneWidget);
    expect(find.text('相机标识'), findsNothing);
    expect(find.text('底部留白'), findsNothing);
    await tapVisible(tester, find.text('微信朋友圈'));
    await tester.pump();
    expect(editor.recipe.template.variant, 3);
    await tapVisible(tester, find.text('胶片'));
    await tester.pump();
    expect(find.text('显示齿孔'), findsOneWidget);
    expect(find.text('昵称'), findsNothing);
    await tapVisible(tester, find.text('上下齿孔'));
    await tester.pump();
    await tapVisible(tester, find.text('摄影'));
    await tester.pump();
    expect(editor.recipe.frameMark, 'sony');
    expect(editor.recipe.values[0], .6);
    await tapVisible(tester, find.text('社交卡片'));
    await tester.pump();
    expect(editor.recipe.template.variant, 3);
    editor.undo();
    await tester.pump();
    expect(editor.recipe.template.category, 'photo');
    await tapVisible(
        tester, find.byKey(const ValueKey('frame-category-plain')));
    await tester.pump();
    expect(find.text('相框文字字体'), findsNothing);
    expect(find.text('底部留白'), findsNothing);
    await tester.pumpWidget(const SizedBox.shrink());
  });
  testWidgets('editor opens without loading a native image library',
      (tester) async {
    await tester.pumpWidget(const PhotoApp());
    await tester.pump();
    expect(find.text('光屿'), findsOneWidget);
    expect(find.text('正在准备照片编辑…'), findsOneWidget);
    expect(
        tester
            .widget<FilledButton>(
                find.widgetWithText(FilledButton, '正在准备照片编辑…'))
            .onPressed,
        isNull);
    final dynamic state = tester.state(find.byType(EditorPage));
    final editor = state.editor as EditorController;
    editor.initialized = true;
    editor.change(editor.recipe);
    await tester.pump();
    expect(
        tester
            .widget<FilledButton>(find.widgetWithText(FilledButton, '选择照片'))
            .onPressed,
        isNotNull);
    expect(
        tester
            .widget<OutlinedButton>(find.widgetWithText(OutlinedButton, '导入'))
            .onPressed,
        isNotNull);
    expect(find.byType(SegmentedButton<int>), findsOneWidget);
    await tester.pumpWidget(const SizedBox.shrink());
  });

  testWidgets('camera and lens sources are independent and preserve layout',
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
    await tester.tap(find.text('相框'));
    await tester.pump();
    await Scrollable.ensureVisible(tester.element(find.text('画廊双行')),
        alignment: .5);
    await tester.pumpAndSettle();
    await tester.tap(find.text('画廊双行'));
    await tester.pump();
    expect(editor.recipe.frameLayout, 2);
    expect(editor.recipe.frameMark, 'hasselblad');
    expect(editor.recipe.frameAccent, 'sony-gm');
    expect(editor.recipe.frameFont, 'mono');
    expect(editor.recipe.frameFields, 64);
    expect(editor.recipe.caption, 'My photograph');
    expect(editor.recipe.borderColor, 0x151515);
    for (final name in ['纯色留白', '单行信息', '左右信息栏', '画廊双行', '信息栏', '画廊', '留白']) {
      await Scrollable.ensureVisible(
          tester.element(find.widgetWithText(ChoiceChip, name).last),
          alignment: .5);
      await tester.pumpAndSettle();
      await tester.tap(find.widgetWithText(ChoiceChip, name).last);
      await tester.pump();
      expect(editor.recipe.frameMark, 'hasselblad');
      expect(editor.recipe.frameAccent, 'sony-gm');
      expect(editor.recipe.frameFont, 'mono');
      expect(editor.recipe.frameFields, 64);
      expect(editor.recipe.caption, 'My photograph');
      expect(editor.recipe.borderColor, 0x151515);
    }
    await tester.scrollUntilVisible(find.text('相机标识'), 250,
        scrollable: find
            .descendant(
                of: find.byType(ListView), matching: find.byType(Scrollable))
            .last);
    final cameraSource = find.byKey(const ValueKey('camera-mark-source'));
    final lensSource = find.byKey(const ValueKey('lens-mark-source'));
    Future<void> chooseSource(Finder source, String text) async {
      final panel = find
          .descendant(
              of: find.byType(ListView), matching: find.byType(Scrollable))
          .last;
      tester.state<ScrollableState>(panel).position.jumpTo(0);
      await tester.pump();
      await tester.scrollUntilVisible(source, 250, scrollable: panel);
      await tapVisible(
          tester, find.descendant(of: source, matching: find.text(text)));
    }

    await chooseSource(cameraSource, '隐藏');
    await tester.pump();
    expect(editor.recipe.frameMark, '');
    expect(editor.recipe.frameAccent, 'sony-gm');
    expect(tester.widget<SegmentedButton<String>>(cameraSource).selected,
        {'hidden'});
    expect(editor.recipe.logoPath, '');
    expect(editor.recipe.frameBrand, 0);
    expect(editor.recipe.frameFields, 64);
    await chooseSource(cameraSource, '手动');
    await tester.pumpAndSettle();
    expect(editor.recipe.frameMark, 'sony');
    expect(editor.metadata, isEmpty);
    expect(find.text('选择标识 · 无需 EXIF，不改变照片的机型与拍摄参数'), findsOneWidget);
    final beforeSources = editor.recipe.copyWith(
        frameAdvanced: true,
        framePositions: [...defaultFramePositions]..[0] = .8);
    editor.change(beforeSources);
    await tester.pump();
    await chooseSource(lensSource, '自动');
    expect(editor.recipe.frameMark, beforeSources.frameMark);
    expect(editor.recipe.frameAccent, 'auto');
    await chooseSource(cameraSource, '自动');
    expect(editor.recipe.frameMark, 'auto');
    expect(editor.recipe.frameAccent, 'auto');
    expect(editor.recipe.framePositions, beforeSources.framePositions);
    expect(editor.recipe.frameAdvanced, isTrue);
    expect(editor.recipe.frameLayout, beforeSources.frameLayout);
    await chooseSource(lensSource, '隐藏');
    expect(editor.recipe.frameMark, 'auto');
    expect(editor.recipe.frameAccent, '');
    await chooseSource(lensSource, '手动');
    expect(editor.recipe.frameMark, 'auto');
    expect(editor.recipe.frameAccent, 'sigma');
    await chooseSource(cameraSource, '手动');
    expect(editor.recipe.frameAccent, 'sigma');
    editor.change(editor.recipe.copyWith(frameAdvanced: false));
    await tester.pump();

    await tester.scrollUntilVisible(find.text('高级排版'), 250,
        scrollable: find
            .descendant(
                of: find.byType(ListView), matching: find.byType(Scrollable))
            .last);
    await tapVisible(tester, find.text('高级排版'));
    await Scrollable.ensureVisible(
        tester.element(find.widgetWithText(SwitchListTile, '启用位置微调')),
        alignment: .5);
    await tester.pumpAndSettle();
    await tester.tap(find.widgetWithText(SwitchListTile, '启用位置微调'));
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
