import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'package:ffi/ffi.dart';
import '../models/edit.dart';
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
    final ready = Completer<void>(); _ready = ready;
    _receive.listen((dynamic message) {
      if (message is SendPort) {
        _port = message;
        if (!ready.isCompleted) ready.complete();
        return;
      }
      final data = Map<String, dynamic>.from(message as Map);
      final pending = _requests.remove(data['id']);
      if (pending == null) return;
      if (data['error'] != null) { pending.completeError(StateError(data['error'] as String)); }
      else { pending.complete(data); }
    });
    _errors.listen((dynamic error) {
      final failure = StateError('图像工作线程退出：$error');
      _failure = failure;
      if (!ready.isCompleted) ready.completeError(failure);
      for (final request in _requests.values) { request.completeError(failure); }
      _requests.clear();
    });
    _isolate = await Isolate.spawn(_entry,
      [_receive.sendPort, Platform.environment['PHOTO_CORE_LIBRARY']],
      onError: _errors.sendPort, errorsAreFatal: true);
    if (_disposed) _isolate?.kill(priority: Isolate.immediate);
    await ready.future;
    if (_disposed) { _isolate?.kill(priority: Isolate.immediate); throw StateError('图像线程已关闭'); }
  }

  Future<Map<String, dynamic>> call(String operation, Map<String, Object?> arguments) async {
    if (_disposed) throw StateError('图像线程已关闭');
    await start();
    if (_disposed) throw StateError('图像线程已关闭');
    if (_failure != null) throw StateError('图像线程不可用：$_failure');
    final id = ++_next;
    final result = Completer<Map<String, dynamic>>(); _requests[id] = result;
    _port!.send({'id': id, 'operation': operation, ...arguments}); return result.future;
  }
  void dispose() {
    _disposed = true;
    // Graceful close releases cached native images after the current operation.
    if (_port != null) {
      _port!.send({'operation': 'close'});
    } else {
      _isolate?.kill(priority: Isolate.immediate);
      final ready = _ready;
      if (ready != null && !ready.isCompleted) ready.completeError(StateError('编辑器已关闭'));
    }
    _receive.close(); _errors.close();
    for (final pending in _requests.values) { pending.completeError(StateError('编辑器已关闭')); }
    _requests.clear();
  }
}

void _entry(List<Object?> startup) {
  final reply = startup[0] as SendPort;
  final native = Bindings(startup[1] as String?);
  final receive = ReceivePort(); reply.send(receive.sendPort);
  Pointer<NativeImage> source = nullptr;
  String? sourcePath;
  receive.listen((dynamic raw) {
    final message = Map<String, dynamic>.from(raw as Map);
    if (message['operation'] == 'close') {
      native.freeImage(source); receive.close(); return;
    }
    final id = message['id'];
    try {
      if (message['operation'] == 'validateLut') {
        using((arena) {
          final out = arena<Pointer<NativeLut>>();
          native.check(native.loadLut((message['lutPath'] as String).toNativeUtf8(allocator: arena), out));
          native.freeLut(out.value);
        });
        reply.send({'id': id}); return;
      }
      final path = message['path'] as String;
      final recipe = EditRecipe.fromJson(Map<String, dynamic>.from(message['recipe'] as Map));
      final exporting = message['operation'] == 'export';
      Pointer<NativeImage> input;
      if (exporting) {
        input = native.openImage(path, 0);
      } else {
        if (sourcePath != path || source == nullptr) {
          // Release the old preview before opening another large RAW.
          native.freeImage(source); source = nullptr; sourcePath = null;
          source = native.openImage(path, 1600); sourcePath = path;
        }
        input = source;
      }
      Pointer<NativeImage> rendered = nullptr;
      try {
        rendered = _render(native, input, recipe);
        if (exporting) {
          using((arena) => native.check(native.encode(rendered,
            (message['destination'] as String).toNativeUtf8(allocator: arena),
            message['format'] as int, message['quality'] as int,
            message['metadata'] == true ? 1 : 0)));
          reply.send({'id': id, 'destination': message['destination']});
        } else {
          final info = native.imageInfo(rendered);
          final length = (info['width'] as int) * (info['height'] as int) * 4;
          final original = native.imageInfo(input);
          reply.send({'id': id, 'info': original, 'width': info['width'], 'height': info['height'],
            'capabilities': native.capabilities,
            'pixels': TransferableTypedData.fromList([native.rgba(rendered, length)])});
        }
      } finally {
        native.freeImage(rendered);
        if (exporting) native.freeImage(input);
      }
    } catch (error) { reply.send({'id': id, 'error': error.toString()}); }
  });
}

Pointer<NativeImage> _render(Bindings native, Pointer<NativeImage> input, EditRecipe recipe) {
  Pointer<NativeImage> current = nullptr;
  try {
    using((arena) {
      final params = arena<NativeParams>(); final p = params.ref; final v = recipe.values;
      p.exposure = v[0]; p.contrast = v[1]; p.highlights = v[2]; p.shadows = v[3];
      p.whites = v[4]; p.blacks = v[5]; p.temperature = v[6]; p.tint = v[7];
      p.saturation = v[8]; p.vibrance = v[9];
      final out = arena<Pointer<NativeImage>>(); native.check(native.params(input, params, out)); current = out.value;
    });
    if (recipe.lutPath != null) {
      using((arena) {
        final lut = arena<Pointer<NativeLut>>();
        native.check(native.loadLut(recipe.lutPath!.toNativeUtf8(allocator: arena), lut));
        try {
          final out = arena<Pointer<NativeImage>>(); native.check(native.lut(current, lut.value, recipe.strength, out));
          native.freeImage(current); current = out.value;
        } finally { native.freeLut(lut.value); }
      });
    }
    if (recipe.borderWidth > 0 || recipe.borderBottom > 0) {
      using((arena) {
        final border = arena<NativeBorder>(); final b = border.ref;
        b.width = recipe.borderWidth; b.bottom = recipe.borderBottom; b.color = recipe.borderColor;
        writeText(b.text, 512, recipe.caption); writeText(b.fontPath, 1024, recipe.fontPath);
        final out = arena<Pointer<NativeImage>>(); native.check(native.border(current, border, out));
        native.freeImage(current); current = out.value;
      });
    }
    return current;
  } catch (_) { native.freeImage(current); rethrow; }
}
