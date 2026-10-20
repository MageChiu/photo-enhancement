import 'dart:async';
import 'dart:io';
import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'editor_controller.dart';
import 'models/edit.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const PhotoApp());
}

class PhotoApp extends StatelessWidget {
  const PhotoApp({super.key});
  @override
  Widget build(BuildContext context) => MaterialApp(
    title: '光屿 · Photo Enhancement', debugShowCheckedModeBanner: false,
    theme: ThemeData(
      brightness: Brightness.dark, useMaterial3: true,
      colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xffe5c185), brightness: Brightness.dark,
        primary: const Color(0xffe5c185), surface: const Color(0xff191c20)),
      scaffoldBackgroundColor: const Color(0xff111316),
      sliderTheme: const SliderThemeData(trackHeight: 2, thumbShape: RoundSliderThumbShape(enabledThumbRadius: 5)),
      inputDecorationTheme: const InputDecorationTheme(border: OutlineInputBorder(), isDense: true),
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
  int tab = 0;
  static const imageTypes = XTypeGroup(label: '照片与 RAW', extensions: [
    'jpg', 'jpeg', 'png', 'heic', 'heif', 'webp', 'arw', 'nef', 'cr2', 'cr3',
    'rw2', 'raf', 'orf', 'dng', 'pef', 'srw', 'raw', 'tif', 'tiff',
  ]);
  @override
  void initState() { super.initState(); unawaited(editor.initialize()); }
  @override
  void dispose() { editor.dispose(); super.dispose(); }

  Future<void> _guard(Future<void> Function() action) async {
    try { await action(); } catch (e) { if (mounted) editor.report(e); }
  }
  Future<void> _open() => _guard(() async {
    final files = await openFiles(acceptedTypeGroups: [imageTypes]);
    if (files.isEmpty || !mounted) return;
    for (final file in files.skip(1)) {
      editor.recentFiles.remove(file.path); editor.recentFiles.add(file.path);
    }
    editor.open(files.first.path);
  });
  Future<void> _importLut() => _guard(() async {
    final file = await openFile(acceptedTypeGroups: [const XTypeGroup(label: '3D LUT', extensions: ['cube'])]);
    if (file != null && mounted) await editor.importLut(file.path);
  });
  Future<void> _importPreset() => _guard(() async {
    final file = await openFile(acceptedTypeGroups: [const XTypeGroup(label: '照片预设', extensions: ['json'])]);
    if (file != null && mounted) await editor.importPreset(file.path);
  });
  Future<void> _font() => _guard(() async {
    final file = await openFile(acceptedTypeGroups: [const XTypeGroup(label: '字体', extensions: ['ttf', 'otf', 'ttc'])]);
    if (file != null && mounted) editor.change(editor.recipe.copyWith(fontPath: file.path));
  });
  Future<String?> _nameDialog(String title, String initial) async {
    final input = TextEditingController(text: initial);
    try {
      return await showDialog<String>(context: context, builder: (context) => AlertDialog(
        title: Text(title), content: SizedBox(width: 360, child: TextField(controller: input,
          autofocus: true, maxLength: 100, onSubmitted: (value) => Navigator.pop(context, value))),
        actions: [TextButton(onPressed: () => Navigator.pop(context), child: const Text('取消')),
          FilledButton(onPressed: () => Navigator.pop(context, input.text), child: const Text('保存'))],
      ));
    } finally { input.dispose(); }
  }
  Future<void> _savePreset() => _guard(() async {
    final name = await _nameDialog('保存为本地预设', '我的纪实风格');
    if (name != null && mounted) await editor.savePreset(name);
  });
  Future<void> _exportPreset() => _guard(() async {
    final name = await _nameDialog('导出便携预设（内含 LUT）', '我的纪实风格');
    if (name == null || !mounted) return;
    final content = await editor.presetStore.serialize(name, editor.recipe);
    final target = await getSaveLocation(suggestedName: 'photo-style.preset.json',
      acceptedTypeGroups: [const XTypeGroup(label: '照片预设', extensions: ['json'])]);
    if (target == null) return;
    // The save dialog governs replacement of preset documents. Image exports
    // have the stronger native O_EXCL guarantee and can never overwrite photos.
    final file = File(target.path);
    await file.writeAsString(content, flush: true);
  });

  Future<void> _export() => _guard(() async {
    if (editor.sourcePath == null || editor.exporting) return;
    var format = 1, quality = 95, metadata = true;
    final accepted = await showDialog<bool>(context: context, builder: (context) => StatefulBuilder(
      builder: (context, update) => AlertDialog(
        title: const Text('导出原始分辨率'),
        content: SizedBox(width: 380, child: Column(mainAxisSize: MainAxisSize.min, children: [
          const Text('将从原图重新处理。不会覆盖原文件或任何已有文件。', style: TextStyle(color: Colors.white60)),
          const SizedBox(height: 20),
          DropdownButtonFormField<int>(value: format, decoration: const InputDecoration(labelText: '格式'),
            items: [const DropdownMenuItem(value: 1, child: Text('JPEG · 兼容性优先')),
              const DropdownMenuItem(value: 2, child: Text('PNG · 16 位无损')),
              if (editor.capabilities.contains('heif')) const DropdownMenuItem(value: 3, child: Text('HEIF · 需要可用编码器'))],
            onChanged: (value) => update(() => format = value!)),
          const SizedBox(height: 16),
          if (format != 2) ...[Text('质量 $quality'), Slider(value: quality.toDouble(), min: 1, max: 100,
            divisions: 99, onChanged: (value) => update(() => quality = value.round()))],
          SwitchListTile(contentPadding: EdgeInsets.zero, title: const Text('保留拍摄信息'),
            subtitle: const Text('仅机型、镜头参数与时间，不含 GPS'), value: metadata,
            onChanged: (value) => update(() => metadata = value)),
        ])),
        actions: [TextButton(onPressed: () => Navigator.pop(context, false), child: const Text('取消')),
          FilledButton(onPressed: () => Navigator.pop(context, true), child: const Text('选择保存位置'))],
      ),
    ));
    if (accepted != true || !mounted) return;
    final extension = format == 1 ? 'jpg' : format == 2 ? 'png' : 'heic';
    final name = editor.sourcePath!.split(Platform.pathSeparator).last.replaceFirst(RegExp(r'\.[^.]+$'), '');
    final location = await getSaveLocation(suggestedName: '$name-edited.$extension',
      acceptedTypeGroups: [XTypeGroup(label: extension.toUpperCase(), extensions: [extension])]);
    if (location != null && mounted) await editor.export(location.path, format: format, quality: quality, includeMetadata: metadata);
  });

  @override
  Widget build(BuildContext context) => CallbackShortcuts(
    bindings: {
      SingleActivator(LogicalKeyboardKey.keyO, meta: Platform.isMacOS, control: !Platform.isMacOS): _open,
      SingleActivator(LogicalKeyboardKey.keyE, meta: Platform.isMacOS, control: !Platform.isMacOS): _export,
      SingleActivator(LogicalKeyboardKey.keyZ, meta: Platform.isMacOS, control: !Platform.isMacOS): editor.undo,
      SingleActivator(LogicalKeyboardKey.keyZ, meta: Platform.isMacOS, control: !Platform.isMacOS, shift: true): editor.redo,
    },
    child: Focus(autofocus: true, child: AnimatedBuilder(animation: editor, builder: (context, _) => Scaffold(
      body: SafeArea(child: Column(children: [
        _toolbar(),
        if (editor.error != null || editor.notice != null) _message(),
        Expanded(child: LayoutBuilder(builder: (context, constraints) {
          final controls = _controls();
          if (constraints.maxWidth < 760) {
            return Column(children: [Expanded(child: _canvas()), SizedBox(height: 300, child: controls)]);
          }
          return Row(children: [if (constraints.maxWidth > 1130) _library(),
            Expanded(child: _canvas()), SizedBox(width: 320, child: controls)]);
        })),
        _status(),
      ])),
    ))),
  );

  Widget _toolbar() => Container(
    height: 76, padding: const EdgeInsets.symmetric(horizontal: 24),
    decoration: const BoxDecoration(border: Border(bottom: BorderSide(color: Colors.white10))),
    child: Row(children: [
      const Icon(Icons.filter_frames_rounded, color: Color(0xffe5c185), size: 30),
      const SizedBox(width: 12), const Text('光屿', style: TextStyle(fontSize: 23, fontWeight: FontWeight.w600, letterSpacing: 4)),
      if (MediaQuery.sizeOf(context).width > 1000) ...[
        const SizedBox(width: 14), const Text('PHOTO\nENHANCEMENT', style: TextStyle(fontSize: 9, letterSpacing: 2, color: Colors.white38)),
        const SizedBox(width: 24), _pill('纯本地 · 离线处理', Icons.shield_outlined),
      ],
      const Spacer(),
      if (MediaQuery.sizeOf(context).width > 540) ...[
        IconButton(tooltip: '撤销', onPressed: editor.history.canUndo ? editor.undo : null, icon: const Icon(Icons.undo, size: 20)),
        IconButton(tooltip: '重做', onPressed: editor.history.canRedo ? editor.redo : null, icon: const Icon(Icons.redo, size: 20)),
      ],
      const SizedBox(width: 10),
      OutlinedButton.icon(onPressed: _open, icon: const Icon(Icons.add_photo_alternate_outlined, size: 18), label: const Text('导入')),
      const SizedBox(width: 10),
      FilledButton.icon(onPressed: editor.sourcePath != null && !editor.exporting ? _export : null,
        icon: editor.exporting ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2)) : const Icon(Icons.file_download_outlined, size: 18),
        label: Text(editor.exporting ? '导出中' : '导出')),
    ]),
  );
  Widget _pill(String text, IconData icon) => Container(
    padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
    decoration: BoxDecoration(color: Colors.white.withValues(alpha: .04), borderRadius: BorderRadius.circular(20)),
    child: Row(mainAxisSize: MainAxisSize.min, children: [Icon(icon, size: 12, color: Colors.white54),
      const SizedBox(width: 5), Text(text, style: const TextStyle(fontSize: 10, color: Colors.white54))]),
  );
  Widget _message() => Container(width: double.infinity, padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 10),
    color: editor.error != null ? const Color(0xff482725) : const Color(0xff20392c),
    child: SelectableText(editor.error ?? editor.notice!, style: const TextStyle(fontSize: 12)));

  Widget _library() => Container(width: 200, color: const Color(0xff17191d), child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
    const Padding(padding: EdgeInsets.all(20), child: Text('本地图库', style: TextStyle(fontSize: 12, fontWeight: FontWeight.w600, letterSpacing: 2))),
    Padding(padding: const EdgeInsets.symmetric(horizontal: 14), child: OutlinedButton.icon(onPressed: _open,
      icon: const Icon(Icons.add, size: 16), label: const Text('添加照片'))),
    const Padding(padding: EdgeInsets.fromLTRB(20, 24, 20, 10), child: Text('本次打开', style: TextStyle(fontSize: 10, color: Colors.white38))),
    Expanded(child: ListView.builder(itemCount: editor.recentFiles.length, itemBuilder: (context, index) {
      final path = editor.recentFiles[index]; final selected = path == editor.sourcePath;
      return ListTile(dense: true, selected: selected,
        selectedTileColor: const Color(0xff2b2924), leading: Icon(Icons.image_outlined, size: 18,
          color: selected ? const Color(0xffe5c185) : Colors.white38),
        title: Text(path.split(Platform.pathSeparator).last, maxLines: 2, overflow: TextOverflow.ellipsis, style: const TextStyle(fontSize: 11)),
        onTap: () => editor.open(path));
    })),
    const Padding(padding: EdgeInsets.all(20), child: Text('原图始终保留\n所有编辑均以参数记录', style: TextStyle(fontSize: 10, height: 1.8, color: Colors.white30))),
  ]));

  Widget _canvas() => Column(children: [
    SizedBox(height: 48, child: Row(children: [const SizedBox(width: 24),
      Expanded(child: Text(editor.sourcePath?.split(Platform.pathSeparator).last ?? '你的下一张好照片，从这里开始',
        overflow: TextOverflow.ellipsis, style: const TextStyle(fontSize: 11, color: Colors.white54))),
      if (editor.preview != null) _pill('适应画布 · 可缩放', Icons.zoom_in), const SizedBox(width: 24),
    ])),
    Expanded(child: Padding(padding: const EdgeInsets.fromLTRB(28, 8, 28, 22), child: Stack(children: [
      Positioned.fill(child: editor.preview == null ? _empty() : InteractiveViewer(minScale: .5, maxScale: 6,
        child: Center(child: RawImage(image: editor.preview, fit: BoxFit.contain, filterQuality: FilterQuality.high)))),
      if (editor.processing) const Positioned(right: 12, bottom: 12, child: SizedBox(width: 20, height: 20,
        child: CircularProgressIndicator(strokeWidth: 2, color: Color(0xffe5c185)))),
    ]))),
    if (editor.metadata.isNotEmpty) Container(padding: const EdgeInsets.only(bottom: 20), child: _exif()),
  ]);
  Widget _empty() => Center(child: SingleChildScrollView(child: Column(mainAxisSize: MainAxisSize.min, children: [
    Container(width: 100, height: 100, decoration: BoxDecoration(color: const Color(0xff202329), borderRadius: BorderRadius.circular(28)),
      child: const Icon(Icons.landscape_outlined, size: 45, color: Color(0xffb3a18a))),
    const SizedBox(height: 26), const Text('还原现场，也留下你的表达', style: TextStyle(fontSize: 21, letterSpacing: 1)),
    const SizedBox(height: 12), const Text('十项调色 · 纪实预设 · EXIF 相框', style: TextStyle(color: Colors.white38, fontSize: 12)),
    const SizedBox(height: 30), FilledButton.icon(onPressed: _open, icon: const Icon(Icons.add, size: 18), label: const Text('选择照片')),
    const SizedBox(height: 16), const Text('JPEG / PNG / HEIC / WebP / 相机 RAW', style: TextStyle(color: Colors.white24, fontSize: 10)),
  ])));
  Widget _exif() {
    final i = editor.metadata;
    final parts = <String>[
      if ((i['model'] as String? ?? '').isNotEmpty) i['model'] as String,
      if ((i['aperture'] as num? ?? 0) > 0) 'f/${(i['aperture'] as num).toStringAsPrecision(2)}',
      if ((i['shutter'] as num? ?? 0) > 0) (i['shutter'] as num) < 1 ? '1/${(1 / (i['shutter'] as num)).round()}s' : '${i['shutter']}s',
      if ((i['iso'] as num? ?? 0) > 0) 'ISO ${i['iso']}',
      if (i['isRaw'] == true) 'RAW',
    ];
    return Text(parts.isEmpty ? '未包含拍摄信息' : parts.join('   ·   '), textAlign: TextAlign.center,
      style: const TextStyle(fontSize: 10, color: Colors.white38));
  }

  Widget _controls() => Container(color: const Color(0xff191c20), child: Column(children: [
    const SizedBox(height: 16),
    SegmentedButton<int>(showSelectedIcon: false, segments: const [
      ButtonSegment(value: 0, label: Text('调色')), ButtonSegment(value: 1, label: Text('风格')), ButtonSegment(value: 2, label: Text('边框')),
    ], selected: {tab}, onSelectionChanged: (value) => setState(() => tab = value.first)),
    const SizedBox(height: 10),
    Expanded(child: ListView(padding: const EdgeInsets.all(20), children: switch (tab) {
      0 => _toneControls(), 1 => _lookControls(), _ => _borderControls(),
    })),
  ]));
  Widget _section(String title, String subtitle) => Padding(padding: const EdgeInsets.only(bottom: 20), child: Column(
    crossAxisAlignment: CrossAxisAlignment.start, children: [Text(title, style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w500)),
      const SizedBox(height: 6), Text(subtitle, style: const TextStyle(fontSize: 10, color: Colors.white38, height: 1.6))]));
  List<Widget> _toneControls() => [
    Row(children: [const Expanded(child: Text('基础调整', style: TextStyle(fontSize: 15))), TextButton(onPressed: editor.reset, child: const Text('重置', style: TextStyle(fontSize: 11)))]),
    const SizedBox(height: 8),
    for (var i = 0; i < 10; ++i) _slider(controlLabels[i], editor.recipe.values[i],
      min: i == 0 ? -5 : -1, max: i == 0 ? 5 : 1,
      label: i == 0 ? '${editor.recipe.values[i].toStringAsFixed(2)} EV' : '${(editor.recipe.values[i] * 100).round()}',
      onChanged: (value) => editor.change(editor.recipe.control(i, value), transient: true)),
    const SizedBox(height: 14), const Text('色温 / 色调为相对调整；RAW 使用相机白平衡作为起点。', style: TextStyle(fontSize: 10, color: Colors.white30, height: 1.6)),
  ];
  Widget _slider(String title, double value, {double min = 0, double max = 1, String? label, required ValueChanged<double> onChanged}) => Padding(
    padding: const EdgeInsets.only(bottom: 12), child: Column(children: [
      Row(children: [Text(title, style: const TextStyle(fontSize: 11, color: Colors.white70)), const Spacer(),
        Text(label ?? '${(value * 100).round()}', style: const TextStyle(fontSize: 10, color: Colors.white38))]),
      SizedBox(height: 30, child: Slider(value: value, min: min, max: max,
        onChangeStart: (_) => editor.beginGesture(), onChangeEnd: (_) => editor.endGesture(),
        onChanged: editor.sourcePath == null ? null : onChanged)),
    ]));
  List<Widget> _lookControls() => [
    _section('现场的不同叙事', '以真实为底色，不模拟任何新闻机构的官方风格。'),
    for (final preset in editor.presetStore.presets) Padding(padding: const EdgeInsets.only(bottom: 10), child: OutlinedButton(
      style: OutlinedButton.styleFrom(alignment: Alignment.centerLeft, padding: const EdgeInsets.all(16)),
      onPressed: () => editor.applyPreset(preset), child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(preset.name, style: const TextStyle(fontSize: 12)), const SizedBox(height: 5),
        Text(preset.description, style: const TextStyle(fontSize: 10, color: Colors.white38)),
      ]))),
    if (editor.recipe.lutPath != null) ...[
      const SizedBox(height: 14), Text('当前：${editor.recipe.lutName ?? '自定义 LUT'}', style: const TextStyle(fontSize: 11)),
      const SizedBox(height: 12), _slider('LUT 强度', editor.recipe.strength,
        onChanged: (value) => editor.change(editor.recipe.copyWith(strength: value), transient: true)),
      TextButton(onPressed: () => editor.change(editor.recipe.copyWith(clearLut: true)), child: const Text('移除 LUT')),
    ],
    const Divider(height: 28), OutlinedButton.icon(onPressed: _importLut, icon: const Icon(Icons.grid_on, size: 16), label: const Text('导入 .cube LUT')),
    OutlinedButton.icon(onPressed: _savePreset, icon: const Icon(Icons.bookmark_add_outlined, size: 16), label: const Text('保存当前预设')),
    Row(children: [Expanded(child: TextButton(onPressed: _importPreset, child: const Text('导入预设'))),
      Expanded(child: TextButton(onPressed: _exportPreset, child: const Text('导出预设')))]),
  ];
  List<Widget> _borderControls() => [
    _section('让照片完整呈现', '边框以画面短边为比例，预览与原图导出保持一致。'),
    Wrap(spacing: 6, children: [
      ActionChip(label: const Text('无边框'), onPressed: () => editor.change(editor.recipe.copyWith(borderWidth: 0, borderBottom: 0))),
      ActionChip(label: const Text('纯色留白'), onPressed: () => editor.change(editor.recipe.copyWith(borderWidth: .05, borderBottom: 0))),
      ActionChip(label: const Text('EXIF 拍立得'), onPressed: () => editor.change(editor.recipe.copyWith(borderWidth: .04, borderBottom: .15))),
    ]),
    const SizedBox(height: 24), _slider('边框宽度', editor.recipe.borderWidth, max: .25,
      label: '${(editor.recipe.borderWidth * 100).toStringAsFixed(1)}%',
      onChanged: (value) => editor.change(editor.recipe.copyWith(borderWidth: value), transient: true)),
    _slider('底部留白', editor.recipe.borderBottom, max: .4,
      label: '${(editor.recipe.borderBottom * 100).toStringAsFixed(1)}%',
      onChanged: (value) => editor.change(editor.recipe.copyWith(borderBottom: value), transient: true)),
    const Text('边框颜色', style: TextStyle(fontSize: 11, color: Colors.white70)), const SizedBox(height: 12),
    Wrap(spacing: 12, children: [for (final color in [0xffffff, 0xf3eee4, 0x151515, 0x314137]) InkWell(
      onTap: () => editor.change(editor.recipe.copyWith(borderColor: color)),
      child: Container(width: 30, height: 30, decoration: BoxDecoration(color: Color(0xff000000 | color),
        shape: BoxShape.circle, border: Border.all(color: editor.recipe.borderColor == color ? const Color(0xffe5c185) : Colors.white24, width: 2))))]),
    const SizedBox(height: 24),
    OutlinedButton.icon(icon: const Icon(Icons.edit_note, size: 16), label: const Text('设置自定义文字'),
      onPressed: () => _guard(() async {
        final value = await _nameDialog('相框文字（留空使用 EXIF）', editor.recipe.caption);
        if (value != null && mounted) editor.change(editor.recipe.copyWith(caption: value));
      })),
    Text(editor.recipe.caption.isEmpty ? '自动使用 EXIF；无 EXIF 时留白' : editor.recipe.caption,
      style: const TextStyle(fontSize: 10, color: Colors.white54, height: 1.6)),
    const SizedBox(height: 16), OutlinedButton.icon(onPressed: _font, icon: const Icon(Icons.text_fields, size: 16), label: const Text('选择文字字体')),
    Text(editor.recipe.fontPath.isEmpty ? '文字边框需要本地字体（中文请选择包含中文字形的字体）' : editor.recipe.fontPath.split(Platform.pathSeparator).last,
      style: const TextStyle(fontSize: 10, color: Colors.white38, height: 1.6)),
  ];
  Widget _status() => Container(height: 30, padding: const EdgeInsets.symmetric(horizontal: 20),
    decoration: const BoxDecoration(border: Border(top: BorderSide(color: Colors.white10))),
    child: Row(children: [const Icon(Icons.circle, size: 5, color: Color(0xff8aa591)), const SizedBox(width: 7),
      Text(editor.processing ? '正在生成预览…' : '本地处理 · 原图不变', style: const TextStyle(fontSize: 9, color: Colors.white38)),
      const Spacer(), const Text('16-bit · Linear sRGB', style: TextStyle(fontSize: 9, color: Colors.white30))]));
}
