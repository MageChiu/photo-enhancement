import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:photo_enhancement/native/bindings.dart';

void main() {
  final path = Platform.environment['PHOTO_CORE_LIBRARY'];
  test('Dart FFI layout and ownership agree with the native C ABI', () {
    final native = Bindings(path);
    expect(sizeOf<DecodeOptions>(), 16);
    expect(sizeOf<NativeParams>(), 80);
    expect(sizeOf<NativeBorder>(), 1560);
    expect(sizeOf<NativeInfo>(), 360);
    using((arena) {
      final options = arena<DecodeOptions>(); native.defaultOptions(options);
      expect(options.ref.maxPixels, 80000000);
      final out = arena<Pointer<NativeImage>>();
      final status = native.decode('/does-not-exist.jpg'.toNativeUtf8(allocator: arena), options, out);
      expect(status, 2); expect(out.value, nullptr);
      final source = arena<Uint16>(4);
      source[0] = 32768; source[1] = 0; source[2] = 65535; source[3] = 65535;
      native.check(native.createImage(1, 1, source, out));
      final image = out.value;
      try {
        final params = arena<NativeParams>();
        params.ref.exposure = 1;
        native.check(native.params(image, params, out));
        final edited = out.value;
        try {
          expect(native.imageInfo(edited)['width'], 1);
          final rgba = native.rgba(edited, 4);
          expect(rgba[0], 255); expect(rgba[1], 0); expect(rgba[2], 255); expect(rgba[3], 255);
        } finally { native.freeImage(edited); }
      } finally { native.freeImage(image); }
    });
  }, skip: path == null ? 'Set PHOTO_CORE_LIBRARY after building the native kernel' : false);
}
