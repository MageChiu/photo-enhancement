import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'package:ffi/ffi.dart';
import '../models/edit.dart';
import '../services/frame_resources.dart';
import 'bindings.dart';

class ImageWorker {
  Isolate? _isolate;
  SendPort? _port;
  final _receive = ReceivePort();
  final _errors = ReceivePort();
  final Map<int, Completer<Map<String, dynamic>>> _requests = {};
  int _next = 0;
  bool _disposed = false;
  Future<void>? _starting;
  Completer<void>? _ready;
  Object? _failure;

  Future<void> start() => _starting ??= _start();
  Future<void> _start() async {
    final ready = Completer<void>();
    _ready = ready;
    _receive.listen((dynamic message) {
      if (message is SendPort) {
        _port = message;
        if (!ready.isCompleted) ready.complete();
        return;
      }
      final data = Map<String, dynamic>.from(message as Map);
      final pending = _requests.remove(data['id']);
      if (pending == null) return;
      if (data['error'] != null) {
        pending
            .completeError(ImageProcessingException(data['error'] as String));
      } else {
        pending.complete(data);
      }
    });
    _errors.listen((dynamic error) {
      final failure = StateError('图像工作线程退出：$error');
      _failure = failure;
      if (!ready.isCompleted) ready.completeError(failure);
      for (final request in _requests.values) {
        request.completeError(failure);
      }
      _requests.clear();
    });
    _isolate = await Isolate.spawn(
        _entry, [_receive.sendPort, Platform.environment['PHOTO_CORE_LIBRARY']],
        onError: _errors.sendPort, errorsAreFatal: true);
    if (_disposed) _isolate?.kill(priority: Isolate.immediate);
    await ready.future;
    if (_disposed) {
      _isolate?.kill(priority: Isolate.immediate);
      throw StateError('图像线程已关闭');
    }
  }

  Future<Map<String, dynamic>> call(
      String operation, Map<String, Object?> arguments) async {
    if (_disposed) throw StateError('图像线程已关闭');
    await start();
    if (_disposed) throw StateError('图像线程已关闭');
    if (_failure != null) throw StateError('图像线程不可用：$_failure');
    final id = ++_next;
    final result = Completer<Map<String, dynamic>>();
    _requests[id] = result;
    _port!.send({'id': id, 'operation': operation, ...arguments});
    return result.future;
  }

  void dispose() {
    _disposed = true;
    // Graceful close releases cached native images after the current operation.
    if (_port != null) {
      _port!.send({'operation': 'close'});
    } else {
      _isolate?.kill(priority: Isolate.immediate);
      final ready = _ready;
      if (ready != null && !ready.isCompleted) {
        ready.completeError(StateError('编辑器已关闭'));
      }
    }
    _receive.close();
    _errors.close();
    for (final pending in _requests.values) {
      pending.completeError(StateError('编辑器已关闭'));
    }
    _requests.clear();
  }
}

void _entry(List<Object?> startup) {
  final reply = startup[0] as SendPort;
  final native = Bindings(startup[1] as String?);
  final receive = ReceivePort();
  reply.send(receive.sendPort);
  Pointer<NativeImage> source = nullptr;
  String? sourcePath;
  Map<String, String> lens = {};
  receive.listen((dynamic raw) {
    final message = Map<String, dynamic>.from(raw as Map);
    if (message['operation'] == 'close') {
      native.freeImage(source);
      receive.close();
      return;
    }
    final id = message['id'];
    try {
      if (message['operation'] == 'validateLut') {
        using((arena) {
          final out = arena<Pointer<NativeLut>>();
          native.check(native.loadLut(
              (message['lutPath'] as String).toNativeUtf8(allocator: arena),
              out));
          native.freeLut(out.value);
        });
        reply.send({'id': id});
        return;
      }
      final path = message['path'] as String;
      var recipe = EditRecipe.fromJson(
          Map<String, dynamic>.from(message['recipe'] as Map));
      final assets =
          Map<String, String>.from(message['frameAssets'] as Map? ?? {});
      final exporting = message['operation'] == 'export';
      if (exporting) {
        recipe = resolveFrameResources(
            recipe, message['make'] as String? ?? '', assets,
            cameraModel: (message['model'] as String?) ?? '',
            lensMake: (message['lensMake'] as String?) ?? '',
            lensModel: (message['lensModel'] as String?) ?? '');
        using((arena) {
          final options = arena<DecodeOptions>();
          native.defaultOptions(options);
          options.ref.maxEdge = 0;
          if (Platform.isIOS ||
              Platform.isAndroid ||
              Platform.operatingSystem == 'ohos') {
            options.ref.maxFileBytes = 256 * 1024 * 1024;
          }
          final params = arena<NativeParams>();
          _fillParams(params.ref, recipe);
          final effects = arena<NativeEffects>();
          for (var i = 0; i < 38; ++i) {
            effects.ref.values[i] = recipe.effects[i];
          }
          final border = arena<NativeFrame>();
          _fillFrame(border.ref, recipe);
          final look = arena<NativeLook>();
          _fillLook(look.ref, recipe);
          native.check(native.processFileTemplate(
              path.toNativeUtf8(allocator: arena),
              (message['destination'] as String).toNativeUtf8(allocator: arena),
              options,
              params,
              effects,
              (recipe.lutPath ?? '').toNativeUtf8(allocator: arena),
              recipe.strength,
              border,
              look,
              _adjustments(arena, recipe),
              _template(arena, recipe),
              message['format'] as int,
              message['quality'] as int,
              message['metadata'] == true ? 1 : 0,
              0));
        });
        reply.send({'id': id, 'destination': message['destination']});
        return;
      }
      if (sourcePath != path || source == nullptr) {
        // Release the old preview before opening another large RAW.
        native.freeImage(source);
        source = nullptr;
        sourcePath = null;
        source = native.openImage(path, 1600);
        lens = native.lensInfo(path);
        sourcePath = path;
      }
      final input = source;
      if (message['operation'] == 'inspect') {
        reply.send({
          'id': id,
          'info': {...native.imageInfo(input), ...lens}
        });
        return;
      }
      if (message['operation'] == 'thumbnails') {
        final results = <Map<String, dynamic>>[];
        using((arena) {
          final out = arena<Pointer<NativeImage>>();
          native.check(native.resizeImage(input, 192, out));
          final small = out.value;
          try {
            for (final dynamic spec in message['presets'] as List) {
              final look =
                  EditRecipe.fromJson(Map<String, dynamic>.from(spec as Map))
                      .copyWith(borderWidth: 0, borderBottom: 0);
              final image = _render(native, small, look);
              try {
                final info = native.imageInfo(image);
                results.add({
                  'width': info['width'],
                  'height': info['height'],
                  'pixels': TransferableTypedData.fromList([
                    native.rgba(image,
                        (info['width'] as int) * (info['height'] as int) * 4)
                  ])
                });
              } finally {
                native.freeImage(image);
              }
            }
          } finally {
            native.freeImage(small);
          }
        });
        reply.send({'id': id, 'thumbnails': results});
        return;
      }
      Pointer<NativeImage> rendered = nullptr;
      Pointer<NativeImage> originalFrame = nullptr;
      try {
        recipe = resolveFrameResources(
            recipe, native.imageInfo(input)['make'] as String? ?? '', assets,
            cameraModel: native.imageInfo(input)['model'] as String? ?? '',
            lensMake: lens['lensMake'] ?? '',
            lensModel: lens['lensModel'] ?? '');
        rendered = _render(native, input, recipe);
        final info = native.imageInfo(rendered);
        final length = (info['width'] as int) * (info['height'] as int) * 4;
        final original = {...native.imageInfo(input), ...lens};
        originalFrame = _frame(native, input, recipe);
        reply.send({
          'id': id,
          'info': original,
          'width': info['width'],
          'height': info['height'],
          'capabilities': native.capabilities,
          'lookAnalysis': recipe.lookKind == 1
              ? _analysis(native, input)
              : <String, dynamic>{},
          'originalPixels': TransferableTypedData.fromList(
              [native.rgba(originalFrame, length)]),
          'pixels':
              TransferableTypedData.fromList([native.rgba(rendered, length)])
        });
      } finally {
        native.freeImage(rendered);
        native.freeImage(originalFrame);
      }
    } catch (error) {
      reply.send({'id': id, 'error': error.toString()});
    }
  });
}

Pointer<NativeImage> _render(
    Bindings native, Pointer<NativeImage> input, EditRecipe recipe) {
  Pointer<NativeImage> current = nullptr;
  try {
    using((arena) {
      final params = arena<NativeParams>();
      _fillParams(params.ref, recipe);
      final out = arena<Pointer<NativeImage>>();
      native.check(native.params(input, params, out));
      current = out.value;
    });
    if (recipe.hasEffects) {
      using((arena) {
        final effects = arena<NativeEffects>();
        for (var i = 0; i < 38; ++i) {
          effects.ref.values[i] = recipe.effects[i];
        }
        final out = arena<Pointer<NativeImage>>();
        native.check(native.effects(current, effects, out));
        native.freeImage(current);
        current = out.value;
      });
    }
    if (recipe.lutPath != null) {
      using((arena) {
        final lut = arena<Pointer<NativeLut>>();
        native.check(native.loadLut(
            recipe.lutPath!.toNativeUtf8(allocator: arena), lut));
        try {
          final out = arena<Pointer<NativeImage>>();
          if (recipe.lookKind == 1) {
            final look = arena<NativeLook>();
            _fillLook(look.ref, recipe);
            final analysis = arena<NativeLookAnalysis>();
            // Analyse decoded source, before manual edits. Do not auto-cancel
            // the warmth/exposure the user deliberately sets with sliders.
            native.check(native.analyzeLook(input, analysis));
            final info = native.imageInfo(current);
            native.check(native.applyLook(
                current,
                lut.value,
                recipe.strength,
                look,
                analysis,
                info['width'] as int,
                info['height'] as int,
                0,
                out));
          } else {
            native.check(native.lut(current, lut.value, recipe.strength, out));
          }
          native.freeImage(current);
          current = out.value;
        } finally {
          native.freeLut(lut.value);
        }
      });
    }
    final framed = _frame(native, current, recipe);
    native.freeImage(current);
    current = framed;
    return current;
  } catch (_) {
    native.freeImage(current);
    rethrow;
  }
}

void _fillParams(NativeParams p, EditRecipe recipe) {
  final v = recipe.values;
  p.exposure = v[0];
  p.contrast = v[1];
  p.highlights = v[2];
  p.shadows = v[3];
  p.whites = v[4];
  p.blacks = v[5];
  p.temperature = v[6];
  p.tint = v[7];
  p.saturation = v[8];
  p.vibrance = v[9];
}

Pointer<NativeImage> _frame(
        Bindings native, Pointer<NativeImage> image, EditRecipe recipe) =>
    using((arena) {
      final frame = arena<NativeFrame>();
      _fillFrame(frame.ref, recipe);
      final out = arena<Pointer<NativeImage>>();
      native.check(native.addFrameTemplate(image, frame,
          _adjustments(arena, recipe), _template(arena, recipe), out));
      return out.value;
    });

Pointer<NativeFrameAdjustments> _adjustments(Arena arena, EditRecipe recipe) {
  final a = arena<NativeFrameAdjustments>();
  a.ref.enabled = recipe.frameAdvanced ? 1 : 0;
  a.ref.infoAlign = recipe.frameInfoAlign;
  a.ref.captionAlign = recipe.frameCaptionAlign;
  for (var i = 0; i < 13; ++i) {
    a.ref.positions[i] = recipe.framePositions[i];
  }
  writeText(a.ref.accentPath, 1024, recipe.accentPath);
  return a;
}

void _fillFrame(NativeFrame frame, EditRecipe recipe) {
  final b = frame.border;
  b.width = recipe.borderWidth;
  b.bottom = recipe.template.category == 'plain' ? 0 : recipe.borderBottom;
  b.color = recipe.borderColor;
  writeText(b.text, 512, recipe.caption);
  writeText(b.fontPath, 1024, recipe.fontPath);
  frame.fields = recipe.frameFields;
  frame.layout = recipe.frameLayout;
  frame.brand = recipe.frameBrand;
  writeText(frame.logoPath, 1024, recipe.logoPath);
}

Pointer<NativeFrameTemplate> _template(Arena arena, EditRecipe recipe) {
  final out = arena<NativeFrameTemplate>();
  final t = recipe.template;
  out.ref.kind = t.category == 'film'
      ? 1
      : t.category == 'social'
          ? 2
          : 0;
  out.ref.variant = t.category == 'film' ? t.filmVariant : t.variant;
  out.ref.flags =
      (t.holes ? 1 : 0) | (t.showAvatar ? 2 : 0) | (t.showActions ? 4 : 0);
  writeText(out.ref.title, 128, t.category == 'film' ? t.filmLabel : t.title);
  writeText(
      out.ref.subtitle, 128, t.category == 'film' ? t.frameNumber : t.account);
  writeText(out.ref.location, 256, t.location);
  writeText(out.ref.body, 1024, t.body);
  writeText(out.ref.timestamp, 256, t.timestamp);
  writeText(out.ref.stats, 256, t.stats);
  writeText(out.ref.avatarPath, 1024, t.avatarPath);
  return out;
}

void _fillLook(NativeLook look, EditRecipe recipe) {
  look.kind = recipe.lutPath == null ? 0 : recipe.lookKind;
  look.adaptive = recipe.lookAdaptive ? 1 : 0;
  look.adaptation = recipe.lookAdaptation;
  look.texture = recipe.lookTexture;
  look.vignette = recipe.lookVignette;
  look.grain = recipe.lookGrain;
}

Map<String, dynamic> _analysis(Bindings native, Pointer<NativeImage> source) =>
    using((arena) {
      final analysis = arena<NativeLookAnalysis>();
      native.check(native.analyzeLook(source, analysis));
      return {
        'exposure': analysis.ref.exposure,
        'temperature': analysis.ref.temperature,
        'confidence': analysis.ref.confidence,
        'median': analysis.ref.median
      };
    });
