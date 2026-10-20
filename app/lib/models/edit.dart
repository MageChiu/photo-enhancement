import 'dart:convert';

const controlNames = <String>[
  'exposure', 'contrast', 'highlights', 'shadows', 'whites', 'blacks',
  'temperature', 'tint', 'saturation', 'vibrance',
];
const controlLabels = <String>[
  '曝光', '对比度', '高光', '阴影', '白色', '黑色', '色温', '色调', '饱和度', '自然饱和度',
];

/// Serializable edit recipe only: never contains a native pointer or pixel data.
class EditRecipe {
  EditRecipe({
    List<double>? values,
    this.lutPath,
    this.lutName,
    this.strength = 1,
    this.borderWidth = 0,
    this.borderBottom = 0,
    this.borderColor = 0xffffff,
    this.caption = '',
    this.fontPath = '',
  }) : values = List.unmodifiable(values ?? List<double>.filled(10, 0));

  final List<double> values;
  final String? lutPath;
  final String? lutName;
  final double strength, borderWidth, borderBottom;
  final int borderColor;
  final String caption, fontPath;

  EditRecipe copyWith({
    List<double>? values, String? lutPath, String? lutName, bool clearLut = false,
    double? strength, double? borderWidth, double? borderBottom,
    int? borderColor, String? caption, String? fontPath,
  }) => EditRecipe(
    values: values ?? this.values,
    lutPath: clearLut ? null : lutPath ?? this.lutPath,
    lutName: clearLut ? null : lutName ?? this.lutName,
    strength: strength ?? this.strength,
    borderWidth: borderWidth ?? this.borderWidth,
    borderBottom: borderBottom ?? this.borderBottom,
    borderColor: borderColor ?? this.borderColor,
    caption: caption ?? this.caption, fontPath: fontPath ?? this.fontPath,
  );

  EditRecipe control(int index, double value) {
    final updated = List<double>.of(values)..[index] = value;
    return copyWith(values: updated);
  }

  Map<String, Object?> toJson() => {
    'version': 1,
    'params': {for (var i = 0; i < 10; ++i) controlNames[i]: values[i]},
    'lutPath': lutPath, 'lutName': lutName, 'strength': strength,
    'border': {'width': borderWidth, 'bottom': borderBottom, 'color': borderColor,
      'caption': caption, 'fontPath': fontPath},
  };

  factory EditRecipe.fromJson(Map<String, dynamic> json) {
    if (json['version'] != 1) throw const FormatException('不支持的预设版本');
    final params = json['params'];
    if (params is! Map) throw const FormatException('预设缺少参数');
    double number(Object? value, double low, double high, [double fallback = 0]) {
      if (value == null) return fallback;
      if (value is! num || !value.isFinite || value < low || value > high) {
        throw const FormatException('预设参数超出范围');
      }
      return value.toDouble();
    }
    String string(Object? value, int max) {
      if (value == null) return '';
      if (value is! String || utf8.encode(value).length >= max) {
        throw const FormatException('预设文本过长或格式错误');
      }
      return value;
    }
    final border = json['border'] ?? <String, dynamic>{};
    if (border is! Map) throw const FormatException('边框参数格式错误');
    final color = border['color'] ?? 0xffffff;
    if (color is! int || color < 0 || color > 0xffffff) {
      throw const FormatException('边框颜色无效');
    }
    final path = string(json['lutPath'], 4096);
    final name = string(json['lutName'], 256);
    return EditRecipe(
      values: [for (var i = 0; i < 10; ++i)
        number(params[controlNames[i]], i == 0 ? -5 : -1, i == 0 ? 5 : 1)],
      lutPath: path.isEmpty ? null : path,
      lutName: name.isEmpty ? null : name,
      strength: number(json['strength'], 0, 1, 1),
      borderWidth: number(border['width'], 0, .25),
      borderBottom: number(border['bottom'], 0, .4), borderColor: color,
      caption: string(border['caption'], 512), fontPath: string(border['fontPath'], 1024),
    );
  }

  @override
  bool operator ==(Object other) => other is EditRecipe && jsonEncode(toJson()) == jsonEncode(other.toJson());
  @override
  int get hashCode => jsonEncode(toJson()).hashCode;
}

/// History stores recipes, not full-resolution copies. One drag = one entry.
class EditHistory {
  final List<EditRecipe> _undo = [], _redo = [];
  EditRecipe current = EditRecipe();
  bool get canUndo => _undo.isNotEmpty;
  bool get canRedo => _redo.isNotEmpty;
  void reset([EditRecipe? recipe]) { current = recipe ?? EditRecipe(); _undo.clear(); _redo.clear(); }
  void commit(EditRecipe next, {EditRecipe? previous}) {
    final before = previous ?? current;
    if (next == before) return;
    _undo.add(before);
    if (_undo.length > 100) _undo.removeAt(0);
    current = next; _redo.clear();
  }
  void undo() { if (canUndo) { _redo.add(current); current = _undo.removeLast(); } }
  void redo() { if (canRedo) { _undo.add(current); current = _redo.removeLast(); } }
}
