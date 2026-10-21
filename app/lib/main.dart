import 'dart:async';
import 'dart:io';
import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';
import 'package:flutter/foundation.dart' show listEquals;
import 'package:flutter/services.dart';
import 'editor_controller.dart';
import 'models/edit.dart';
import 'models/frame_marks.dart';
import 'services/platform_files.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const PhotoApp());
}

class PhotoApp extends StatelessWidget {
  const PhotoApp({super.key});
  @override
  Widget build(BuildContext context) => MaterialApp(
        title: '光屿 · Photo Enhancement',
        debugShowCheckedModeBanner: false,
        theme: ThemeData(
          brightness: Brightness.dark, useMaterial3: true,
          colorScheme: ColorScheme.fromSeed(
              seedColor: const Color(0xffe5c185),
              brightness: Brightness.dark,
              primary: const Color(0xffe5c185),
              surface: const Color(0xff191c20)),
          scaffoldBackgroundColor: const Color(0xff111316),
          sliderTheme: const SliderThemeData(
              trackHeight: 2,
              thumbShape: RoundSliderThumbShape(enabledThumbRadius: 5)),
          inputDecorationTheme: const InputDecorationTheme(
              border: OutlineInputBorder(), isDense: true),
          // Infer the SDK's card theme data type instead of constructing CardTheme,
          // which became an inherited theme widget in newer Flutter versions.
          cardTheme: ThemeData.dark().cardTheme.copyWith(
                color: const Color(0xff20242a),
                elevation: 0,
              ),
        ),
        home: const EditorPage(),
      );
}

class EditorPage extends StatefulWidget {
  const EditorPage({super.key});
  @override
  State<EditorPage> createState() => _EditorPageState();
}

class _EditorPageState extends State<EditorPage> {
  final editor = EditorController();
  int tab = 0, hslChannel = 0;
  bool _holdingOriginal = false;
  static const imageTypes = XTypeGroup(label: '照片与 RAW', extensions: [
    'jpg',
    'jpeg',
    'png',
    'heic',
    'heif',
    'webp',
    'arw',
    'nef',
    'nrw',
    'cr2',
    'cr3',
    'rw2',
    'rwl',
    'raf',
    'orf',
    'dng',
    'pef',
    'x3f',
    '3fr',
    'fff',
    'gpr',
    'iiq',
    'kdc',
    'dcr',
    'srw',
    'raw',
    'tif',
    'tiff',
  ]);
  @override
  void initState() {
    super.initState();
    unawaited(editor.initialize());
  }

  @override
  void dispose() {
    editor.dispose();
    super.dispose();
  }

  Future<void> _guard(Future<void> Function() action) async {
    try {
      await action();
    } catch (e) {
      if (mounted) editor.report(e);
    }
  }

  Future<void> _open() => _guard(() async {
        final files = await PlatformFiles.open(
            extensions: imageTypes.extensions!, multiple: true);
        if (files.isEmpty || !mounted) return;
        for (final file in files.skip(1)) {
          editor.recentFiles.remove(file);
          editor.recentFiles.add(file);
        }
        editor.open(files.first);
      });
  Future<void> _importLut() => _guard(() async {
        final files = await PlatformFiles.open(extensions: ['cube']);
        if (files.isNotEmpty && mounted) await editor.importLut(files.first);
      });
  Future<void> _importPreset() => _guard(() async {
        final files = await PlatformFiles.open(extensions: ['json']);
        if (files.isNotEmpty && mounted) await editor.importPreset(files.first);
      });
  Future<void> _font() => _guard(() async {
        final files =
            await PlatformFiles.open(extensions: ['ttf', 'otf', 'ttc']);
        if (files.isNotEmpty && mounted) {
          editor.change(editor.recipe
              .copyWith(fontPath: files.first, frameFont: 'custom'));
        }
      });
  Future<void> _logo() => _guard(() async {
        final files = await PlatformFiles.open(extensions: ['png']);
        if (files.isNotEmpty && mounted) await editor.importLogo(files.first);
      });
  Future<String?> _nameDialog(String title, String initial) async {
    final input = TextEditingController(text: initial);
    try {
      return await showDialog<String>(
          context: context,
          builder: (context) => AlertDialog(
                title: Text(title),
                content: SizedBox(
                    width: 360,
                    child: TextField(
                        controller: input,
                        autofocus: true,
                        maxLength: 100,
                        onSubmitted: (value) => Navigator.pop(context, value))),
                actions: [
                  TextButton(
                      onPressed: () => Navigator.pop(context),
                      child: const Text('取消')),
                  FilledButton(
                      onPressed: () => Navigator.pop(context, input.text),
                      child: const Text('保存'))
                ],
              ));
    } finally {
      input.dispose();
    }
  }

  Future<void> _savePreset() => _guard(() async {
        final name = await _nameDialog('保存为本地预设', '我的纪实风格');
        if (name != null && mounted) await editor.savePreset(name);
      });
  Future<void> _saveFrame() => _guard(() async {
        final name = await _nameDialog('保存边框预设', '我的相框');
        if (name != null && mounted) {
          await editor.savePreset(name, frameOnly: true);
        }
      });
  Future<void> _exportFrame() => _guard(() async {
        final name = await _nameDialog('导出边框预设（内含图标）', '我的相框');
        if (name == null || !mounted) return;
        final content = await editor.presetStore
            .serialize(name, editor.recipe, frameOnly: true);
        await PlatformFiles.save('photo-frame.preset.json', ['json'],
            (path) async {
          await File(path).writeAsString(content, flush: true);
        });
      });
  Future<void> _exportPreset() => _guard(() async {
        final name = await _nameDialog('导出便携预设（内含 LUT）', '我的纪实风格');
        if (name == null || !mounted) return;
        final content = await editor.presetStore.serialize(name, editor.recipe);
        await PlatformFiles.save('photo-style.preset.json', ['json'],
            (path) async {
          await File(path).writeAsString(content, flush: true);
        });
      });

  Future<void> _export() => _guard(() async {
        if (editor.sourcePath == null || editor.exporting) return;
        var format = 1, quality = 95, metadata = true;
        final accepted = await showDialog<bool>(
            context: context,
            builder: (context) => StatefulBuilder(
                  builder: (context, update) => AlertDialog(
                    title: const Text('导出原始分辨率'),
                    content: SizedBox(
                        width: 380,
                        child:
                            Column(mainAxisSize: MainAxisSize.min, children: [
                          Text(
                              PlatformFiles.isMobile
                                  ? '将从原图副本重新处理，由系统确认保存位置。'
                                  : '将从原图重新处理。不会覆盖原文件或任何已有文件。',
                              style: TextStyle(color: Colors.white60)),
                          const SizedBox(height: 20),
                          InputDecorator(
                              decoration:
                                  const InputDecoration(labelText: '格式'),
                              child: DropdownButtonHideUnderline(
                                  child: DropdownButton<int>(
                                      value: format,
                                      isExpanded: true,
                                      items: [
                                        const DropdownMenuItem(
                                            value: 1,
                                            child: Text('JPEG · 兼容性优先')),
                                        const DropdownMenuItem(
                                            value: 2,
                                            child: Text('PNG · 16 位无损')),
                                        if (editor.capabilities
                                            .contains('heif-encode'))
                                          const DropdownMenuItem(
                                              value: 3,
                                              child: Text('HEIF · 需要可用编码器'))
                                      ],
                                      onChanged: (value) =>
                                          update(() => format = value!)))),
                          const SizedBox(height: 16),
                          if (format != 2) ...[
                            Text('质量 $quality'),
                            Slider(
                                value: quality.toDouble(),
                                min: 1,
                                max: 100,
                                divisions: 99,
                                onChanged: (value) =>
                                    update(() => quality = value.round()))
                          ],
                          SwitchListTile(
                              contentPadding: EdgeInsets.zero,
                              title: const Text('保留拍摄信息'),
                              subtitle: const Text('仅机型、镜头参数与时间，不含 GPS'),
                              value: metadata,
                              onChanged: (value) =>
                                  update(() => metadata = value)),
                        ])),
                    actions: [
                      TextButton(
                          onPressed: () => Navigator.pop(context, false),
                          child: const Text('取消')),
                      FilledButton(
                          onPressed: () => Navigator.pop(context, true),
                          child: const Text('选择保存位置'))
                    ],
                  ),
                ));
        if (accepted != true || !mounted) return;
        final extension = format == 1
            ? 'jpg'
            : format == 2
                ? 'png'
                : 'heic';
        final name = editor.sourcePath!
            .split(Platform.pathSeparator)
            .last
            .replaceFirst(RegExp(r'\.[^.]+$'), '');
        final saved = await PlatformFiles.save(
            '$name-edited.$extension', [extension], (path) async {
          await editor.export(path,
              format: format, quality: quality, includeMetadata: metadata);
          if (editor.error != null) throw ExportException(editor.error!);
        });
        if (PlatformFiles.isMobile) editor.exportFinished(saved);
      });

  @override
  Widget build(BuildContext context) => CallbackShortcuts(
        bindings: {
          SingleActivator(LogicalKeyboardKey.keyO,
              meta: Platform.isMacOS, control: !Platform.isMacOS): _open,
          SingleActivator(LogicalKeyboardKey.keyE,
              meta: Platform.isMacOS, control: !Platform.isMacOS): _export,
          SingleActivator(LogicalKeyboardKey.keyZ,
              meta: Platform.isMacOS, control: !Platform.isMacOS): editor.undo,
          SingleActivator(LogicalKeyboardKey.keyZ,
              meta: Platform.isMacOS,
              control: !Platform.isMacOS,
              shift: true): editor.redo,
        },
        child: Focus(
            autofocus: true,
            child: AnimatedBuilder(
                animation: editor,
                builder: (context, _) => Scaffold(
                      body: SafeArea(
                          child: Column(children: [
                        _toolbar(),
                        if (editor.error != null || editor.notice != null)
                          _message(),
                        Expanded(child:
                            LayoutBuilder(builder: (context, constraints) {
                          final controls = _controls();
                          if (constraints.maxWidth < 760) {
                            return Column(children: [
                              Expanded(child: _canvas()),
                              SizedBox(height: 300, child: controls)
                            ]);
                          }
                          return Row(children: [
                            if (constraints.maxWidth > 1130) _library(),
                            Expanded(child: _canvas()),
                            SizedBox(width: 320, child: controls)
                          ]);
                        })),
                        _status(),
                      ])),
                    ))),
      );

  Widget _toolbar() => Container(
        height: 76,
        padding: EdgeInsets.symmetric(
            horizontal: MediaQuery.sizeOf(context).width < 540 ? 12 : 24),
        decoration: const BoxDecoration(
            border: Border(bottom: BorderSide(color: Colors.white10))),
        child: Row(children: [
          const Icon(Icons.filter_frames_rounded,
              color: Color(0xffe5c185), size: 30),
          const SizedBox(width: 12),
          const Text('光屿',
              style: TextStyle(
                  fontSize: 23, fontWeight: FontWeight.w600, letterSpacing: 4)),
          if (MediaQuery.sizeOf(context).width > 1000) ...[
            const SizedBox(width: 14),
            const Text('PHOTO\nENHANCEMENT',
                style: TextStyle(
                    fontSize: 9, letterSpacing: 2, color: Colors.white38)),
            const SizedBox(width: 24),
            _pill('纯本地 · 离线处理', Icons.shield_outlined),
          ],
          const Spacer(),
          if (MediaQuery.sizeOf(context).width > 540) ...[
            IconButton(
                tooltip: '撤销',
                onPressed: editor.history.canUndo ? editor.undo : null,
                icon: const Icon(Icons.undo, size: 20)),
            IconButton(
                tooltip: '重做',
                onPressed: editor.history.canRedo ? editor.redo : null,
                icon: const Icon(Icons.redo, size: 20)),
          ],
          const SizedBox(width: 10),
          OutlinedButton.icon(
              onPressed: _open,
              icon: const Icon(Icons.add_photo_alternate_outlined, size: 18),
              label: const Text('导入')),
          const SizedBox(width: 10),
          FilledButton.icon(
              onPressed: editor.sourcePath != null && !editor.exporting
                  ? _export
                  : null,
              icon: editor.exporting
                  ? const SizedBox(
                      width: 16,
                      height: 16,
                      child: CircularProgressIndicator(strokeWidth: 2))
                  : const Icon(Icons.file_download_outlined, size: 18),
              label: Text(editor.exporting ? '导出中' : '导出')),
        ]),
      );
  Widget _pill(String text, IconData icon) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
        decoration: BoxDecoration(
            color: Colors.white.withValues(alpha: .04),
            borderRadius: BorderRadius.circular(20)),
        child: Row(mainAxisSize: MainAxisSize.min, children: [
          Icon(icon, size: 12, color: Colors.white54),
          const SizedBox(width: 5),
          Text(text,
              style: const TextStyle(fontSize: 10, color: Colors.white54))
        ]),
      );
  Widget _message() => Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 10),
      color: editor.error != null
          ? const Color(0xff482725)
          : const Color(0xff20392c),
      child: SelectableText(editor.error ?? editor.notice!,
          style: const TextStyle(fontSize: 12)));

  Widget _library() => Container(
      width: 200,
      color: const Color(0xff17191d),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        const Padding(
            padding: EdgeInsets.all(20),
            child: Text('本地图库',
                style: TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w600,
                    letterSpacing: 2))),
        Padding(
            padding: const EdgeInsets.symmetric(horizontal: 14),
            child: OutlinedButton.icon(
                onPressed: _open,
                icon: const Icon(Icons.add, size: 16),
                label: const Text('添加照片'))),
        const Padding(
            padding: EdgeInsets.fromLTRB(20, 24, 20, 10),
            child: Text('本次打开',
                style: TextStyle(fontSize: 10, color: Colors.white38))),
        Expanded(
            child: ListView.builder(
                itemCount: editor.recentFiles.length,
                itemBuilder: (context, index) {
                  final path = editor.recentFiles[index];
                  final selected = path == editor.sourcePath;
                  return ListTile(
                      dense: true,
                      selected: selected,
                      selectedTileColor: const Color(0xff2b2924),
                      leading: Icon(Icons.image_outlined,
                          size: 18,
                          color: selected
                              ? const Color(0xffe5c185)
                              : Colors.white38),
                      title: Text(path.split(Platform.pathSeparator).last,
                          maxLines: 2,
                          overflow: TextOverflow.ellipsis,
                          style: const TextStyle(fontSize: 11)),
                      onTap: () => editor.open(path));
                })),
        const Padding(
            padding: EdgeInsets.all(20),
            child: Text('原图始终保留\n所有编辑均以参数记录',
                style: TextStyle(
                    fontSize: 10, height: 1.8, color: Colors.white30))),
      ]));

  Widget _canvas() => Column(children: [
        SizedBox(
            height: 48,
            child: Row(children: [
              const SizedBox(width: 24),
              Expanded(
                  child: Text(
                      editor.sourcePath?.split(Platform.pathSeparator).last ??
                          '你的下一张好照片，从这里开始',
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                          fontSize: 11, color: Colors.white54))),
              if (editor.preview != null) ...[
                TextButton.icon(
                    onPressed: editor.originalPreview != null
                        ? editor.toggleOriginal
                        : null,
                    icon: Icon(
                        editor.compareOriginal
                            ? Icons.auto_fix_high
                            : Icons.compare,
                        size: 17),
                    label: Text(editor.compareOriginal ? '看效果' : '看原片')),
                Tooltip(
                  message: '按住查看原片；松开恢复。对比保留边框与缩放位置。',
                  child: Listener(
                    onPointerDown: editor.originalPreview == null
                        ? null
                        : (_) => setState(() => _holdingOriginal = true),
                    onPointerUp: (_) =>
                        setState(() => _holdingOriginal = false),
                    onPointerCancel: (_) =>
                        setState(() => _holdingOriginal = false),
                    child: IconButton(
                        onPressed:
                            editor.originalPreview == null ? null : () {},
                        icon: const Icon(Icons.visibility_outlined, size: 18)),
                  ),
                ),
                if (MediaQuery.sizeOf(context).width > 1000)
                  _pill('可缩放', Icons.zoom_in),
              ],
              const SizedBox(width: 24),
            ])),
        Expanded(
            child: Padding(
                padding: const EdgeInsets.fromLTRB(28, 8, 28, 22),
                child: Stack(children: [
                  Positioned.fill(
                      child: editor.preview == null
                          ? _empty()
                          : InteractiveViewer(
                              key: ValueKey(editor.sourcePath),
                              minScale: .5,
                              maxScale: 6,
                              child: Center(
                                  child: RawImage(
                                      image: (editor.compareOriginal ||
                                                  _holdingOriginal) &&
                                              editor.originalPreview != null
                                          ? editor.originalPreview
                                          : editor.preview,
                                      fit: BoxFit.contain,
                                      filterQuality: FilterQuality.high)))),
                  if (editor.preview != null &&
                      (editor.compareOriginal || _holdingOriginal))
                    Positioned(
                        left: 12,
                        top: 12,
                        child: _pill('原片 · 保留相框', Icons.image_outlined)),
                  if (editor.processing)
                    const Positioned(
                        right: 12,
                        bottom: 12,
                        child: SizedBox(
                            width: 20,
                            height: 20,
                            child: CircularProgressIndicator(
                                strokeWidth: 2, color: Color(0xffe5c185)))),
                ]))),
        if (editor.metadata.isNotEmpty)
          Container(padding: const EdgeInsets.only(bottom: 20), child: _exif()),
      ]);
  Widget _empty() => Center(
          child: SingleChildScrollView(
              child: Column(mainAxisSize: MainAxisSize.min, children: [
        Container(
            width: 100,
            height: 100,
            decoration: BoxDecoration(
                color: const Color(0xff202329),
                borderRadius: BorderRadius.circular(28)),
            child: const Icon(Icons.landscape_outlined,
                size: 45, color: Color(0xffb3a18a))),
        const SizedBox(height: 26),
        const Text('还原现场，也留下你的表达',
            style: TextStyle(fontSize: 21, letterSpacing: 1)),
        const SizedBox(height: 12),
        const Text('十项调色 · 纪实预设 · EXIF 相框',
            style: TextStyle(color: Colors.white38, fontSize: 12)),
        const SizedBox(height: 30),
        FilledButton.icon(
            onPressed: _open,
            icon: const Icon(Icons.add, size: 18),
            label: const Text('选择照片')),
        const SizedBox(height: 16),
        const Text('JPEG / PNG / HEIC / WebP / 相机 RAW',
            style: TextStyle(color: Colors.white24, fontSize: 10)),
      ])));
  Widget _exif() {
    final i = editor.metadata;
    final parts = <String>[
      if ((i['model'] as String? ?? '').isNotEmpty) i['model'] as String,
      if ((i['aperture'] as num? ?? 0) > 0)
        'f/${(i['aperture'] as num).toStringAsPrecision(2)}',
      if ((i['shutter'] as num? ?? 0) > 0)
        (i['shutter'] as num) < 1
            ? '1/${(1 / (i['shutter'] as num)).round()}s'
            : '${i['shutter']}s',
      if ((i['iso'] as num? ?? 0) > 0) 'ISO ${i['iso']}',
      if (i['isRaw'] == true) 'RAW',
    ];
    return Text(parts.isEmpty ? '未包含拍摄信息' : parts.join('   ·   '),
        textAlign: TextAlign.center,
        style: const TextStyle(fontSize: 10, color: Colors.white38));
  }

  Widget _controls() => Material(
      color: const Color(0xff191c20),
      child: Column(children: [
        const SizedBox(height: 16),
        SegmentedButton<int>(
            showSelectedIcon: false,
            segments: const [
              ButtonSegment(value: 0, label: Text('调色')),
              ButtonSegment(value: 1, label: Text('风格')),
              ButtonSegment(value: 2, label: Text('边框')),
              ButtonSegment(value: 3, label: Text('高级')),
            ],
            selected: {tab},
            onSelectionChanged: (value) => setState(() => tab = value.first)),
        const SizedBox(height: 10),
        Expanded(
            child: ListView(
                padding: const EdgeInsets.all(20),
                children: switch (tab) {
                  0 => _toneControls(),
                  1 => _lookControls(),
                  2 => _borderControls(),
                  _ => _advancedControls(),
                })),
      ]));
  Widget _section(String title, String subtitle) => Padding(
      padding: const EdgeInsets.only(bottom: 20),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(title,
            style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w500)),
        const SizedBox(height: 6),
        Text(subtitle,
            style: const TextStyle(
                fontSize: 10, color: Colors.white38, height: 1.6))
      ]));
  List<Widget> _toneControls() => [
        Row(children: [
          const Expanded(child: Text('基础调整', style: TextStyle(fontSize: 15))),
          TextButton(
              onPressed: editor.reset,
              child: const Text('重置', style: TextStyle(fontSize: 11)))
        ]),
        const SizedBox(height: 8),
        for (var i = 0; i < 10; ++i)
          _slider(controlLabels[i], editor.recipe.values[i],
              min: i == 0 ? -5 : -1,
              max: i == 0 ? 5 : 1,
              label: i == 0
                  ? '${editor.recipe.values[i].toStringAsFixed(2)} EV'
                  : '${(editor.recipe.values[i] * 100).round()}',
              onChanged: (value) => editor
                  .change(editor.recipe.control(i, value), transient: true)),
        const SizedBox(height: 14),
        const Text('色温 / 色调为相对调整；RAW 使用相机白平衡作为起点。',
            style: TextStyle(fontSize: 10, color: Colors.white30, height: 1.6)),
      ];
  Widget _slider(String title, double value,
          {double min = 0,
          double max = 1,
          String? label,
          required ValueChanged<double> onChanged}) =>
      Padding(
          padding: const EdgeInsets.only(bottom: 12),
          child: Column(children: [
            Row(children: [
              Text(title,
                  style: const TextStyle(fontSize: 11, color: Colors.white70)),
              const Spacer(),
              Text(label ?? '${(value * 100).round()}',
                  style: const TextStyle(fontSize: 10, color: Colors.white38))
            ]),
            SizedBox(
                height: 30,
                child: Slider(
                    value: value,
                    min: min,
                    max: max,
                    onChangeStart: (_) => editor.beginGesture(),
                    onChangeEnd: (_) => editor.endGesture(),
                    onChanged: editor.sourcePath == null ? null : onChanged)),
          ]));
  String _lookAnalysisText() {
    final stats = editor.lookAnalysis;
    if (stats.isEmpty) return '导入照片后分析当前解码画面。';
    final confidence = (stats['confidence'] as num? ?? 0).toDouble();
    if (confidence < .2) return '候选中性色不足，未作明显色温修正；仍会有限度保护亮度。';
    final temperature = (stats['temperature'] as num? ?? 0).toDouble();
    return temperature < -.02
        ? '检测到候选中性色偏暖，进行有限补偿。'
        : temperature > .02
            ? '检测到候选中性色偏冷，进行有限补偿。'
            : '候选中性色接近中性，主要调整亮度。';
  }

  List<Widget> _lookControls() => [
        _section('风格', '先试「寒峻」：低调明暗、冷灰绿、暗红与材质感。'),
        for (final group in ['冷峻纪实', '纪实光影', '基础纪实', '本地预设']) ...[
          if (editor.presetStore.presets.any((p) => p.group == group))
            Padding(
                padding: const EdgeInsets.only(bottom: 10, top: 8),
                child: Text(group,
                    style:
                        const TextStyle(fontSize: 11, color: Colors.white54))),
          for (final preset
              in editor.presetStore.presets.where((p) => p.group == group))
            Padding(
                padding: const EdgeInsets.only(bottom: 10),
                child: OutlinedButton(
                    style: OutlinedButton.styleFrom(
                        side: BorderSide(
                            color: editor.recipe.lutPath != null &&
                                    editor.recipe.lutPath ==
                                        preset.recipe.lutPath
                                ? const Color(0xffe5c185)
                                : Colors.white12),
                        alignment: Alignment.centerLeft,
                        padding: const EdgeInsets.all(12)),
                    onPressed: editor.sourcePath == null
                        ? null
                        : () => editor.applyPreset(preset),
                    child: Row(children: [
                      if (preset.colorOnly &&
                          editor.filterThumbnails.length >
                              editor.presetStore.presets.indexOf(preset)) ...[
                        ClipRRect(
                            borderRadius: BorderRadius.circular(4),
                            child: SizedBox(
                                width: 72,
                                height: 60,
                                child: RawImage(
                                    image: editor.filterThumbnails[editor
                                        .presetStore.presets
                                        .indexOf(preset)],
                                    fit: BoxFit.cover))),
                        const SizedBox(width: 12),
                      ],
                      Expanded(
                          child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                            Text(preset.name,
                                style: const TextStyle(fontSize: 12)),
                            const SizedBox(height: 5),
                            Text(preset.description,
                                style: const TextStyle(
                                    fontSize: 10, color: Colors.white38)),
                            if (!preset.colorOnly)
                              const Text('完整编辑预设（含相框）',
                                  style: TextStyle(
                                      fontSize: 9, color: Colors.white38)),
                          ])),
                    ]))),
        ],
        if (editor.recipe.lutPath != null) ...[
          const SizedBox(height: 14),
          Text('当前：${editor.recipe.lutName ?? '自定义滤镜'}',
              style: const TextStyle(fontSize: 11)),
          const SizedBox(height: 12),
          _slider('滤镜强度', editor.recipe.strength,
              label: '${(editor.recipe.strength * 100).round()}%',
              onChanged: (value) => editor.change(
                  editor.recipe.copyWith(strength: value),
                  transient: true)),
          const Text('内置滤镜的完整效果随强度变化，不改变手动调色和边框。',
              style: TextStyle(fontSize: 10, color: Colors.white38)),
          if (editor.recipe.lookKind == 1) ...[
            const SizedBox(height: 12),
            SwitchListTile(
                contentPadding: EdgeInsets.zero,
                title: const Text('自适应起点', style: TextStyle(fontSize: 12)),
                subtitle: const Text('有限度修正亮度和疑似色偏，不能识别历史调色。',
                    style: TextStyle(fontSize: 10)),
                value: editor.recipe.lookAdaptive,
                onChanged: (value) =>
                    editor.change(editor.recipe.copyWith(lookAdaptive: value))),
            if (editor.recipe.lookAdaptive) ...[
              _slider('自适应程度', editor.recipe.lookAdaptation,
                  onChanged: (value) => editor.change(
                      editor.recipe.copyWith(lookAdaptation: value),
                      transient: true)),
              Text(_lookAnalysisText(),
                  style: const TextStyle(fontSize: 10, color: Colors.white54)),
            ],
            _slider('材质感', editor.recipe.lookTexture,
                onChanged: (value) => editor.change(
                    editor.recipe.copyWith(lookTexture: value),
                    transient: true)),
            _slider('暗角', editor.recipe.lookVignette,
                onChanged: (value) => editor.change(
                    editor.recipe.copyWith(lookVignette: value),
                    transient: true)),
            _slider('颗粒', editor.recipe.lookGrain,
                onChanged: (value) => editor.change(
                    editor.recipe.copyWith(lookGrain: value),
                    transient: true)),
            const Text('想保留现场暖光，可关闭自适应，再用色温和强度微调。',
                style: TextStyle(fontSize: 10, color: Colors.white38)),
          ],
          TextButton(
              onPressed: () => editor
                  .change(editor.recipe.copyWith(clearLut: true, lookKind: 0)),
              child: const Text('移除滤镜')),
        ],
        const Divider(height: 28),
        OutlinedButton.icon(
            onPressed: _importLut,
            icon: const Icon(Icons.grid_on, size: 16),
            label: const Text('导入 .cube LUT')),
        OutlinedButton.icon(
            onPressed: _savePreset,
            icon: const Icon(Icons.bookmark_add_outlined, size: 16),
            label: const Text('保存当前编辑预设')),
        Row(children: [
          Expanded(
              child: TextButton(
                  onPressed: _importPreset, child: const Text('导入预设'))),
          Expanded(
              child: TextButton(
                  onPressed: _exportPreset, child: const Text('导出预设'))),
        ]),
      ];
  List<Widget> _borderControls() => [
        _section('让照片完整呈现', '样式只调整留白与排版，保留所选标识、字体和信息。'),
        Wrap(spacing: 6, runSpacing: 6, children: [
          for (final style in const [
            (name: '无边框', width: 0.0, bottom: 0.0, layout: -1),
            (name: '纯色留白', width: .05, bottom: 0.0, layout: 0),
            (name: '单行信息', width: .04, bottom: .15, layout: 0),
            (name: '左右信息栏', width: .025, bottom: .12, layout: 1),
            (name: '画廊双行', width: .06, bottom: .19, layout: 2),
            (name: '信息栏', width: .025, bottom: .14, layout: 1),
            (name: '画廊', width: .06, bottom: .21, layout: 2),
            (name: '留白', width: .04, bottom: .15, layout: 1),
          ])
            ChoiceChip(
                label: Text(style.name),
                showCheckmark: false,
                selected: !editor.recipe.frameAdvanced &&
                    (editor.recipe.borderWidth - style.width).abs() < .000001 &&
                    (editor.recipe.borderBottom - style.bottom).abs() <
                        .000001 &&
                    (style.layout < 0 ||
                        editor.recipe.frameLayout == style.layout),
                onSelected: (_) => editor.change(editor.recipe.copyWith(
                    frameAdvanced: false,
                    borderWidth: style.width,
                    borderBottom: style.bottom,
                    frameLayout: style.layout < 0
                        ? editor.recipe.frameLayout
                        : style.layout))),
        ]),
        const SizedBox(height: 24),
        if (editor.presetStore.presets.any((p) => p.frameOnly))
          const Padding(
              padding: EdgeInsets.only(bottom: 8),
              child: Text('我的完整边框预设',
                  style: TextStyle(fontSize: 11, color: Colors.white70))),
        for (final preset
            in editor.presetStore.presets.where((p) => p.frameOnly))
          Padding(
              padding: const EdgeInsets.only(bottom: 8),
              child: OutlinedButton(
                  onPressed: () => editor.applyPreset(preset),
                  child: Text(preset.name))),
        _slider('边框宽度', editor.recipe.borderWidth,
            max: .25,
            label: '${(editor.recipe.borderWidth * 100).toStringAsFixed(1)}%',
            onChanged: (value) => editor.change(
                editor.recipe.copyWith(borderWidth: value),
                transient: true)),
        _slider('底部留白', editor.recipe.borderBottom,
            max: .4,
            label: '${(editor.recipe.borderBottom * 100).toStringAsFixed(1)}%',
            onChanged: (value) => editor.change(
                editor.recipe.copyWith(borderBottom: value),
                transient: true)),
        const Text('边框颜色',
            style: TextStyle(fontSize: 11, color: Colors.white70)),
        const SizedBox(height: 12),
        Wrap(spacing: 12, children: [
          for (final color in [0xffffff, 0xf3eee4, 0x151515, 0x314137])
            InkWell(
                onTap: () =>
                    editor.change(editor.recipe.copyWith(borderColor: color)),
                child: Container(
                    width: 30,
                    height: 30,
                    decoration: BoxDecoration(
                        color: Color(0xff000000 | color),
                        shape: BoxShape.circle,
                        border: Border.all(
                            color: editor.recipe.borderColor == color
                                ? const Color(0xffe5c185)
                                : Colors.white24,
                            width: 2))))
        ]),
        const SizedBox(height: 24),
        const Text('显示内容',
            style: TextStyle(fontSize: 11, color: Colors.white70)),
        const SizedBox(height: 10),
        Wrap(spacing: 6, runSpacing: 4, children: [
          for (final entry in const <int, String>{
            1: '机型',
            2: '焦距',
            4: '光圈',
            8: '快门',
            16: 'ISO',
            32: '日期',
            64: '自定义文字'
          }.entries)
            FilterChip(
                label: Text(entry.value),
                selected: editor.recipe.frameFields & entry.key != 0,
                onSelected: (selected) => editor.change(editor.recipe.copyWith(
                    frameFields: selected
                        ? editor.recipe.frameFields | entry.key
                        : editor.recipe.frameFields & ~entry.key))),
        ]),
        const SizedBox(height: 8),
        Wrap(spacing: 6, children: [
          TextButton(
              onPressed: () =>
                  editor.change(editor.recipe.copyWith(frameFields: 31)),
              child: const Text('拍摄参数')),
          TextButton(
              onPressed: () =>
                  editor.change(editor.recipe.copyWith(frameFields: 1)),
              child: const Text('仅机型')),
          TextButton(
              onPressed: () =>
                  editor.change(editor.recipe.copyWith(frameFields: 0)),
              child: const Text('隐藏信息')),
        ]),
        const Text('缺失的拍摄信息自动隐藏。文字和标识显示在底部留白区域。',
            style: TextStyle(fontSize: 10, color: Colors.white38)),
        const SizedBox(height: 18),
        const Text('标识来源',
            style: TextStyle(fontSize: 11, color: Colors.white70)),
        const SizedBox(height: 10),
        SegmentedButton<String>(
            segments: const [
              ButtonSegment(value: 'auto', label: Text('自动')),
              ButtonSegment(value: 'manual', label: Text('手动')),
              ButtonSegment(value: 'hidden', label: Text('隐藏')),
            ],
            selected: {
              _markMode
            },
            onSelectionChanged: (values) {
              final mode = values.single;
              editor.change(editor.recipe.copyWith(
                  frameMark: mode == 'auto'
                      ? 'auto'
                      : mode == 'manual'
                          ? (editor.recipe.frameMark.isNotEmpty &&
                                  editor.recipe.frameMark != 'auto'
                              ? editor.recipe.frameMark
                              : 'sony')
                          : '',
                  frameAccent:
                      mode == 'hidden' ? '' : editor.recipe.frameAccent,
                  logoPath: mode == 'manual' ? editor.recipe.logoPath : '',
                  frameBrand: 0,
                  borderBottom:
                      mode != 'hidden' && editor.recipe.borderBottom == 0
                          ? .15
                          : editor.recipe.borderBottom));
            }),
        const SizedBox(height: 10),
        if (_markMode == 'auto')
          Text(
              markForMake(editor.metadata['make'] as String? ?? '',
                          editor.metadata['model'] as String? ?? '')
                      .isEmpty
                  ? '根据照片 EXIF 推荐品牌；未识别到品牌时留空。也可切换手动选择。'
                  : '根据照片推荐：${frameMarks[markForMake(editor.metadata['make'] as String? ?? '', editor.metadata['model'] as String? ?? '')]}。可切换手动选择。',
              style: const TextStyle(fontSize: 10, color: Colors.white54)),
        if (_markMode == 'manual') ...[
          const Text('选择标识 · 无需 EXIF，不改变照片的机型与拍摄参数',
              style: TextStyle(fontSize: 10, color: Colors.white54)),
          const SizedBox(height: 10),
          Wrap(spacing: 8, runSpacing: 8, children: [
            for (final mark in frameMarks.entries
                .where((mark) => mark.key.isNotEmpty && mark.key != 'auto'))
              _frameMarkTile(mark.key, mark.value),
          ]),
        ],
        if (_markMode == 'hidden')
          const Text('标识已隐藏，机型与拍摄参数仍按各自开关显示。',
              style: TextStyle(fontSize: 10, color: Colors.white54)),
        const SizedBox(height: 16),
        DropdownButtonFormField<String>(
            key: ValueKey('accent-${editor.recipe.frameAccent}'),
            isExpanded: true,
            initialValue: editor.recipe.frameAccent,
            decoration: const InputDecoration(labelText: '镜头辅助标识'),
            items: [
              for (final mark in frameAccents.entries)
                DropdownMenuItem(
                    value: mark.key,
                    child: Text(mark.value, overflow: TextOverflow.ellipsis))
            ],
            onChanged: (value) {
              if (value != null) {
                editor.change(editor.recipe.copyWith(
                    frameAccent: value,
                    logoPath: '',
                    frameBrand: 0,
                    borderBottom:
                        value.isNotEmpty && editor.recipe.borderBottom == 0
                            ? .15
                            : editor.recipe.borderBottom));
              }
            }),
        const SizedBox(height: 10),
        Text(
            editor.recipe.frameAccent == 'auto'
                ? _lensRecommendation
                : '镜头标识可独立选择，不根据机身品牌推断。',
            style: TextStyle(fontSize: 10, color: Colors.white38)),
        const SizedBox(height: 12),
        DropdownButtonFormField<int>(
            key: ValueKey('tone-${editor.recipe.markTone}'),
            initialValue: editor.recipe.markTone,
            decoration: const InputDecoration(labelText: '标识配色'),
            items: const [
              DropdownMenuItem(value: 0, child: Text('自动黑白 · 跟随边框颜色')),
              DropdownMenuItem(value: 1, child: Text('原色')),
              DropdownMenuItem(value: 2, child: Text('黑色')),
              DropdownMenuItem(value: 3, child: Text('白色')),
            ],
            onChanged: (value) {
              if (value != null) {
                editor.change(editor.recipe.copyWith(markTone: value));
              }
            }),
        if (editor.recipe.frameBrand != 0)
          TextButton(
              onPressed: () =>
                  editor.change(editor.recipe.copyWith(frameBrand: 0)),
              child: const Text('关闭旧预设的品牌文字')),
        const SizedBox(height: 12),
        OutlinedButton.icon(
            onPressed: _logo,
            icon: const Icon(Icons.add_photo_alternate_outlined, size: 16),
            label: const Text('使用自定义 PNG 标识')),
        if (editor.recipe.logoPath.isNotEmpty)
          TextButton(
              onPressed: () =>
                  editor.change(editor.recipe.copyWith(logoPath: '')),
              child: const Text('移除自定义标识')),
        const Text('支持透明 PNG（≤4MB、≤100万像素）。自定义标识与内置标识分别使用。',
            style: TextStyle(fontSize: 10, color: Colors.white38)),
        const SizedBox(height: 24),
        OutlinedButton.icon(
            icon: const Icon(Icons.edit_note, size: 16),
            label: const Text('设置自定义文字'),
            onPressed: () => _guard(() async {
                  final value =
                      await _nameDialog('相框自定义文字', editor.recipe.caption);
                  if (value != null && mounted) {
                    editor.change(editor.recipe.copyWith(
                        caption: value,
                        frameFields: value.isEmpty
                            ? editor.recipe.frameFields & ~64
                            : editor.recipe.frameFields | 64));
                  }
                })),
        Text(
            editor.recipe.caption.isEmpty
                ? '自定义文字为空；按上方开关显示 EXIF'
                : editor.recipe.caption,
            style: const TextStyle(
                fontSize: 10, color: Colors.white54, height: 1.6)),
        const SizedBox(height: 16),
        const Divider(height: 28),
        OutlinedButton.icon(
            onPressed: _saveFrame,
            icon: const Icon(Icons.bookmark_add_outlined, size: 16),
            label: const Text('保存边框预设')),
        Row(children: [
          Expanded(
              child: TextButton(
                  onPressed: _importPreset, child: const Text('导入边框预设'))),
          Expanded(
              child: TextButton(
                  onPressed: _exportFrame, child: const Text('导出边框预设'))),
        ]),
        DropdownButtonFormField<String>(
            key: ValueKey('font-${editor.recipe.frameFont}'),
            initialValue: editor.recipe.frameFont,
            decoration: const InputDecoration(labelText: '机型、参数与题注字体'),
            items: [
              for (final font in frameFonts.entries)
                DropdownMenuItem(
                    value: font.key,
                    enabled: font.key != 'custom' ||
                        editor.recipe.fontPath.isNotEmpty,
                    child: Text(font.value))
            ],
            onChanged: (value) {
              if (value != null) {
                editor.change(editor.recipe.copyWith(frameFont: value));
              }
            }),
        const SizedBox(height: 10),
        OutlinedButton.icon(
            onPressed: _font,
            icon: const Icon(Icons.text_fields, size: 16),
            label: const Text('导入本地字体')),
        if (editor.recipe.frameFont == 'custom')
          Text(
              editor.recipe.fontPath.isEmpty
                  ? '请选择字体文件'
                  : editor.recipe.fontPath.split(Platform.pathSeparator).last,
              style: const TextStyle(fontSize: 10, color: Colors.white38)),
        const Text('内置字体用于拉丁字母与数字；中文题注请选择含中文字形的本地字体。品牌字标保持原样。',
            style: TextStyle(fontSize: 10, color: Colors.white38, height: 1.6)),
        const SizedBox(height: 20),
        ExpansionTile(
            title: const Text('高级排版'),
            subtitle: const Text('分别调整标识、信息与题注的位置和大小',
                style: TextStyle(fontSize: 10)),
            tilePadding: EdgeInsets.zero,
            children: [
              SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  title: const Text('启用位置微调'),
                  value: editor.recipe.frameAdvanced,
                  onChanged: (enabled) => editor.change(editor.recipe.copyWith(
                      frameAdvanced: enabled,
                      borderBottom: enabled && editor.recipe.borderBottom == 0
                          ? .15
                          : editor.recipe.borderBottom))),
              if (editor.recipe.frameAdvanced) ...[
                const Text('位置按底部留白区域的比例保存；内容限制在留白区域内。',
                    style: TextStyle(fontSize: 10, color: Colors.white54)),
                for (final group in const [
                  (title: '品牌主标识', indices: [0, 1, 2]),
                  (title: '镜头辅助标识', indices: [3, 4, 5]),
                  (title: '机型与拍摄参数', indices: [6, 7, 8, 9]),
                  (title: '日期与自定义题注', indices: [10, 11, 12]),
                ])
                  ExpansionTile(
                      title: Text(group.title,
                          style: const TextStyle(fontSize: 12)),
                      children: [
                        for (final index in group.indices)
                          _framePositionSlider(index),
                        if (group.indices.contains(6) ||
                            group.indices.contains(10))
                          DropdownButtonFormField<int>(
                              key: ValueKey(
                                  'align-${group.title}-${group.indices.contains(6) ? editor.recipe.frameInfoAlign : editor.recipe.frameCaptionAlign}'),
                              initialValue: group.indices.contains(6)
                                  ? editor.recipe.frameInfoAlign
                                  : editor.recipe.frameCaptionAlign,
                              decoration:
                                  const InputDecoration(labelText: '文字对齐'),
                              items: const [
                                DropdownMenuItem(value: 0, child: Text('左对齐')),
                                DropdownMenuItem(value: 1, child: Text('居中')),
                                DropdownMenuItem(value: 2, child: Text('右对齐')),
                              ],
                              onChanged: (value) {
                                if (value == null) return;
                                editor.change(group.indices.contains(6)
                                    ? editor.recipe
                                        .copyWith(frameInfoAlign: value)
                                    : editor.recipe
                                        .copyWith(frameCaptionAlign: value));
                              }),
                      ]),
              ],
              TextButton.icon(
                  icon: const Icon(Icons.restart_alt, size: 16),
                  label: const Text('恢复当前样式排版'),
                  onPressed: () => editor.change(editor.recipe.copyWith(
                      frameAdvanced: false,
                      framePositions: defaultFramePositions,
                      frameInfoAlign: 2,
                      frameCaptionAlign: 2))),
            ]),
      ];
  String get _lensRecommendation {
    final make = editor.metadata['lensMake'] as String? ?? '';
    final model = editor.metadata['lensModel'] as String? ?? '';
    final id = accentForLens(make, model);
    if (id.isEmpty) {
      return model.isEmpty
          ? '照片缺少可识别的镜头信息；标识留空，可手动选择。'
          : '已读到镜头：$model。未匹配标识，可手动选择。';
    }
    return '根据镜头推荐：${frameAccents[id]}${model.isEmpty ? '' : ' · $model'}';
  }

  Widget _framePositionSlider(int index) {
    final low = [2, 5, 8, 9, 12].contains(index) ? .05 : 0.0;
    final high = index == 8 || index == 12
        ? .4
        : index == 9
            ? .5
            : 1.0;
    final title = index == 2 || index == 5
        ? '标识大小'
        : index == 8 || index == 12
            ? '字号'
            : index == 9
                ? '行距'
                : [0, 3, 6, 10].contains(index)
                    ? '水平位置'
                    : '垂直位置';
    return _slider(title, editor.recipe.framePositions[index],
        min: low,
        max: high,
        label: '${(editor.recipe.framePositions[index] * 100).round()}%',
        onChanged: (value) {
      final positions = List<double>.of(editor.recipe.framePositions)
        ..[index] = value;
      editor.change(editor.recipe.copyWith(framePositions: positions),
          transient: true);
    });
  }

  String get _markMode {
    if (editor.recipe.frameMark == 'auto') return 'auto';
    if (editor.recipe.frameMark.isNotEmpty ||
        editor.recipe.frameAccent.isNotEmpty ||
        editor.recipe.logoPath.isNotEmpty ||
        editor.recipe.frameBrand != 0) {
      return 'manual';
    }
    return 'hidden';
  }

  Widget _frameMarkTile(String id, String label) {
    final selected =
        editor.recipe.frameMark == id && editor.recipe.logoPath.isEmpty;
    final tone =
        resolvedMarkTone(editor.recipe.markTone, editor.recipe.borderColor);
    return SizedBox(
        width: 132,
        child: OutlinedButton(
            style: OutlinedButton.styleFrom(
                padding: const EdgeInsets.all(10),
                side: BorderSide(
                    color:
                        selected ? const Color(0xffe5c185) : Colors.white24)),
            onPressed: () => editor.change(editor.recipe.copyWith(
                frameMark: id,
                logoPath: '',
                frameBrand: 0,
                borderBottom: id.isNotEmpty && editor.recipe.borderBottom == 0
                    ? .15
                    : editor.recipe.borderBottom)),
            child: Column(mainAxisSize: MainAxisSize.min, children: [
              Container(
                  height: 36,
                  width: 108,
                  padding: const EdgeInsets.all(5),
                  decoration: BoxDecoration(
                      color: tone == 3
                          ? const Color(0xff202020)
                          : const Color(0xfff5f3ef),
                      borderRadius: BorderRadius.circular(4)),
                  child: id.isEmpty || id == 'auto'
                      ? Icon(id.isEmpty ? Icons.block : Icons.auto_awesome,
                          color: tone == 3 ? Colors.white70 : Colors.black54,
                          size: 20)
                      : Image.asset('assets/frame_marks/$id-$tone.png',
                          fit: BoxFit.contain)),
              const SizedBox(height: 6),
              Text(label,
                  style: const TextStyle(fontSize: 11),
                  textAlign: TextAlign.center),
            ])));
  }

  Widget _effectSlider(String title, int index,
          {double min = 0, double max = 1, String? label}) =>
      _slider(title, editor.recipe.effects[index],
          min: min,
          max: max,
          label: label,
          onChanged: (value) => editor
              .change(editor.recipe.effect(index, value), transient: true));

  List<Widget> _advancedControls() => [
        _section('精细调整', 'HSL、曲线和分离色调在 LUT 前应用。锐化与降噪针对当前分辨率处理。'),
        ExpansionTile(
            title: const Text('HSL 色彩'),
            initiallyExpanded: true,
            tilePadding: EdgeInsets.zero,
            children: [
              DropdownButton<int>(
                  value: hslChannel,
                  isExpanded: true,
                  items: [
                    for (var i = 0; i < 8; ++i)
                      DropdownMenuItem(
                          value: i,
                          child: Text(const [
                            '红',
                            '橙',
                            '黄',
                            '绿',
                            '青',
                            '蓝',
                            '紫',
                            '洋红'
                          ][i]))
                  ],
                  onChanged: (value) => setState(() => hslChannel = value!)),
              _effectSlider('色相', hslChannel, min: -1),
              _effectSlider('饱和度', 8 + hslChannel, min: -1),
              _effectSlider('明度', 16 + hslChannel, min: -1),
            ]),
        ExpansionTile(
            title: const Text('RGB 主曲线'),
            tilePadding: EdgeInsets.zero,
            children: [
              SizedBox(
                  height: 120,
                  width: double.infinity,
                  child: CustomPaint(
                      painter: _CurvePainter(
                          editor.recipe.effects.sublist(24, 29)))),
              const SizedBox(height: 12),
              for (var i = 0; i < 5; ++i)
                _effectSlider(const ['黑场', '暗部', '中间调', '亮部', '白场'][i], 24 + i),
            ]),
        ExpansionTile(
            title: const Text('分离色调'),
            tilePadding: EdgeInsets.zero,
            children: [
              _effectSlider('阴影色相', 29,
                  label: '${(editor.recipe.effects[29] * 360).round()}°'),
              _effectSlider('阴影强度', 30),
              _effectSlider('高光色相', 31,
                  label: '${(editor.recipe.effects[31] * 360).round()}°'),
              _effectSlider('高光强度', 32),
              _effectSlider('明暗平衡', 33, min: -1),
            ]),
        ExpansionTile(
            title: const Text('细节'),
            tilePadding: EdgeInsets.zero,
            children: [
              _effectSlider('锐化强度', 34, max: 2),
              _effectSlider('锐化半径', 35,
                  min: 1,
                  max: 3,
                  label: '${editor.recipe.effects[35].round()} px'),
              _effectSlider('明度降噪', 36),
              _effectSlider('色彩降噪', 37),
              const Text('降噪使用局部边缘保护，不替代相机 RAW 专用降噪。',
                  style: TextStyle(fontSize: 10, color: Colors.white38)),
            ]),
        TextButton(
            onPressed: () =>
                editor.change(editor.recipe.copyWith(effects: defaultEffects)),
            child: const Text('重置高级调整')),
      ];

  Widget _status() => Container(
      height: 30,
      padding: const EdgeInsets.symmetric(horizontal: 20),
      decoration: const BoxDecoration(
          border: Border(top: BorderSide(color: Colors.white10))),
      child: Row(children: [
        const Icon(Icons.circle, size: 5, color: Color(0xff8aa591)),
        const SizedBox(width: 7),
        Text(editor.processing ? '正在生成预览…' : '本地处理 · 原图不变',
            style: const TextStyle(fontSize: 9, color: Colors.white38)),
        const Spacer(),
        const Text('16-bit · Linear sRGB',
            style: TextStyle(fontSize: 9, color: Colors.white30))
      ]));
}

class _CurvePainter extends CustomPainter {
  _CurvePainter(this.points);
  final List<double> points;
  @override
  void paint(Canvas canvas, Size size) {
    final guide = Paint()
      ..color = Colors.white12
      ..strokeWidth = 1;
    for (var i = 0; i <= 4; ++i) {
      canvas.drawLine(Offset(size.width * i / 4, 0),
          Offset(size.width * i / 4, size.height), guide);
      canvas.drawLine(Offset(0, size.height * i / 4),
          Offset(size.width, size.height * i / 4), guide);
    }
    final line = Paint()
      ..color = const Color(0xffe5c185)
      ..strokeWidth = 2
      ..style = PaintingStyle.stroke;
    final path = Path();
    for (var i = 0; i < 5; ++i) {
      final point = Offset(size.width * i / 4, size.height * (1 - points[i]));
      if (i == 0) {
        path.moveTo(point.dx, point.dy);
      } else {
        path.lineTo(point.dx, point.dy);
      }
      canvas.drawCircle(point, 3, Paint()..color = const Color(0xffe5c185));
    }
    canvas.drawPath(path, line);
  }

  @override
  bool shouldRepaint(_CurvePainter oldDelegate) =>
      !listEquals(points, oldDelegate.points);
}
