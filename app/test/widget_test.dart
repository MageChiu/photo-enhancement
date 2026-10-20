import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:photo_enhancement/main.dart';

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
}
