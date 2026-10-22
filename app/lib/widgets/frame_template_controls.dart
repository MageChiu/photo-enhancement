import 'dart:convert';
import 'dart:io';
import 'package:flutter/material.dart';
import '../models/edit.dart';
import '../models/frame_template.dart';

/// Each type owns its form; photographic controls are never shown here.
class FrameTemplateControls extends StatelessWidget {
  const FrameTemplateControls(
      {super.key,
      required this.recipe,
      required this.onChanged,
      required this.onAvatar});
  final EditRecipe recipe;
  final ValueChanged<FrameTemplate> onChanged;
  final VoidCallback onAvatar;

  Future<void> _edit(BuildContext context, String title, String initial,
      int bytes, ValueChanged<String> save,
      {bool multiline = false}) async {
    var text = initial;
    final form = GlobalKey<FormState>();
    final result = await showDialog<String>(
        context: context,
        builder: (context) => AlertDialog(
              title: Text(title),
              content: SizedBox(
                  width: 380,
                  child: Form(
                      key: form,
                      child: TextFormField(
                        initialValue: initial,
                        onChanged: (value) => text = value,
                        autofocus: true,
                        minLines: multiline ? 3 : 1,
                        maxLines: multiline ? 8 : 1,
                        maxLength: bytes - 1,
                        decoration:
                            const InputDecoration(helperText: '清空即可隐藏该项'),
                        validator: (value) {
                          if (value!.contains('\u0000') ||
                              utf8.encode(value).length >= bytes) {
                            return '内容过长，请缩短文字';
                          }
                          if (value.runes
                              .any((c) => c < 32 && !(multiline && c == 10))) {
                            return '内容包含不支持的控制字符';
                          }
                          return null;
                        },
                      ))),
              actions: [
                TextButton(
                    onPressed: () => Navigator.pop(context),
                    child: const Text('取消')),
                FilledButton(
                    onPressed: () {
                      if (form.currentState!.validate()) {
                        Navigator.pop(context, text);
                      }
                    },
                    child: const Text('保存'))
              ],
            ));
    if (result != null && context.mounted) save(result);
  }

  Widget _field(BuildContext context, String label, String value, int bytes,
          ValueChanged<String> save, {bool multiline = false}) =>
      ListTile(
          contentPadding: EdgeInsets.zero,
          title: Text(label, style: const TextStyle(fontSize: 12)),
          subtitle: Text(value.isEmpty ? '未填写' : value,
              maxLines: 2, overflow: TextOverflow.ellipsis),
          trailing: const Icon(Icons.edit_outlined, size: 18),
          onTap: () =>
              _edit(context, label, value, bytes, save, multiline: multiline));

  @override
  Widget build(BuildContext context) {
    final t = recipe.template;
    if (t.category == 'film') {
      return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        const Text('胶片样式'),
        const SizedBox(height: 10),
        Wrap(spacing: 6, children: [
          for (final entry in const {0: '两侧齿孔', 1: '上下齿孔'}.entries)
            ChoiceChip(
                label: Text(entry.value),
                selected: t.filmVariant == entry.key,
                onSelected: (_) =>
                    onChanged(t.copyWith(filmVariant: entry.key))),
        ]),
        SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('显示齿孔'),
            value: t.holes,
            onChanged: (v) => onChanged(t.copyWith(holes: v))),
        _field(context, '胶片文字', t.filmLabel, 128,
            (v) => onChanged(t.copyWith(filmLabel: v))),
        _field(context, '帧编号', t.frameNumber, 128,
            (v) => onChanged(t.copyWith(frameNumber: v))),
        _field(context, '日期 / 附注', t.timestamp, 256,
            (v) => onChanged(t.copyWith(timestamp: v))),
        const Text('黑色底片与琥珀色文字；照片保持原比例。',
            style: TextStyle(fontSize: 11, color: Colors.white54)),
      ]);
    }
    return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      const Text('平台样式'),
      const SizedBox(height: 10),
      Wrap(spacing: 6, runSpacing: 6, children: [
        for (final entry in const {
          0: 'Instagram',
          1: 'Facebook',
          2: 'X',
          3: '微信朋友圈'
        }.entries)
          ChoiceChip(
              label: Text(entry.value),
              selected: t.variant == entry.key,
              onSelected: (_) => onChanged(t.copyWith(variant: entry.key))),
      ]),
      const SizedBox(height: 10),
      const Text('生成平台风格的照片卡片，可保存为图片。',
          style: TextStyle(fontSize: 11, color: Colors.white54)),
      SwitchListTile(
          contentPadding: EdgeInsets.zero,
          title: const Text('显示头像'),
          value: t.showAvatar,
          onChanged: (v) => onChanged(t.copyWith(showAvatar: v))),
      if (t.showAvatar) ...[
        if (t.avatarPath.isNotEmpty)
          Align(
              alignment: Alignment.centerLeft,
              child: ClipOval(
                  child: Image.file(File(t.avatarPath),
                      width: 48,
                      height: 48,
                      fit: BoxFit.cover,
                      errorBuilder: (_, error, stack) => const Icon(
                          Icons.account_circle_outlined,
                          size: 48)))),
        OutlinedButton.icon(
            onPressed: onAvatar,
            icon: const Icon(Icons.account_circle_outlined),
            label: const Text('选择头像 PNG')),
        if (t.avatarPath.isNotEmpty)
          TextButton(
              onPressed: () => onChanged(t.copyWith(avatarPath: '')),
              child: const Text('移除头像')),
        const Text('未选图片时显示纯色头像；PNG ≤4MB、≤100万像素。',
            style: TextStyle(fontSize: 10, color: Colors.white38)),
      ],
      _field(
          context, '昵称', t.title, 128, (v) => onChanged(t.copyWith(title: v))),
      if (t.variant != 0)
        _field(context, '账号 / 用户信息', t.account, 128,
            (v) => onChanged(t.copyWith(account: v))),
      _field(context, '地点', t.location, 256,
          (v) => onChanged(t.copyWith(location: v))),
      _field(context, '配文', t.body, 1024, (v) => onChanged(t.copyWith(body: v)),
          multiline: true),
      _field(context, '时间', t.timestamp, 256,
          (v) => onChanged(t.copyWith(timestamp: v))),
      _field(context, '点赞 / 互动文字', t.stats, 256,
          (v) => onChanged(t.copyWith(stats: v))),
      SwitchListTile(
          contentPadding: EdgeInsets.zero,
          title: const Text('显示互动图标'),
          value: t.showActions,
          onChanged: (v) => onChanged(t.copyWith(showActions: v))),
    ]);
  }
}
