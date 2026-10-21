import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'package:ffi/ffi.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:photo_enhancement/native/bindings.dart';
import 'package:photo_enhancement/native/worker.dart';
import 'package:photo_enhancement/models/edit.dart';

void main() {
  final path = Platform.environment['PHOTO_CORE_LIBRARY'];
  test('Dart FFI layout and ownership agree with the native C ABI', () {
    final native = Bindings(path);
    expect(sizeOf<DecodeOptions>(), 16);
    expect(sizeOf<NativeParams>(), 80);
    expect(sizeOf<NativeEffects>(), 304);
    expect(sizeOf<NativeBorder>(), 1560);
    expect(sizeOf<NativeFrame>(), 2600);
    expect(sizeOf<NativeFrameTemplate>(), 3084);
    expect(sizeOf<NativeFrameAdjustments>(), 1144);
    expect(sizeOf<NativeLensInfo>(), 384);
    expect(sizeOf<NativeLook>(), 40);
    expect(sizeOf<NativeLookAnalysis>(), 32);
    expect(sizeOf<NativeInfo>(), 360);
    using((arena) {
      final options = arena<DecodeOptions>();
      native.defaultOptions(options);
      expect(options.ref.maxPixels, 32768 * 32768);
      final out = arena<Pointer<NativeImage>>();
      final status = native.decode(
          '/does-not-exist.jpg'.toNativeUtf8(allocator: arena), options, out);
      expect(status, 2);
      expect(out.value, nullptr);
      final source = arena<Uint16>(4);
      source[0] = 32768;
      source[1] = 0;
      source[2] = 65535;
      source[3] = 65535;
      native.check(native.createImage(1, 1, source, out));
      final image = out.value;
      try {
        final temporary = Directory.systemTemp.createTempSync('photo-ffi-');
        try {
          final input = '${temporary.path}/input.jpg';
          final output = '${temporary.path}/output.png';
          native.check(native.encode(
              image, input.toNativeUtf8(allocator: arena), 1, 95, 1));
          final exportParams = arena<NativeParams>();
          final exportEffects = arena<NativeEffects>();
          native.defaultEffects(exportEffects);
          final exportBorder = arena<NativeBorder>();
          exportBorder.ref.color = 0xffffff;
          native.check(native.processFile(
              input.toNativeUtf8(allocator: arena),
              output.toNativeUtf8(allocator: arena),
              options,
              exportParams,
              exportEffects,
              nullptr,
              1,
              exportBorder,
              2,
              95,
              1,
              0));
          final frame = arena<NativeFrame>();
          frame.ref.border.color = 0xffffff;
          native.check(native.addFrame(image, frame, out));
          native.freeImage(out.value);
          native.check(native.processFileFrame(
              input.toNativeUtf8(allocator: arena),
              '${temporary.path}/framed.png'.toNativeUtf8(allocator: arena),
              options,
              exportParams,
              exportEffects,
              nullptr,
              1,
              frame,
              2,
              95,
              1,
              0));
          final look = arena<NativeLook>();
          final analysis = arena<NativeLookAnalysis>();
          native.check(native.analyzeLook(image, analysis));
          expect(analysis.ref.confidence, inInclusiveRange(0, 1));
          native.check(native.processFileLook(
              input.toNativeUtf8(allocator: arena),
              '${temporary.path}/look-interface.png'
                  .toNativeUtf8(allocator: arena),
              options,
              exportParams,
              exportEffects,
              nullptr,
              1,
              frame,
              look,
              2,
              95,
              1,
              0));
          final exported = native.openImage(output, 1600);
          try {
            expect(native.imageInfo(exported)['width'], 1);
          } finally {
            native.freeImage(exported);
          }
        } finally {
          temporary.deleteSync(recursive: true);
        }
        final effects = arena<NativeEffects>();
        native.defaultEffects(effects);
        expect(effects.ref.values[25], .25);
        expect(effects.ref.values[35], 1);
        effects.ref.values[27] = .9;
        native.check(native.effects(image, effects, out));
        final advanced = out.value;
        try {
          expect(native.rgba(advanced, 4)[0],
              greaterThan(native.rgba(image, 4)[0]));
          expect(native.rgba(advanced, 4)[3], 255);
        } finally {
          native.freeImage(advanced);
        }
        final params = arena<NativeParams>();
        params.ref.exposure = 1;
        native.check(native.params(image, params, out));
        final edited = out.value;
        try {
          expect(native.imageInfo(edited)['width'], 1);
          final rgba = native.rgba(edited, 4);
          expect(rgba[0], 255);
          expect(rgba[1], 0);
          expect(rgba[2], 255);
          expect(rgba[3], 255);
        } finally {
          native.freeImage(edited);
        }
      } finally {
        native.freeImage(image);
      }
    });
  },
      skip: path == null
          ? 'Set PHOTO_CORE_LIBRARY after building the native kernel'
          : false);
  test(
      'worker comparison and thumbnails use unedited source without altering exports',
      () async {
    final native = Bindings(path);
    final directory = Directory.systemTemp.createTempSync('photo-comparison-');
    final input = '${directory.path}/source.png';
    final worker = ImageWorker();
    try {
      using((arena) {
        final rgba = arena<Uint16>(4);
        rgba[0] = 12000;
        rgba[1] = 8000;
        rgba[2] = 16000;
        rgba[3] = 65535;
        final out = arena<Pointer<NativeImage>>();
        native.check(native.createImage(1, 1, rgba, out));
        try {
          native.check(native.encode(
              out.value, input.toNativeUtf8(allocator: arena), 2, 95, 1));
        } finally {
          native.freeImage(out.value);
        }
      });
      final recipe = EditRecipe().control(0, 1);
      final result = await worker
          .call('preview', {'path': input, 'recipe': recipe.toJson()});
      final edited = (result['pixels'] as TransferableTypedData)
          .materialize()
          .asUint8List();
      final original = (result['originalPixels'] as TransferableTypedData)
          .materialize()
          .asUint8List();
      expect(edited[0], greaterThan(original[0]));
      final neutral = await worker
          .call('preview', {'path': input, 'recipe': EditRecipe().toJson()});
      expect(
          (neutral['pixels'] as TransferableTypedData)
              .materialize()
              .asUint8List(),
          original);
      final thumbnails = await worker.call('thumbnails', {
        'path': input,
        'recipe': recipe.toJson(),
        'presets': [EditRecipe().toJson(), recipe.toJson()],
      });
      expect(thumbnails['thumbnails'], hasLength(2));
      final output = '${directory.path}/edited.png';
      await worker.call('export', {
        'path': input,
        'recipe': recipe.toJson(),
        'destination': output,
        'format': 2,
        'quality': 95,
        'metadata': true
      });
      final exported = native.openImage(output, 1600);
      try {
        expect(native.rgba(exported, 4), edited);
      } finally {
        native.freeImage(exported);
      }
    } finally {
      worker.dispose();
      directory.deleteSync(recursive: true);
    }
  },
      skip: path == null
          ? 'Set PHOTO_CORE_LIBRARY after rebuilding the native kernel'
          : false);
}
