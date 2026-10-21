import 'dart:async';
import 'dart:io';
import 'dart:isolate';
import 'dart:ui' as ui;
import 'package:flutter/foundation.dart';
import 'models/edit.dart';
import 'native/worker.dart';
import 'services/presets.dart';
import 'services/frame_resources.dart';

class ExportException implements Exception {
  const ExportException(this.message);
  final String message;
  @override
  String toString() => message;
}

class EditorController extends ChangeNotifier {
  EditorController({ImageWorker? previewWorker, ImageWorker? exportWorker})
      : _previewWorker = previewWorker ?? ImageWorker(),
        _exportWorker = exportWorker ?? ImageWorker();
  final ImageWorker _previewWorker, _exportWorker;
  final history = EditHistory();
  final presetStore = PresetStore();
  final frameResources = FrameResources();
  Future<void>? _initializing;
  EditRecipe get recipe => history.current;
  String? sourcePath, error, notice;
  Map<String, dynamic> metadata = {};
  String capabilities = '';
  Map<String, dynamic> lookAnalysis = {};
  ui.Image? preview, originalPreview;
  bool compareOriginal = false;
  final List<ui.Image> filterThumbnails = [];
  String? _thumbnailPath;
  int _thumbnailRevision = 0;
  bool _thumbnailsPending = false;
  bool processing = false, exporting = false, initialized = false;
  bool _disposed = false, _dirty = false, _pumping = false;
  int _revision = 0;
  Timer? _debounce;
  EditRecipe? _gestureStart;
  final List<String> recentFiles = [];

  Future<void> initialize() => _initializing ??= _initialize();

  Future<void> _initialize() async {
    try {
      await presetStore.initialize();
      await frameResources.initialize(presetStore.directory);
      initialized = true;
      if (sourcePath != null && preview != null) {
        unawaited(_loadThumbnails(sourcePath!));
      }
    } catch (e) {
      error = '初始化失败：$e';
    }
    _notify();
  }

  void open(String path) {
    sourcePath = path;
    metadata = {};
    lookAnalysis = {};
    error = null;
    notice = null;
    history.reset(EditRecipe(fontPath: recipe.fontPath));
    recentFiles.remove(path);
    recentFiles.insert(0, path);
    if (recentFiles.length > 12) recentFiles.removeLast();
    preview?.dispose();
    originalPreview?.dispose();
    preview = null;
    originalPreview = null;
    compareOriginal = false;
    _thumbnailPath = null;
    ++_thumbnailRevision;
    for (final image in filterThumbnails) {
      image.dispose();
    }
    filterThumbnails.clear();
    _queue(immediate: true);
  }

  void beginGesture() {
    _gestureStart = recipe;
  }

  void endGesture() {
    final before = _gestureStart;
    _gestureStart = null;
    if (before != null) history.commit(recipe, previous: before);
    _notify();
  }

  void change(EditRecipe next, {bool transient = false}) {
    if (transient) {
      history.current = next;
    } else {
      history.commit(next);
    }
    _queue();
  }

  void undo() {
    _gestureStart = null;
    history.undo();
    _queue(immediate: true);
  }

  void redo() {
    _gestureStart = null;
    history.redo();
    _queue(immediate: true);
  }

  void reset() {
    change(EditRecipe(fontPath: recipe.fontPath));
  }

  void applyPreset(PhotoPreset preset) {
    if (preset.frameOnly) {
      change(
          recipe.withFrame(preset.recipe.copyWith(fontPath: recipe.fontPath)));
    } else if (preset.colorOnly) {
      change(recipe.copyWith(
          lutPath: preset.recipe.lutPath,
          lutName: preset.name,
          strength: preset.recipe.strength,
          lookKind: preset.recipe.lookKind,
          lookAdaptive: preset.recipe.lookAdaptive,
          lookAdaptation: preset.recipe.lookAdaptation,
          lookTexture: preset.recipe.lookTexture,
          lookVignette: preset.recipe.lookVignette,
          lookGrain: preset.recipe.lookGrain));
    } else {
      change(preset.recipe.copyWith(fontPath: recipe.fontPath));
    }
  }

  Future<void> importLut(String path) async {
    await _previewWorker.call('validateLut', {'lutPath': path});
    // Own a local copy, so deleting/moving the imported file doesn't break edits.
    final owned = File(
        '${presetStore.directory.path}/lut-${DateTime.now().microsecondsSinceEpoch}.cube');
    await File(path).copy(owned.path);
    change(recipe.copyWith(
        lutPath: owned.path,
        lookKind: 0,
        lutName: path.split(Platform.pathSeparator).last));
  }

  Future<void> importPreset(String path) async {
    final preset = await presetStore.read(path);
    if (preset.recipe.lutPath != null) {
      await _previewWorker
          .call('validateLut', {'lutPath': preset.recipe.lutPath});
    }
    await presetStore.save(preset.name, preset.recipe,
        frameOnly: preset.frameOnly);
    applyPreset(preset);
  }

  Future<void> savePreset(String name, {bool frameOnly = false}) async {
    await presetStore.save(name, recipe, frameOnly: frameOnly);
    notice = '已保存预设：$name';
    _notify();
  }

  void _queue({bool immediate = false}) {
    ++_revision;
    _dirty = true;
    error = null;
    _debounce?.cancel();
    if (sourcePath != null) {
      _debounce = Timer(Duration(milliseconds: immediate ? 0 : 70), _pump);
    }
    _notify();
  }

  Future<void> _pump() async {
    if (_disposed || _pumping || !_dirty || sourcePath == null) return;
    _pumping = true;
    _dirty = false;
    processing = true;
    final revision = _revision;
    final path = sourcePath!;
    final snapshot = recipe;
    _notify();
    try {
      await initialize();
      final result = await _previewWorker.call('preview', {
        'path': path,
        'recipe': snapshot.toJson(),
        'frameAssets': frameResources.paths
      });
      if (_disposed || revision != _revision) return;
      final edited = await _upload(result);
      late final ui.Image original;
      try {
        original = await _upload(result, 'originalPixels');
      } catch (_) {
        edited.dispose();
        rethrow;
      }
      if (_disposed || revision != _revision) {
        edited.dispose();
        original.dispose();
      } else {
        preview?.dispose();
        originalPreview?.dispose();
        preview = edited;
        originalPreview = original;
        metadata = Map<String, dynamic>.from(result['info'] as Map);
        capabilities = result['capabilities'] as String;
        lookAnalysis =
            Map<String, dynamic>.from(result['lookAnalysis'] as Map? ?? {});
      }
    } catch (e) {
      if (!_disposed && revision == _revision) error = e.toString();
    } finally {
      _pumping = false;
      processing = false;
      _notify();
      if (_dirty && !_disposed) {
        unawaited(_pump());
      } else if (!_disposed &&
          initialized &&
          _thumbnailPath != path &&
          sourcePath == path &&
          error == null) {
        unawaited(_loadThumbnails(path));
      }
    }
  }

  static Future<ui.Image> _upload(Map<String, dynamic> result,
      [String key = 'pixels']) async {
    final bytes =
        (result[key] as TransferableTypedData).materialize().asUint8List();
    for (var i = 0; i < bytes.length; i += 4) {
      final alpha = bytes[i + 3];
      if (alpha < 255) {
        for (var c = 0; c < 3; ++c) {
          bytes[i + c] = (bytes[i + c] * alpha + 127) ~/ 255;
        }
      }
    }
    final buffer = await ui.ImmutableBuffer.fromUint8List(bytes);
    try {
      final descriptor = ui.ImageDescriptor.raw(buffer,
          width: result['width'] as int,
          height: result['height'] as int,
          pixelFormat: ui.PixelFormat.rgba8888);
      try {
        final codec = await descriptor.instantiateCodec();
        try {
          return (await codec.getNextFrame()).image;
        } finally {
          codec.dispose();
        }
      } finally {
        descriptor.dispose();
      }
    } finally {
      buffer.dispose();
    }
  }

  Future<void> _loadThumbnails(String path) async {
    if (_thumbnailsPending || _disposed) return;
    _thumbnailsPending = true;
    final revision = _thumbnailRevision;
    final images = <ui.Image>[];
    try {
      final presets = presetStore.presets.where((p) => p.colorOnly).toList();
      final result = await _previewWorker.call('thumbnails', {
        'path': path,
        'recipe': EditRecipe().toJson(),
        'presets': presets.map((p) => p.recipe.toJson()).toList(),
      });
      for (final dynamic item in result['thumbnails'] as List) {
        images.add(await _upload(Map<String, dynamic>.from(item as Map)));
      }
      if (_disposed || revision != _thumbnailRevision || sourcePath != path) {
        for (final image in images) {
          image.dispose();
        }
        images.clear();
      } else {
        for (final image in filterThumbnails) {
          image.dispose();
        }
        filterThumbnails
          ..clear()
          ..addAll(images);
        images.clear();
        _thumbnailPath = path;
        _notify();
      }
    } catch (_) {
      for (final image in images) {
        image.dispose();
      }
      // Thumbnail failure must not disable the main editor.
    } finally {
      _thumbnailsPending = false;
      if (!_disposed &&
          sourcePath != null &&
          sourcePath != path &&
          preview != null) {
        unawaited(_loadThumbnails(sourcePath!));
      }
    }
  }

  void toggleOriginal() {
    if (originalPreview == null) return;
    compareOriginal = !compareOriginal;
    _notify();
  }

  Future<void> importLogo(String path) async {
    final file = File(path);
    if (await file.length() > 4 * 1024 * 1024) {
      throw const FormatException('标识不能超过 4MB');
    }
    final ownedPath = await presetStore.cacheLogo(await file.readAsBytes());
    change(recipe.copyWith(
        logoPath: ownedPath,
        frameBrand: 0,
        frameMark: '',
        frameAccent: '',
        borderBottom: recipe.borderBottom > 0 ? recipe.borderBottom : .16));
  }

  Future<void> export(String destination,
      {int format = 1, int quality = 95, bool includeMetadata = true}) async {
    if (sourcePath == null || exporting) return;
    final path = sourcePath!, snapshot = recipe;
    final sourceMake = metadata['make'] as String? ?? '';
    var model = metadata['model'] as String? ?? '';
    var lensMake = metadata['lensMake'] as String? ?? '';
    var lensModel = metadata['lensModel'] as String? ?? '';
    exporting = true;
    error = null;
    notice = null;
    _notify();
    try {
      await initialize();
      // Inspect through the preview worker even when export is requested before
      // the first preview has completed. Never infer a brand from the filename.
      var make = sourceMake;
      if ((snapshot.frameMark == 'auto' && make.isEmpty) ||
          (snapshot.frameAccent == 'auto' &&
              lensMake.isEmpty &&
              lensModel.isEmpty)) {
        final info = await _previewWorker
            .call('inspect', {'path': path, 'recipe': snapshot.toJson()});
        make = (info['info'] as Map)['make'] as String? ?? '';
        model = (info['info'] as Map)['model'] as String? ?? '';
        lensMake = (info['info'] as Map)['lensMake'] as String? ?? '';
        lensModel = (info['info'] as Map)['lensModel'] as String? ?? '';
      }
      await _exportWorker.call('export', {
        'path': path,
        'recipe': snapshot.toJson(),
        'destination': destination,
        'format': format,
        'quality': quality,
        'metadata': includeMetadata,
        'make': make,
        'model': model,
        'lensMake': lensMake,
        'lensModel': lensModel,
        'frameAssets': frameResources.paths
      });
      notice = '已导出：$destination';
    } catch (e) {
      error = '导出失败（不会覆盖已有文件）：$e';
    } finally {
      exporting = false;
      _notify();
    }
  }

  void report(Object e) {
    error = e.toString();
    _notify();
  }

  void exportFinished(bool saved) {
    notice = saved ? '已导出到所选位置' : null;
    _notify();
  }

  void _notify() {
    if (!_disposed) notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    _debounce?.cancel();
    ++_revision;
    _previewWorker.dispose();
    _exportWorker.dispose();
    preview?.dispose();
    originalPreview?.dispose();
    ++_thumbnailRevision;
    for (final image in filterThumbnails) {
      image.dispose();
    }
    super.dispose();
  }
}
