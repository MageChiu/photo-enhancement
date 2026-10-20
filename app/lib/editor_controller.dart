import 'dart:async';
import 'dart:io';
import 'dart:isolate';
import 'dart:ui' as ui;
import 'package:flutter/foundation.dart';
import 'models/edit.dart';
import 'native/worker.dart';
import 'services/presets.dart';

class EditorController extends ChangeNotifier {
  EditorController({ImageWorker? previewWorker, ImageWorker? exportWorker})
    : _previewWorker = previewWorker ?? ImageWorker(),
      _exportWorker = exportWorker ?? ImageWorker();
  final ImageWorker _previewWorker, _exportWorker;
  final history = EditHistory();
  final presetStore = PresetStore();
  EditRecipe get recipe => history.current;
  String? sourcePath, error, notice;
  Map<String, dynamic> metadata = {};
  String capabilities = '';
  ui.Image? preview;
  bool processing = false, exporting = false, initialized = false;
  bool _disposed = false, _dirty = false, _pumping = false;
  int _revision = 0;
  Timer? _debounce;
  EditRecipe? _gestureStart;
  final List<String> recentFiles = [];

  Future<void> initialize() async {
    try {
      await presetStore.initialize();
      final candidates = Platform.isMacOS ? <String>[
        '/System/Library/Fonts/Supplemental/Arial.ttf', '/System/Library/Fonts/Helvetica.ttc',
      ] : Platform.isAndroid ? <String>['/system/fonts/Roboto-Regular.ttf'] : <String>[];
      for (final path in candidates) {
        if (await File(path).exists()) {
          if (sourcePath == null) { history.reset(recipe.copyWith(fontPath: path)); }
          else if (recipe.fontPath.isEmpty) { history.current = recipe.copyWith(fontPath: path); }
          break;
        }
      }
      initialized = true;
    } catch (e) { error = '初始化失败：$e'; }
    _notify();
  }

  void open(String path) {
    sourcePath = path; metadata = {}; error = null; notice = null;
    history.reset(EditRecipe(fontPath: recipe.fontPath));
    recentFiles.remove(path); recentFiles.insert(0, path);
    if (recentFiles.length > 12) recentFiles.removeLast();
    preview?.dispose(); preview = null;
    _queue(immediate: true);
  }
  void beginGesture() { _gestureStart = recipe; }
  void endGesture() {
    final before = _gestureStart; _gestureStart = null;
    if (before != null) history.commit(recipe, previous: before);
    _notify();
  }
  void change(EditRecipe next, {bool transient = false}) {
    if (transient) { history.current = next; } else { history.commit(next); }
    _queue();
  }
  void undo() { _gestureStart = null; history.undo(); _queue(immediate: true); }
  void redo() { _gestureStart = null; history.redo(); _queue(immediate: true); }
  void reset() { change(EditRecipe(fontPath: recipe.fontPath)); }
  void applyPreset(PhotoPreset preset) {
    change(preset.recipe.copyWith(fontPath: recipe.fontPath));
  }
  Future<void> importLut(String path) async {
    await _previewWorker.call('validateLut', {'lutPath': path});
    // Own a local copy, so deleting/moving the imported file doesn't break edits.
    final owned = File('${presetStore.directory.path}/lut-${DateTime.now().microsecondsSinceEpoch}.cube');
    await File(path).copy(owned.path);
    change(recipe.copyWith(lutPath: owned.path, lutName: path.split(Platform.pathSeparator).last));
  }
  Future<void> importPreset(String path) async {
    final preset = await presetStore.read(path);
    if (preset.recipe.lutPath != null) {
      await _previewWorker.call('validateLut', {'lutPath': preset.recipe.lutPath});
    }
    await presetStore.save(preset.name, preset.recipe); applyPreset(preset);
  }
  Future<void> savePreset(String name) async {
    await presetStore.save(name, recipe); notice = '已保存预设：$name'; _notify();
  }

  void _queue({bool immediate = false}) {
    ++_revision; _dirty = true; error = null;
    _debounce?.cancel();
    if (sourcePath != null) _debounce = Timer(Duration(milliseconds: immediate ? 0 : 70), _pump);
    _notify();
  }
  Future<void> _pump() async {
    if (_disposed || _pumping || !_dirty || sourcePath == null) return;
    _pumping = true; _dirty = false; processing = true;
    final revision = _revision;
    final path = sourcePath!;
    final snapshot = recipe;
    _notify();
    try {
      final result = await _previewWorker.call('preview', {'path': path, 'recipe': snapshot.toJson()});
      if (_disposed || revision != _revision) return;
      final bytes = (result['pixels'] as TransferableTypedData).materialize().asUint8List();
      // Flutter's raw RGBA upload expects premultiplied display pixels.
      for (var i = 0; i < bytes.length; i += 4) {
        final alpha = bytes[i + 3];
        if (alpha < 255) { for (var c = 0; c < 3; ++c) { bytes[i + c] = (bytes[i + c] * alpha + 127) ~/ 255; } }
      }
      final buffer = await ui.ImmutableBuffer.fromUint8List(bytes);
      try {
        final descriptor = ui.ImageDescriptor.raw(buffer,
          width: result['width'] as int, height: result['height'] as int,
          pixelFormat: ui.PixelFormat.rgba8888);
        try {
          final codec = await descriptor.instantiateCodec();
          try {
            final frame = await codec.getNextFrame();
            if (_disposed || revision != _revision) { frame.image.dispose(); }
            else {
              final old = preview; preview = frame.image; old?.dispose();
              metadata = Map<String, dynamic>.from(result['info'] as Map);
              capabilities = result['capabilities'] as String;
            }
          } finally { codec.dispose(); }
        } finally { descriptor.dispose(); }
      } finally { buffer.dispose(); }
    } catch (e) { if (!_disposed && revision == _revision) error = e.toString(); }
    finally {
      _pumping = false; processing = false; _notify();
      if (_dirty && !_disposed) unawaited(_pump());
    }
  }

  Future<void> export(String destination, {int format = 1, int quality = 95, bool includeMetadata = true}) async {
    if (sourcePath == null || exporting) return;
    final path = sourcePath!, snapshot = recipe;
    exporting = true; error = null; notice = null; _notify();
    try {
      await _exportWorker.call('export', {'path': path, 'recipe': snapshot.toJson(),
        'destination': destination, 'format': format, 'quality': quality, 'metadata': includeMetadata});
      notice = '已导出：$destination';
    } catch (e) { error = '导出失败（不会覆盖已有文件）：$e'; }
    finally { exporting = false; _notify(); }
  }
  void report(Object e) { error = e.toString(); _notify(); }
  void _notify() { if (!_disposed) notifyListeners(); }
  @override
  void dispose() {
    _disposed = true; _debounce?.cancel(); ++_revision;
    _previewWorker.dispose(); _exportWorker.dispose(); preview?.dispose();
    super.dispose();
  }
}
