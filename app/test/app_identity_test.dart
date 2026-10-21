import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:photo_enhancement/editor_controller.dart';
import 'package:photo_enhancement/main.dart';
import 'package:photo_enhancement/services/app_identity.dart';

void main() {
  testWidgets('window title follows photo selection without repeat on edits',
      (tester) async {
    final calls = <MethodCall>[];
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(AppIdentity.windowChannel, (call) async {
      calls.add(call);
      return null;
    });
    addTearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(AppIdentity.windowChannel, null);
    });
    tester.view.physicalSize = const Size(1400, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    await tester.pumpWidget(const PhotoApp());
    await tester.pump();
    expect(find.text(AppIdentity.description), findsOneWidget);
    expect(calls.single.method, 'setTitle');
    expect(calls.single.arguments, {'title': '光屿'});
    final dynamic state = tester.state(find.byType(EditorPage));
    final editor = state.editor as EditorController;
    // No elapsed timer time: these pumps do not invoke native photo decoding.
    editor.sourcePath = '/photos/相册/B0015014.3FR';
    editor.change(editor.recipe);
    await tester.pump();
    expect(calls.last.arguments, {'title': 'B0015014.3FR — 光屿'});
    final selectedCount = calls.length;
    editor.change(editor.recipe.control(0, .3));
    await tester.pump();
    expect(calls.length, selectedCount);
    editor.sourcePath = '/photos/second.jpg';
    editor.change(editor.recipe);
    await tester.pump();
    expect(calls.last.arguments, {'title': 'second.jpg — 光屿'});
    // Dispose cancels the preview debounce before its first native call.
    await tester.pumpWidget(const SizedBox.shrink());
  }, skip: !Platform.isMacOS);
}
