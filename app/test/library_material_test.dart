import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:photo_enhancement/editor_controller.dart';
import 'package:photo_enhancement/main.dart';

void main() {
  testWidgets('recent photo tiles paint on the sidebar Material',
      (tester) async {
    tester.view.physicalSize = const Size(1400, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    await tester.pumpWidget(const PhotoApp());
    final dynamic state = tester.state(find.byType(EditorPage));
    final editor = state.editor as EditorController;
    // Notify while no source is open so this UI regression never starts FFI.
    editor.change(editor.recipe);
    editor.recentFiles.addAll(['/photos/selected.tif', '/photos/other.jpg']);
    editor.sourcePath = '/photos/selected.tif';
    await tester.pump();
    final selected = find.widgetWithText(ListTile, 'selected.tif');
    final other = find.widgetWithText(ListTile, 'other.jpg');
    expect(selected, findsOneWidget);
    expect(other, findsOneWidget);
    expect(tester.widget<ListTile>(selected).selected, isTrue);
    expect(tester.widget<ListTile>(other).selected, isFalse);
    for (final tile in [selected, other]) {
      final material =
          tester.element(tile).findAncestorWidgetOfExactType<Material>();
      expect(material, isNotNull);
      expect(material!.color, const Color(0xff17191d));
    }
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox.shrink());
  });
}
