import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';
import 'package:ffi/ffi.dart';

final class NativeImage extends Opaque {}

final class NativeLut extends Opaque {}

final class DecodeOptions extends Struct {
  @Uint32()
  external int maxEdge;
  @Uint32()
  external int maxPixels;
  @Uint64()
  external int maxFileBytes;
}

final class NativeParams extends Struct {
  @Double()
  external double exposure;
  @Double()
  external double contrast;
  @Double()
  external double highlights;
  @Double()
  external double shadows;
  @Double()
  external double whites;
  @Double()
  external double blacks;
  @Double()
  external double temperature;
  @Double()
  external double tint;
  @Double()
  external double saturation;
  @Double()
  external double vibrance;
}

final class NativeBorder extends Struct {
  @Double()
  external double width;
  @Double()
  external double bottom;
  @Uint32()
  external int color;
  @Array(512)
  external Array<Uint8> text;
  @Array(1024)
  external Array<Uint8> fontPath;
}

final class NativeFrame extends Struct {
  external NativeBorder border;
  @Uint32()
  external int fields;
  @Uint32()
  external int layout;
  @Uint32()
  external int brand;
  @Array(1024)
  external Array<Uint8> logoPath;
}

final class NativeFrameAdjustments extends Struct {
  @Array(13)
  external Array<Double> positions;
  @Uint32()
  external int enabled;
  @Uint32()
  external int infoAlign;
  @Uint32()
  external int captionAlign;
  @Array(1024)
  external Array<Uint8> accentPath;
}

final class NativeLensInfo extends Struct {
  @Array(128)
  external Array<Uint8> make;
  @Array(256)
  external Array<Uint8> model;
}

final class NativeLook extends Struct {
  @Uint32()
  external int kind;
  @Uint32()
  external int adaptive;
  @Double()
  external double adaptation;
  @Double()
  external double texture;
  @Double()
  external double vignette;
  @Double()
  external double grain;
}

final class NativeLookAnalysis extends Struct {
  @Double()
  external double exposure;
  @Double()
  external double temperature;
  @Double()
  external double confidence;
  @Double()
  external double median;
}

final class NativeInfo extends Struct {
  @Uint32()
  external int width;
  @Uint32()
  external int height;
  @Uint32()
  external int isRaw;
  @Uint32()
  external int iso;
  @Double()
  external double aperture;
  @Double()
  external double shutter;
  @Double()
  external double focalLength;
  @Array(128)
  external Array<Uint8> make;
  @Array(128)
  external Array<Uint8> model;
  @Array(64)
  external Array<Uint8> capturedAt;
}

final class NativeEffects extends Struct {
  @Array(38)
  external Array<Double> values;
}

String readText(Array<Uint8> array, int length) {
  final bytes = <int>[];
  for (var i = 0; i < length && array[i] != 0; ++i) {
    bytes.add(array[i]);
  }
  return utf8.decode(bytes, allowMalformed: true);
}

void writeText(Array<Uint8> array, int capacity, String value) {
  final bytes = utf8.encode(value);
  if (bytes.length >= capacity || bytes.contains(0)) {
    throw const FormatException('原生接口文本超长或包含空字符');
  }
  for (var i = 0; i < capacity; ++i) {
    array[i] = i < bytes.length ? bytes[i] : 0;
  }
}

/// Deliberately hand-bound small, versioned C ABI. All calls run in a worker.
class Bindings {
  Bindings(String? libraryPath) : library = _open(libraryPath) {
    if (abiVersion() != 1) throw StateError('图像内核 ABI 不匹配，请重新构建');
    if (!library.providesSymbol('pe_process_file_advanced') ||
        !library.providesSymbol('pe_read_lens_info')) {
      throw StateError('图像内核版本过旧，请重新运行构建脚本并完整重启应用');
    }
  }
  final DynamicLibrary library;
  static DynamicLibrary _open(String? path) {
    if (path != null && path.isNotEmpty) return DynamicLibrary.open(path);
    if (Platform.isIOS) return DynamicLibrary.process();
    if (Platform.isAndroid || Platform.operatingSystem == 'ohos') {
      return DynamicLibrary.open('libphoto_core.so');
    }
    if (Platform.isMacOS) {
      final embedded =
          '${File(Platform.resolvedExecutable).parent.path}/../Frameworks/libphoto_core.dylib';
      if (File(embedded).existsSync()) return DynamicLibrary.open(embedded);
    }
    throw StateError('找不到图像内核。请先运行构建脚本，并设置 PHOTO_CORE_LIBRARY。');
  }

  late final abiVersion = library
      .lookupFunction<Uint32 Function(), int Function()>('pe_abi_version');
  late final _error = library.lookupFunction<Pointer<Utf8> Function(),
      Pointer<Utf8> Function()>('pe_last_error');
  late final _capabilities = library.lookupFunction<Pointer<Utf8> Function(),
      Pointer<Utf8> Function()>('pe_capabilities');
  String get capabilities => _capabilities().toDartString();
  late final defaultOptions = library.lookupFunction<
      Void Function(Pointer<DecodeOptions>),
      void Function(Pointer<DecodeOptions>)>('pe_default_decode_options');
  late final createImage = library.lookupFunction<
      Int32 Function(
          Uint32, Uint32, Pointer<Uint16>, Pointer<Pointer<NativeImage>>),
      int Function(int, int, Pointer<Uint16>,
          Pointer<Pointer<NativeImage>>)>('pe_image_create');
  late final decode = library.lookupFunction<
      Int32 Function(
          Pointer<Utf8>, Pointer<DecodeOptions>, Pointer<Pointer<NativeImage>>),
      int Function(Pointer<Utf8>, Pointer<DecodeOptions>,
          Pointer<Pointer<NativeImage>>)>('pe_decode');
  late final getInfo = library.lookupFunction<
      Int32 Function(Pointer<NativeImage>, Pointer<NativeInfo>),
      int Function(
          Pointer<NativeImage>, Pointer<NativeInfo>)>('pe_image_get_info');
  late final copyPixels = library.lookupFunction<
      Int32 Function(Pointer<NativeImage>, Pointer<Uint8>, Uint64),
      int Function(
          Pointer<NativeImage>, Pointer<Uint8>, int)>('pe_image_copy_rgba8');
  late final params = library.lookupFunction<
      Int32 Function(Pointer<NativeImage>, Pointer<NativeParams>,
          Pointer<Pointer<NativeImage>>),
      int Function(Pointer<NativeImage>, Pointer<NativeParams>,
          Pointer<Pointer<NativeImage>>)>('pe_apply_params');
  late final loadLut = library.lookupFunction<
      Int32 Function(Pointer<Utf8>, Pointer<Pointer<NativeLut>>),
      int Function(Pointer<Utf8>, Pointer<Pointer<NativeLut>>)>('pe_lut_load');
  late final lut = library.lookupFunction<
      Int32 Function(Pointer<NativeImage>, Pointer<NativeLut>, Double,
          Pointer<Pointer<NativeImage>>),
      int Function(Pointer<NativeImage>, Pointer<NativeLut>, double,
          Pointer<Pointer<NativeImage>>)>('pe_apply_lut');
  late final border = library.lookupFunction<
      Int32 Function(Pointer<NativeImage>, Pointer<NativeBorder>,
          Pointer<Pointer<NativeImage>>),
      int Function(Pointer<NativeImage>, Pointer<NativeBorder>,
          Pointer<Pointer<NativeImage>>)>('pe_add_border');
  late final encode = library.lookupFunction<
      Int32 Function(
          Pointer<NativeImage>, Pointer<Utf8>, Int32, Uint32, Uint32),
      int Function(
          Pointer<NativeImage>, Pointer<Utf8>, int, int, int)>('pe_encode');
  late final freeImage = library.lookupFunction<
      Void Function(Pointer<NativeImage>),
      void Function(Pointer<NativeImage>)>('pe_image_free');
  late final freeLut = library.lookupFunction<Void Function(Pointer<NativeLut>),
      void Function(Pointer<NativeLut>)>('pe_lut_free');
  late final effects = library.lookupFunction<
      Int32 Function(Pointer<NativeImage>, Pointer<NativeEffects>,
          Pointer<Pointer<NativeImage>>),
      int Function(Pointer<NativeImage>, Pointer<NativeEffects>,
          Pointer<Pointer<NativeImage>>)>('pe_apply_effects');

  late final defaultEffects = library.lookupFunction<
      Void Function(Pointer<NativeEffects>),
      void Function(Pointer<NativeEffects>)>('pe_default_effects');

  late final processFile = library.lookupFunction<
      Int32 Function(
          Pointer<Utf8>,
          Pointer<Utf8>,
          Pointer<DecodeOptions>,
          Pointer<NativeParams>,
          Pointer<NativeEffects>,
          Pointer<Utf8>,
          Double,
          Pointer<NativeBorder>,
          Int32,
          Uint32,
          Uint32,
          Uint64),
      int Function(
          Pointer<Utf8>,
          Pointer<Utf8>,
          Pointer<DecodeOptions>,
          Pointer<NativeParams>,
          Pointer<NativeEffects>,
          Pointer<Utf8>,
          double,
          Pointer<NativeBorder>,
          int,
          int,
          int,
          int)>('pe_process_file');

  late final addFrame = library.lookupFunction<
      Int32 Function(Pointer<NativeImage>, Pointer<NativeFrame>,
          Pointer<Pointer<NativeImage>>),
      int Function(Pointer<NativeImage>, Pointer<NativeFrame>,
          Pointer<Pointer<NativeImage>>)>('pe_add_frame');
  late final addFrameAdvanced = library.lookupFunction<
      Int32 Function(Pointer<NativeImage>, Pointer<NativeFrame>,
          Pointer<NativeFrameAdjustments>, Pointer<Pointer<NativeImage>>),
      int Function(
          Pointer<NativeImage>,
          Pointer<NativeFrame>,
          Pointer<NativeFrameAdjustments>,
          Pointer<Pointer<NativeImage>>)>('pe_add_frame_advanced');
  late final readLensInfo = library.lookupFunction<
      Int32 Function(Pointer<Utf8>, Uint64, Pointer<NativeLensInfo>),
      int Function(
          Pointer<Utf8>, int, Pointer<NativeLensInfo>)>('pe_read_lens_info');
  Map<String, String> lensInfo(String path) => using((arena) {
        final lens = arena<NativeLensInfo>();
        check(readLensInfo(
            path.toNativeUtf8(allocator: arena),
            Platform.isIOS ||
                    Platform.isAndroid ||
                    Platform.operatingSystem == 'ohos'
                ? 256 * 1024 * 1024
                : 512 * 1024 * 1024,
            lens));
        return {
          'lensMake': readText(lens.ref.make, 128),
          'lensModel': readText(lens.ref.model, 256)
        };
      });
  late final resizeImage = library.lookupFunction<
      Int32 Function(
          Pointer<NativeImage>, Uint32, Pointer<Pointer<NativeImage>>),
      int Function(Pointer<NativeImage>, int,
          Pointer<Pointer<NativeImage>>)>('pe_resize');
  late final processFileFrame = library.lookupFunction<
      Int32 Function(
          Pointer<Utf8>,
          Pointer<Utf8>,
          Pointer<DecodeOptions>,
          Pointer<NativeParams>,
          Pointer<NativeEffects>,
          Pointer<Utf8>,
          Double,
          Pointer<NativeFrame>,
          Int32,
          Uint32,
          Uint32,
          Uint64),
      int Function(
          Pointer<Utf8>,
          Pointer<Utf8>,
          Pointer<DecodeOptions>,
          Pointer<NativeParams>,
          Pointer<NativeEffects>,
          Pointer<Utf8>,
          double,
          Pointer<NativeFrame>,
          int,
          int,
          int,
          int)>('pe_process_file_frame');

  late final analyzeLook = library.lookupFunction<
      Int32 Function(Pointer<NativeImage>, Pointer<NativeLookAnalysis>),
      int Function(Pointer<NativeImage>,
          Pointer<NativeLookAnalysis>)>('pe_analyze_look');
  late final applyLook = library.lookupFunction<
      Int32 Function(
          Pointer<NativeImage>,
          Pointer<NativeLut>,
          Double,
          Pointer<NativeLook>,
          Pointer<NativeLookAnalysis>,
          Uint32,
          Uint32,
          Uint32,
          Pointer<Pointer<NativeImage>>),
      int Function(
          Pointer<NativeImage>,
          Pointer<NativeLut>,
          double,
          Pointer<NativeLook>,
          Pointer<NativeLookAnalysis>,
          int,
          int,
          int,
          Pointer<Pointer<NativeImage>>)>('pe_apply_look');
  late final processFileLook = library.lookupFunction<
      Int32 Function(
          Pointer<Utf8>,
          Pointer<Utf8>,
          Pointer<DecodeOptions>,
          Pointer<NativeParams>,
          Pointer<NativeEffects>,
          Pointer<Utf8>,
          Double,
          Pointer<NativeFrame>,
          Pointer<NativeLook>,
          Int32,
          Uint32,
          Uint32,
          Uint64),
      int Function(
          Pointer<Utf8>,
          Pointer<Utf8>,
          Pointer<DecodeOptions>,
          Pointer<NativeParams>,
          Pointer<NativeEffects>,
          Pointer<Utf8>,
          double,
          Pointer<NativeFrame>,
          Pointer<NativeLook>,
          int,
          int,
          int,
          int)>('pe_process_file_look');

  late final processFileAdvanced = library.lookupFunction<
      Int32 Function(
          Pointer<Utf8>,
          Pointer<Utf8>,
          Pointer<DecodeOptions>,
          Pointer<NativeParams>,
          Pointer<NativeEffects>,
          Pointer<Utf8>,
          Double,
          Pointer<NativeFrame>,
          Pointer<NativeLook>,
          Pointer<NativeFrameAdjustments>,
          Int32,
          Uint32,
          Uint32,
          Uint64),
      int Function(
          Pointer<Utf8>,
          Pointer<Utf8>,
          Pointer<DecodeOptions>,
          Pointer<NativeParams>,
          Pointer<NativeEffects>,
          Pointer<Utf8>,
          double,
          Pointer<NativeFrame>,
          Pointer<NativeLook>,
          Pointer<NativeFrameAdjustments>,
          int,
          int,
          int,
          int)>('pe_process_file_advanced');

  void check(int status) {
    if (status != 0) {
      throw ImageProcessingException('内核错误 $status：${_error().toDartString()}');
    }
  }

  Pointer<NativeImage> openImage(String path, int edge) => using((arena) {
        final options = arena<DecodeOptions>();
        defaultOptions(options);
        options.ref.maxEdge = edge;
        if (Platform.isIOS ||
            Platform.isAndroid ||
            Platform.operatingSystem == 'ohos') {
          options.ref.maxFileBytes = 256 * 1024 * 1024;
        }
        final out = arena<Pointer<NativeImage>>();
        check(decode(path.toNativeUtf8(allocator: arena), options, out));
        return out.value;
      });
  Map<String, Object> imageInfo(Pointer<NativeImage> image) => using((arena) {
        final info = arena<NativeInfo>();
        check(getInfo(image, info));
        final i = info.ref;
        return {
          'width': i.width,
          'height': i.height,
          'isRaw': i.isRaw != 0,
          'iso': i.iso,
          'aperture': i.aperture,
          'shutter': i.shutter,
          'focalLength': i.focalLength,
          'make': readText(i.make, 128),
          'model': readText(i.model, 128),
          'capturedAt': readText(i.capturedAt, 64)
        };
      });
  Uint8List rgba(Pointer<NativeImage> image, int length) => using((arena) {
        final buffer = arena<Uint8>(length);
        check(copyPixels(image, buffer, length));
        return Uint8List.fromList(buffer.asTypedList(length));
      });
}

class ImageProcessingException implements Exception {
  const ImageProcessingException(this.message);
  final String message;
  @override
  String toString() => message;
}
